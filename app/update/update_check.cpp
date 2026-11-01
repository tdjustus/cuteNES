#include "update/update_check.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace cutenes::app::update {

UpdateCheck::UpdateCheck(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

UpdateCheck::~UpdateCheck()
{
    if (m_reply) m_reply->abort();
}

void UpdateCheck::start(const QUrl& feed, bool allowLoopbackHttp, const Version& running, const QString& suffix,
    const QString& skipped)
{
    if (!urlAllowed(feed, allowLoopbackHttp)) {
        emit failed(tr("the release feed %1 is not HTTPS").arg(feed.toString()));
        return;
    }
    m_allowLoopback = allowLoopbackHttp;
    QNetworkRequest request(feed);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("cuteNES/%1").arg(QStringLiteral(CUTENES_APP_VERSION)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    // Never to a less safe scheme (HTTPS to HTTP), and no cookies kept.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(30000);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this, running, suffix, skipped, reply = m_reply.data()] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        QString why;
        const std::optional<Release> release = chooseRelease(body, running, suffix, skipped, &why, m_allowLoopback);
        if (release) {
            emit found(*release);
        } else if (!why.isEmpty()) {
            emit failed(why);
        } else if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
        } else {
            emit upToDate();
        }
    });
}

} // namespace cutenes::app::update
