#include "media/movie.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace cutenes::app {

namespace {

const QByteArray kMagic = QByteArrayLiteral("CNM1\n");

QString eventName(Movie::Event::Kind kind)
{
    switch (kind) {
    case Movie::Event::Kind::Power: return QStringLiteral("power");
    case Movie::Event::Kind::Cheats: return QStringLiteral("cheats");
    case Movie::Event::Kind::Reset: break;
    }
    return QStringLiteral("reset");
}

QJsonArray cheatArray(const QVector<Cheat>& cheats)
{
    QJsonArray a;
    for (const Cheat& c : cheats) a.append(cheatToJson(c));
    return a;
}

QVector<Cheat> cheatsFrom(const QJsonArray& array)
{
    QVector<Cheat> cheats;
    for (const QJsonValue& v : array) {
        if (const std::optional<Cheat> c = cheatFromJson(v.toObject())) cheats.push_back(*c);
    }
    return cheats;
}

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

} // namespace

void Movie::truncate(qint64 frame)
{
    input.truncate(std::clamp<qint64>(frame, 0, frames()) * 2);
    events.erase(std::remove_if(events.begin(), events.end(), [&](const Event& e) { return e.frame >= frame; }),
        events.end());
}

QByteArray Movie::serialize() const
{
    QJsonArray eventArray;
    for (const Event& e : events) {
        QJsonObject o{{QStringLiteral("frame"), e.frame}, {QStringLiteral("event"), eventName(e.kind)}};
        if (e.kind == Event::Kind::Cheats) o.insert(QStringLiteral("cheats"), cheatArray(e.cheats));
        eventArray.append(o);
    }
    QJsonObject header{{QStringLiteral("identity"), identity}, {QStringLiteral("core"), core},
        {QStringLiteral("region"), region},
        {QStringLiteral("start"), start == Start::State ? QStringLiteral("state") : QStringLiteral("power_on")},
        {QStringLiteral("frames"), frames()}, {QStringLiteral("rerecords"), rerecords},
        {QStringLiteral("events"), eventArray}};
    if (start == Start::State) header.insert(QStringLiteral("state"), QString::fromLatin1(state.toBase64()));
    if (cheats) header.insert(QStringLiteral("cheats"), cheatArray(*cheats));
    if (battery) header.insert(QStringLiteral("battery"), QString::fromLatin1(battery->toBase64()));
    return kMagic + QJsonDocument(header).toJson(QJsonDocument::Compact) + '\n' + input;
}

std::optional<Movie> Movie::parse(const QByteArray& bytes, QString* error)
{
    if (!bytes.startsWith(kMagic)) {
        fail(error, QStringLiteral("not a cuteNES movie"));
        return std::nullopt;
    }
    const qsizetype end = bytes.indexOf('\n', kMagic.size());
    QJsonParseError parse;
    const QJsonDocument doc = end < 0 ? QJsonDocument()
                                      : QJsonDocument::fromJson(bytes.mid(kMagic.size(), end - kMagic.size()), &parse);
    if (end < 0 || parse.error != QJsonParseError::NoError || !doc.isObject()) {
        fail(error, QStringLiteral("the movie's header is damaged"));
        return std::nullopt;
    }
    const QJsonObject h = doc.object();
    Movie m;
    m.identity = h.value(QStringLiteral("identity")).toString();
    m.core = h.value(QStringLiteral("core")).toString();
    m.region = h.value(QStringLiteral("region")).toString();
    const QString start = h.value(QStringLiteral("start")).toString();
    if (start == QLatin1String("state")) {
        m.start = Start::State;
        m.state = QByteArray::fromBase64(h.value(QStringLiteral("state")).toString().toLatin1());
        if (m.state.isEmpty()) {
            fail(error, QStringLiteral("the movie's start state is missing"));
            return std::nullopt;
        }
    } else if (start != QLatin1String("power_on")) {
        fail(error, QStringLiteral("the movie starts from \"%1\", which this version does not know").arg(start));
        return std::nullopt;
    }
    m.rerecords = h.value(QStringLiteral("rerecords")).toInteger();
    if (h.contains(QStringLiteral("cheats"))) m.cheats = cheatsFrom(h.value(QStringLiteral("cheats")).toArray());
    if (h.contains(QStringLiteral("battery"))) {
        m.battery = QByteArray::fromBase64(h.value(QStringLiteral("battery")).toString().toLatin1());
    }
    m.input = bytes.mid(end + 1);
    const qint64 frames = h.value(QStringLiteral("frames")).toInteger(-1);
    if (frames < 0 || m.input.size() != frames * 2) {
        fail(error, QStringLiteral("the movie is cut short: %1 bytes of input for %2 frames")
                        .arg(m.input.size()).arg(frames));
        return std::nullopt;
    }
    for (const QJsonValue& v : h.value(QStringLiteral("events")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(QStringLiteral("event")).toString();
        Event e;
        e.frame = o.value(QStringLiteral("frame")).toInteger();
        if (name == QLatin1String("reset")) {
            e.kind = Event::Kind::Reset;
        } else if (name == QLatin1String("power")) {
            e.kind = Event::Kind::Power;
        } else if (name == QLatin1String("cheats")) {
            e.kind = Event::Kind::Cheats;
            e.cheats = cheatsFrom(o.value(QStringLiteral("cheats")).toArray());
        } else {
            fail(error, QStringLiteral("the movie has an unknown event \"%1\"").arg(name));
            return std::nullopt;
        }
        m.events.push_back(e);
    }
    std::stable_sort(m.events.begin(), m.events.end(), [](const Event& a, const Event& b) { return a.frame < b.frame; });
    return m;
}

bool Movie::write(const QString& path, QString* error) const
{
    if (!QDir().mkpath(QFileInfo(path).path())) return fail(error, QStringLiteral("cannot create %1").arg(QFileInfo(path).path()));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(serialize()) < 0 || !file.commit()) {
        return fail(error, QStringLiteral("cannot write %1: %2").arg(path, file.errorString()));
    }
    return true;
}

std::optional<Movie> Movie::read(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("cannot read %1: %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    return parse(file.readAll(), error);
}

} // namespace cutenes::app
