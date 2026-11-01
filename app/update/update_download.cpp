#include "update/update_download.hpp"

#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace cutenes::app::update {

namespace {

QNetworkRequest requestFor(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("cuteNES/%1").arg(QStringLiteral(CUTENES_APP_VERSION)));
    // GitHub's asset links redirect to its storage, always over HTTPS.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(60000);
    return request;
}

} // namespace

UpdateDownload::UpdateDownload(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

UpdateDownload::~UpdateDownload()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
    if (m_file.isOpen()) {
        m_file.close();
        m_file.remove();
    }
}

void UpdateDownload::start(const Release& release, const QString& folder, bool allowLoopbackHttp)
{
    m_release = release;
    m_allowLoopback = allowLoopbackHttp;
    m_file.setFileName(QDir(folder).filePath(QLatin1Char('.') + release.assetName + QStringLiteral(".download")));
    if (!urlAllowed(release.sums, allowLoopbackHttp) || !urlAllowed(release.asset, allowLoopbackHttp)) {
        fail(tr("the release's files are not served over HTTPS"));
        return;
    }
    m_reply = m_network->get(requestFor(release.sums));
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] {
        reply->deleteLater();
        if (m_cancelled) return;
        if (reply->error() != QNetworkReply::NoError) {
            fail(tr("SHA256SUMS could not be downloaded: %1").arg(reply->errorString()));
            return;
        }
        const std::optional<QByteArray> expected = checksumFor(reply->readAll(), m_release.assetName);
        if (!expected) {
            fail(tr("SHA256SUMS has no line for %1").arg(m_release.assetName));
            return;
        }
        m_expected = *expected;
        fetchBuild();
    });
}

void UpdateDownload::fetchBuild()
{
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("cannot write %1: %2").arg(QDir::toNativeSeparators(m_file.fileName()), m_file.errorString()));
        return;
    }
    m_reply = m_network->get(requestFor(m_release.asset));
    connect(m_reply, &QNetworkReply::readyRead, this, [this, reply = m_reply.data()] {
        const QByteArray chunk = reply->readAll();
        m_hash.addData(chunk);
        if (m_file.write(chunk) != chunk.size()) {
            reply->abort();
            fail(tr("cannot write %1: %2").arg(QDir::toNativeSeparators(m_file.fileName()), m_file.errorString()));
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, &UpdateDownload::progress);
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] {
        reply->deleteLater();
        if (m_cancelled || !m_file.isOpen()) return;
        const QByteArray rest = reply->readAll();
        m_hash.addData(rest);
        m_file.write(rest);
        if (reply->error() != QNetworkReply::NoError) {
            fail(tr("%1 could not be downloaded: %2").arg(m_release.assetName, reply->errorString()));
            return;
        }
        m_file.close();
        if (m_hash.result().toHex() != m_expected) {
            m_file.remove();
            emit failed(tr("%1 does not match its SHA256SUMS line, so it was not installed").arg(m_release.assetName));
            return;
        }
        emit finished(m_file.fileName());
    });
}

void UpdateDownload::cancel()
{
    m_cancelled = true;
    if (m_reply) m_reply->abort();
    if (m_file.isOpen()) m_file.close();
    m_file.remove();
    emit cancelled();
}

void UpdateDownload::fail(const QString& why)
{
    if (m_file.isOpen()) m_file.close();
    m_file.remove();
    emit failed(why);
}

} // namespace cutenes::app::update
