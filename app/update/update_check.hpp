#pragma once

// The once-per-launch update check (research R16 "Feed"; spec FR-091,
// FR-092): one GET of the release list, and the choice update_feed makes
// from it. It sends only the request GitHub needs (a User-Agent naming
// cuteNES and its version, and the JSON Accept type): no identifier, no
// settings, no cookies.
//
// installSupport() says whether this build can replace itself (FR-096):
// only a release package (CUTENES_PACKAGE, set by the CI package job) can.

#include "update/update_feed.hpp"

#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

namespace cutenes::app::update {

struct InstallSupport {
    bool possible = false;
    QString why; // when not possible: a sentence for the user
};
InstallSupport installSupport();

class UpdateCheck final : public QObject {
    Q_OBJECT

public:
    explicit UpdateCheck(QObject* parent = nullptr);
    ~UpdateCheck() override;

    // Fetches `feed` (HTTPS, or a loopback HTTP test feed when allowed) and
    // reports one of the signals below, once.
    void start(const QUrl& feed, bool allowLoopbackHttp, const Version& running, const QString& suffix,
        const QString& skipped);

signals:
    void found(const cutenes::app::update::Release& release);
    void upToDate();
    void failed(const QString& why);

private:
    QNetworkAccessManager* m_network;
    QPointer<QNetworkReply> m_reply;
};

} // namespace cutenes::app::update
