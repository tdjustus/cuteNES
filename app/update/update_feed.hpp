#pragma once

// Choosing an update from the release feed (research R16; spec FR-092,
// FR-094). Pure functions over the feed's bytes, so they are tested without
// a network; UpdateCheck fetches the feed.
//
// The approach is general: any application whose releases are GitHub
// Release assets with a SHA256SUMS can choose its update this way. What is
// this project's is the tag form (vMAJOR.MINOR.PATCH; the core's own
// core-v* tags are not application releases) and the asset names (G093,
// docs/packaging.md).

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <compare>
#include <optional>

namespace cutenes::app::update {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    // "v1.2.3" or "1.2.3". Anything else, a suffix such as "-rc1"
    // (a prerelease) included, is not a version.
    static std::optional<Version> parse(const QString& text);
    QString toString() const; // "1.2.3"
    auto operator<=>(const Version&) const = default;
};

// A release the user may be offered.
struct Release {
    Version version;
    QUrl page;  // the release on GitHub (See Details)
    QString notes;
    QString assetName;
    QUrl asset; // this platform's build
    QUrl sums;  // the release's SHA256SUMS
};

// This build's asset suffix: "macos-arm64.zip", "macos-x86_64.zip",
// "windows-x64.zip" or "linux-x86_64.AppImage".
QString platformSuffix();

// "cuteNES-<version>-<suffix>".
QString assetName(const Version& version, const QString& suffix);

// The highest version above `running` in GitHub's release list
// (GET /repos/<owner>/<repo>/releases) that is not a draft or prerelease
// and has the `suffix` asset and SHA256SUMS, both over HTTPS. A release
// whose version is `skipped` is passed over, and so is every release below
// it: the user declined that one, not just its number. Nothing to offer is
// nullopt with `error` left empty; a feed that cannot be read is nullopt
// with `error` saying why (GitHub's own message, for a rate limit).
std::optional<Release> chooseRelease(const QByteArray& feed, const Version& running, const QString& suffix,
    const QString& skipped, QString* error);

// The lowercase hex SHA-256 SHA256SUMS gives `name` ("<hex>  <name>", or
// "<hex> *<name>" from Windows' tool), or nullopt.
std::optional<QByteArray> checksumFor(const QByteArray& sums, const QString& name);

// HTTPS only; plain HTTP to 127.0.0.1, ::1 or localhost when
// `allowLoopbackHttp` (the tests' fake feed).
bool urlAllowed(const QUrl& url, bool allowLoopbackHttp);

} // namespace cutenes::app::update
