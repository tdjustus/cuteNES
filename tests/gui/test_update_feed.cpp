// G097 (research R16; spec FR-092, FR-094): choosing an update from the
// release feed, with no network. Expectations come from R16 and G093's asset
// names, not from a run:
// - versions are vMAJOR.MINOR.PATCH, compared part by part as numbers; a
//   suffix ("-rc1") is a prerelease and never offered;
// - the choice is the highest version above the running one that is not a
//   draft or prerelease and has this platform's asset and SHA256SUMS,
//   unless it is the skipped version (a newer one is offered);
// - a SHA256SUMS line is "<hex>  <name>" or "<hex> *<name>";
// - only HTTPS is fetched, but for a loopback test feed when allowed.

#include "update/update_feed.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <gtest/gtest.h>

using cutenes::app::update::Version;
namespace update = cutenes::app::update;

namespace {

Version v(const char* text)
{
    const auto parsed = Version::parse(QString::fromLatin1(text));
    EXPECT_TRUE(parsed) << text;
    return parsed.value_or(Version{});
}

// One release of GitHub's list (only the fields the choice reads).
QJsonObject release(const QString& tag, const QStringList& assets, bool draft = false, bool prerelease = false)
{
    QJsonArray list;
    for (const QString& name : assets) {
        list.append(QJsonObject{{QStringLiteral("name"), name},
            {QStringLiteral("browser_download_url"),
                QStringLiteral("https://github.com/tdjustus/cuteNES/releases/download/%1/%2").arg(tag, name)}});
    }
    return QJsonObject{{QStringLiteral("tag_name"), tag}, {QStringLiteral("draft"), draft},
        {QStringLiteral("prerelease"), prerelease},
        {QStringLiteral("html_url"), QStringLiteral("https://github.com/tdjustus/cuteNES/releases/tag/") + tag},
        {QStringLiteral("body"), QStringLiteral("Notes for ") + tag}, {QStringLiteral("assets"), list}};
}

QStringList full(const QString& version)
{
    return {QStringLiteral("cuteNES-%1-macos-arm64.zip").arg(version),
        QStringLiteral("cuteNES-%1-windows-x64.zip").arg(version),
        QStringLiteral("cuteNES-%1-linux-x86_64.AppImage").arg(version), QStringLiteral("SHA256SUMS")};
}

QByteArray feed(const QJsonArray& releases)
{
    return QJsonDocument(releases).toJson();
}

const QString kMac = QStringLiteral("macos-arm64.zip");

} // namespace

TEST(UpdateFeed, VersionsCompareByNumberAndSuffixesAreNotVersions)
{
    EXPECT_GT(v("v1.2.10"), v("v1.2.9"));
    EXPECT_GT(v("2.0.0"), v("v1.99.99"));
    EXPECT_EQ(v("v0.1.0"), v("0.1.0"));
    EXPECT_EQ(v("v1.2.3").toString(), QStringLiteral("1.2.3"));
    for (const char* bad : {"v1.2", "1.2.3.4", "v1.2.3-rc1", "", "vx.1.2", "1.2.-3", "core-v0.1.0"}) {
        EXPECT_FALSE(Version::parse(QString::fromLatin1(bad))) << bad;
    }
}

TEST(UpdateFeed, TheAssetNamesAreG093s)
{
    EXPECT_EQ(update::assetName(v("1.2.0"), kMac), QStringLiteral("cuteNES-1.2.0-macos-arm64.zip"));
    EXPECT_EQ(update::assetName(v("1.2.0"), QStringLiteral("windows-x64.zip")),
        QStringLiteral("cuteNES-1.2.0-windows-x64.zip"));
    EXPECT_EQ(update::assetName(v("1.2.0"), QStringLiteral("linux-x86_64.AppImage")),
        QStringLiteral("cuteNES-1.2.0-linux-x86_64.AppImage"));
    // The running build's is one of them.
    const QString mine = update::platformSuffix();
    EXPECT_TRUE(mine == QLatin1String("macos-arm64.zip") || mine == QLatin1String("macos-x86_64.zip")
        || mine == QLatin1String("windows-x64.zip") || mine == QLatin1String("linux-x86_64.AppImage"))
        << mine.toStdString();
}

TEST(UpdateFeed, TheChoiceIsTheHighestUsableReleaseAboveTheRunningOne)
{
    // GitHub lists newest first, but the choice must not rely on it.
    const QByteArray releases = feed({
        release(QStringLiteral("v1.5.0"), full(QStringLiteral("1.5.0")), true),        // a draft
        release(QStringLiteral("v1.4.0-rc1"), full(QStringLiteral("1.4.0-rc1")), false, true), // a prerelease
        release(QStringLiteral("v1.3.0"), {QStringLiteral("cuteNES-1.3.0-windows-x64.zip"), QStringLiteral("SHA256SUMS")}),
        release(QStringLiteral("v1.2.1"), {QStringLiteral("cuteNES-1.2.1-macos-arm64.zip")}), // no SHA256SUMS
        release(QStringLiteral("v1.1.0"), full(QStringLiteral("1.1.0"))),
        release(QStringLiteral("v1.2.0"), full(QStringLiteral("1.2.0"))),
        release(QStringLiteral("core-v9.0.0"), full(QStringLiteral("9.0.0"))), // the core's own tags
        release(QStringLiteral("v0.9.0"), full(QStringLiteral("0.9.0"))),
    });
    QString error;
    const auto chosen = update::chooseRelease(releases, v("1.0.0"), kMac, QString(), &error);
    ASSERT_TRUE(chosen) << error.toStdString();
    EXPECT_EQ(chosen->version, v("1.2.0"));
    EXPECT_EQ(chosen->assetName, QStringLiteral("cuteNES-1.2.0-macos-arm64.zip"));
    EXPECT_EQ(chosen->asset,
        QUrl(QStringLiteral("https://github.com/tdjustus/cuteNES/releases/download/v1.2.0/cuteNES-1.2.0-macos-arm64.zip")));
    EXPECT_EQ(chosen->sums, QUrl(QStringLiteral("https://github.com/tdjustus/cuteNES/releases/download/v1.2.0/SHA256SUMS")));
    EXPECT_EQ(chosen->page, QUrl(QStringLiteral("https://github.com/tdjustus/cuteNES/releases/tag/v1.2.0")));
    EXPECT_EQ(chosen->notes, QStringLiteral("Notes for v1.2.0"));

    // Windows has 1.3.0.
    EXPECT_EQ(update::chooseRelease(releases, v("1.0.0"), QStringLiteral("windows-x64.zip"), QString(), &error)->version,
        v("1.3.0"));
    // Up to date, or newer than every release: nothing, and no error.
    error.clear();
    EXPECT_FALSE(update::chooseRelease(releases, v("1.2.0"), kMac, QString(), &error));
    EXPECT_TRUE(error.isEmpty()) << error.toStdString();
    EXPECT_FALSE(update::chooseRelease(releases, v("3.0.0"), kMac, QString(), &error));
}

TEST(UpdateFeed, ASkippedVersionIsNotOfferedButANewerOneIs)
{
    QByteArray releases = feed({release(QStringLiteral("v1.2.0"), full(QStringLiteral("1.2.0")))});
    EXPECT_FALSE(update::chooseRelease(releases, v("1.0.0"), kMac, QStringLiteral("1.2.0"), nullptr));
    releases = feed({release(QStringLiteral("v1.3.0"), full(QStringLiteral("1.3.0"))),
        release(QStringLiteral("v1.2.0"), full(QStringLiteral("1.2.0")))});
    EXPECT_EQ(update::chooseRelease(releases, v("1.0.0"), kMac, QStringLiteral("1.2.0"), nullptr)->version, v("1.3.0"));
    // Skipping the newest falls back to nothing, not to an older release:
    // the skipped one is what the user declined.
    EXPECT_FALSE(update::chooseRelease(releases, v("1.0.0"), kMac, QStringLiteral("1.3.0"), nullptr));
}

TEST(UpdateFeed, ABadFeedIsAnErrorNotAChoice)
{
    QString error;
    EXPECT_FALSE(update::chooseRelease("not json", v("1.0.0"), kMac, QString(), &error));
    EXPECT_FALSE(error.isEmpty());
    error.clear();
    EXPECT_FALSE(update::chooseRelease(R"({"message":"API rate limit exceeded"})", v("1.0.0"), kMac, QString(), &error));
    EXPECT_TRUE(error.contains(QStringLiteral("rate limit"))) << error.toStdString();
    // An asset served from anywhere but HTTPS is not offered.
    QJsonObject r = release(QStringLiteral("v1.2.0"), full(QStringLiteral("1.2.0")));
    QJsonArray assets = r[QStringLiteral("assets")].toArray();
    QJsonObject mac = assets[0].toObject();
    mac[QStringLiteral("browser_download_url")] = QStringLiteral("http://example.com/cuteNES-1.2.0-macos-arm64.zip");
    assets[0] = mac;
    r[QStringLiteral("assets")] = assets;
    EXPECT_FALSE(update::chooseRelease(feed({r}), v("1.0.0"), kMac, QString(), nullptr));
}

TEST(UpdateFeed, AChecksumIsReadFromEitherSha256sumsForm)
{
    const QByteArray sums =
        "f3ed957eae4c254ebfafd86a0a0e26786624cbc95228f2980668e01eebc06c4e  cuteNES-0.1.0-linux-x86_64.AppImage\n"
        "c8ff3b64748782828830d22f25a25e36d43a88a171dca3e55af023a361890010  cuteNES-0.1.0-macos-arm64.zip\n"
        "1B4F3D633922B2A5C13CCBFC73D200F146BF5CC71429B8F1FF50AE8F185D3D04 *cuteNES-0.1.0-windows-x64.zip\r\n";
    EXPECT_EQ(update::checksumFor(sums, QStringLiteral("cuteNES-0.1.0-macos-arm64.zip")),
        QByteArray("c8ff3b64748782828830d22f25a25e36d43a88a171dca3e55af023a361890010"));
    EXPECT_EQ(update::checksumFor(sums, QStringLiteral("cuteNES-0.1.0-windows-x64.zip")),
        QByteArray("1b4f3d633922b2a5c13ccbfc73d200f146bf5cc71429b8f1ff50ae8f185d3d04"));
    EXPECT_FALSE(update::checksumFor(sums, QStringLiteral("cuteNES-0.1.0-macos-arm64")));   // a prefix
    EXPECT_FALSE(update::checksumFor(sums, QStringLiteral("AppImage")));                    // a suffix
    EXPECT_FALSE(update::checksumFor("abc  cuteNES-0.1.0-macos-arm64.zip\n", QStringLiteral("cuteNES-0.1.0-macos-arm64.zip")));
}

TEST(UpdateFeed, OnlyHttpsIsFetchedButALoopbackTestFeed)
{
    EXPECT_TRUE(update::urlAllowed(QUrl(QStringLiteral("https://api.github.com/repos/x/y/releases")), false));
    EXPECT_FALSE(update::urlAllowed(QUrl(QStringLiteral("http://api.github.com/repos/x/y/releases")), false));
    EXPECT_FALSE(update::urlAllowed(QUrl(QStringLiteral("http://127.0.0.1:8080/releases")), false));
    EXPECT_TRUE(update::urlAllowed(QUrl(QStringLiteral("http://127.0.0.1:8080/releases")), true));
    EXPECT_TRUE(update::urlAllowed(QUrl(QStringLiteral("http://localhost:8080/releases")), true));
    EXPECT_FALSE(update::urlAllowed(QUrl(QStringLiteral("http://192.168.1.2/releases")), true));
    EXPECT_FALSE(update::urlAllowed(QUrl(QStringLiteral("file:///etc/passwd")), true));
    EXPECT_FALSE(update::urlAllowed(QUrl(), true));
}
