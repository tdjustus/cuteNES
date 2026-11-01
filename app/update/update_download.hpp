#pragma once

// Downloading a release and checking it (research R16 "Integrity"; spec
// FR-094): SHA256SUMS first, then the build, streamed to a hidden file
// beside the install (the same file system, so installing is a rename) and
// hashed as it arrives. Only a build whose SHA-256 is its SHA256SUMS line is
// reported done; anything else (a network error, a short file, a mismatch,
// Cancel) removes the file.
//
// General: any release with a SHA256SUMS can be fetched this way.

#include "update/update_feed.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

namespace cutenes::app::update {

class UpdateDownload final : public QObject {
    Q_OBJECT

public:
    explicit UpdateDownload(QObject* parent = nullptr);
    ~UpdateDownload() override;

    // Downloads `release`'s build into `folder` as .<asset name>.download.
    void start(const Release& release, const QString& folder, bool allowLoopbackHttp);
    void cancel();

signals:
    void progress(qint64 received, qint64 total);
    void finished(const QString& path); // checked
    void failed(const QString& why);    // nothing is left behind
    void cancelled();

private:
    void fetchBuild();
    void fail(const QString& why);

    QNetworkAccessManager* m_network;
    QPointer<QNetworkReply> m_reply;
    Release m_release;
    bool m_allowLoopback = false;
    QByteArray m_expected;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    bool m_cancelled = false;
};

} // namespace cutenes::app::update
