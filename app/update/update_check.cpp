#include "update/update_check.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace cutenes::app::update {

#ifndef CUTENES_PACKAGE
#define CUTENES_PACKAGE "none"
#endif

InstallSupport installSupport()
{
    if (QLatin1String(CUTENES_PACKAGE) == QLatin1String("none")) {
        return {false, QObject::tr("This copy of cuteNES was not installed from a cuteNES release package, so it "
                                   "cannot replace itself. See Details opens the release page.")};
    }
    // Release packages install in G099.
    return {false, QObject::tr("Installing updates from within cuteNES is coming in a later update. See Details "
                               "opens the release page.")};
}

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
        const std::optional<Release> release = chooseRelease(body, running, suffix, skipped, &why);
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
