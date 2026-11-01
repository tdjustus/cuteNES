#include "update/update_feed.hpp"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSysInfo>

namespace cutenes::app::update {

std::optional<Version> Version::parse(const QString& text)
{
    static const QRegularExpression form(QStringLiteral("^v?(\\d{1,6})\\.(\\d{1,6})\\.(\\d{1,6})$"));
    const QRegularExpressionMatch m = form.match(text);
    if (!m.hasMatch()) return std::nullopt;
    return Version{m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()};
}

QString Version::toString() const
{
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

QString platformSuffix()
{
#if defined(Q_OS_MACOS)
    return QSysInfo::buildCpuArchitecture() == QLatin1String("arm64") ? QStringLiteral("macos-arm64.zip")
                                                                       : QStringLiteral("macos-x86_64.zip");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows-x64.zip");
#else
    return QStringLiteral("linux-x86_64.AppImage");
#endif
}

QString assetName(const Version& version, const QString& suffix)
{
    return QStringLiteral("cuteNES-%1-%2").arg(version.toString(), suffix);
}

std::optional<Release> chooseRelease(const QByteArray& feed, const Version& running, const QString& suffix,
    const QString& skipped, QString* error, bool allowLoopbackHttp)
{
    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(feed, &parse);
    if (!doc.isArray()) {
        // GitHub answers an error as an object with a message.
        if (error) {
            const QString message = doc.object().value(QStringLiteral("message")).toString();
            *error = !message.isEmpty() ? QStringLiteral("the release feed said: %1").arg(message)
                : parse.error != QJsonParseError::NoError
                ? QStringLiteral("the release feed is not JSON: %1").arg(parse.errorString())
                : QStringLiteral("the release feed is not a list of releases");
        }
        return std::nullopt;
    }
    const std::optional<Version> skip = Version::parse(skipped);
    std::optional<Release> best;
    for (const QJsonValue& value : doc.array()) {
        const QJsonObject r = value.toObject();
        if (r.value(QStringLiteral("draft")).toBool() || r.value(QStringLiteral("prerelease")).toBool()) continue;
        const std::optional<Version> version = Version::parse(r.value(QStringLiteral("tag_name")).toString());
        if (!version || *version <= running) continue;
        if (best && *version <= best->version) continue;
        Release candidate;
        candidate.version = *version;
        candidate.assetName = assetName(*version, suffix);
        candidate.page = QUrl(r.value(QStringLiteral("html_url")).toString());
        candidate.notes = r.value(QStringLiteral("body")).toString();
        for (const QJsonValue& a : r.value(QStringLiteral("assets")).toArray()) {
            const QString name = a.toObject().value(QStringLiteral("name")).toString();
            const QUrl url(a.toObject().value(QStringLiteral("browser_download_url")).toString());
            if (!urlAllowed(url, allowLoopbackHttp)) continue;
            if (name == candidate.assetName) candidate.asset = url;
            if (name == QLatin1String("SHA256SUMS")) candidate.sums = url;
        }
        if (candidate.asset.isEmpty() || candidate.sums.isEmpty()) continue;
        best = candidate;
    }
    // The skipped version, and anything below it, was declined.
    if (best && skip && best->version <= *skip) return std::nullopt;
    return best;
}

std::optional<QByteArray> checksumFor(const QByteArray& sums, const QString& name)
{
    static const QRegularExpression line(QStringLiteral("^([0-9A-Fa-f]{64}) [ *](.+)$"));
    for (QByteArray raw : sums.split('\n')) {
        if (raw.endsWith('\r')) raw.chop(1);
        const QRegularExpressionMatch m = line.match(QString::fromUtf8(raw));
        if (m.hasMatch() && m.captured(2) == name) return m.captured(1).toLower().toLatin1();
    }
    return std::nullopt;
}

bool urlAllowed(const QUrl& url, bool allowLoopbackHttp)
{
    if (!url.isValid() || url.host().isEmpty()) return false;
    if (url.scheme() == QLatin1String("https")) return true;
    if (url.scheme() != QLatin1String("http") || !allowLoopbackHttp) return false;
    return url.host() == QLatin1String("localhost") || QHostAddress(url.host()).isLoopback();
}

} // namespace cutenes::app::update
