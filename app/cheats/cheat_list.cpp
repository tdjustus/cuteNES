#include "cheats/cheat_list.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace cutenes::app {

QString kindName(Cheat::Kind kind)
{
    switch (kind) {
    case Cheat::Kind::Freeze: return QStringLiteral("freeze");
    case Cheat::Kind::Set: return QStringLiteral("set");
    case Cheat::Kind::GameGenie: return QStringLiteral("game_genie");
    }
    return {};
}

std::optional<Cheat::Kind> kindFromName(const QString& name)
{
    for (const Cheat::Kind kind : {Cheat::Kind::Freeze, Cheat::Kind::Set, Cheat::Kind::GameGenie}) {
        if (name == kindName(kind)) return kind;
    }
    return std::nullopt;
}

CheatList CheatList::read(const QString& path, QString* error)
{
    CheatList list;
    QFile file(path);
    if (!file.exists()) return list;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(path);
        return list;
    }
    QJsonParseError parse;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll(), &parse).object();
    if (parse.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("%1 is not valid JSON: %2").arg(path, parse.errorString());
        return list;
    }
    for (const QJsonValue& v : root.value(QStringLiteral("cheats")).toArray()) {
        const QJsonObject o = v.toObject();
        const std::optional<Cheat::Kind> kind = kindFromName(o.value(QStringLiteral("kind")).toString());
        if (!kind) continue; // a kind from a newer version: skipped, not lost on purpose
        Cheat c;
        c.label = o.value(QStringLiteral("label")).toString();
        c.kind = *kind;
        c.address = static_cast<std::uint16_t>(o.value(QStringLiteral("address")).toInt());
        c.value = static_cast<std::uint8_t>(o.value(QStringLiteral("value")).toInt());
        c.code = o.value(QStringLiteral("code")).toString();
        c.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        list.cheats.push_back(c);
    }
    for (const QJsonValue& v : root.value(QStringLiteral("watches")).toArray()) {
        const QJsonObject o = v.toObject();
        Watch w;
        w.label = o.value(QStringLiteral("label")).toString();
        w.address = static_cast<std::uint16_t>(o.value(QStringLiteral("address")).toInt());
        w.size = o.value(QStringLiteral("size")).toInt(1) == 2 ? 2 : 1;
        list.watches.push_back(w);
    }
    return list;
}

bool CheatList::write(const QString& path, const GameIdentity& identity, QString* error) const
{
    if (empty()) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            if (error) *error = QStringLiteral("cannot remove %1").arg(path);
            return false;
        }
        return true;
    }
    QJsonArray cheatArray;
    for (const Cheat& c : cheats) {
        QJsonObject o{{QStringLiteral("label"), c.label}, {QStringLiteral("kind"), kindName(c.kind)},
            {QStringLiteral("enabled"), c.enabled}};
        if (c.kind == Cheat::Kind::GameGenie) {
            o.insert(QStringLiteral("code"), c.code);
        } else {
            o.insert(QStringLiteral("address"), c.address);
            o.insert(QStringLiteral("value"), c.value);
        }
        cheatArray.push_back(o);
    }
    QJsonArray watchArray;
    for (const Watch& w : watches) {
        watchArray.push_back(QJsonObject{
            {QStringLiteral("label"), w.label}, {QStringLiteral("address"), w.address}, {QStringLiteral("size"), w.size}});
    }
    const QJsonObject root{{QStringLiteral("identity"), identity.sha1}, {QStringLiteral("cheats"), cheatArray},
        {QStringLiteral("watches"), watchArray}};
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit()) {
        if (error) *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    return true;
}

} // namespace cutenes::app
