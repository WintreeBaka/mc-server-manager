#include "net/HttpClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>

#include "core/Logger.h"
#include "core/StringUtil.h"

namespace mcsm {

QString HttpClient::s_userAgent =
    QStringLiteral("McServerManager/1.0 (+https://localhost) Qt/%1").arg(QLatin1String(qVersion()));

bool HttpReply::parseJson(QJsonObject *out) const
{
    if (!out)
        return false;
    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    *out = doc.object();
    return true;
}

void HttpClient::setUserAgent(const QString &userAgent)
{
    if (!userAgent.trimmed().isEmpty())
        s_userAgent = userAgent.trimmed();
}

QString HttpClient::userAgent()
{
    return s_userAgent;
}

HttpReply HttpClient::get(const QUrl &url, int timeoutMs, int maxRedirects)
{
    HttpReply reply;
    if (!url.isValid()) {
        reply.error = QStringLiteral("invalid url");
        return reply;
    }

    QNetworkAccessManager manager;
    QUrl current = url;

    for (int hop = 0; hop <= maxRedirects; ++hop) {
        QNetworkRequest request(current);
        request.setHeader(QNetworkRequest::UserAgentHeader, s_userAgent);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(timeoutMs);

        QNetworkReply *handle = manager.get(request);
        QEventLoop loop;
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(handle, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&guard, &QTimer::timeout, &loop, [&loop, handle]() {
            handle->abort();
            loop.quit();
        });
        guard.start(timeoutMs + 2000);
        loop.exec();

        const int status = handle->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QVariant location = handle->attribute(QNetworkRequest::RedirectionTargetAttribute);
        const QByteArray payload = handle->readAll();
        const QString contentType = handle->header(QNetworkRequest::ContentTypeHeader).toString();
        const QNetworkReply::NetworkError netError = handle->error();
        const QString netErrorText = handle->errorString();
        handle->deleteLater();

        if (status >= 300 && status < 400 && location.isValid()) {
            current = current.resolved(location.toUrl());
            Logger::debug(QStringLiteral("http"), QStringLiteral("redirect -> %1").arg(current.toString()));
            continue;
        }

        reply.status = status;
        reply.body = payload;
        reply.contentType = contentType;
        reply.ok = (netError == QNetworkReply::NoError) && status >= 200 && status < 300;
        if (!reply.ok) {
            reply.error = netErrorText.isEmpty()
                              ? QStringLiteral("http %1").arg(status)
                              : QStringLiteral("http %1: %2").arg(status).arg(netErrorText);
        }
        return reply;
    }

    reply.error = QStringLiteral("too many redirects");
    return reply;
}

bool HttpClient::download(const QUrl &url,
                          const QString &targetPath,
                          QString *error,
                          const ProgressFn &progress,
                          int timeoutMs)
{
    auto failWith = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!url.isValid())
        return failWith(QStringLiteral("invalid download url"));

    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly))
        return failWith(QStringLiteral("cannot open %1").arg(targetPath));

    QNetworkAccessManager manager;
    QUrl current = url;
    const int maxRedirects = 8;

    for (int hop = 0; hop <= maxRedirects; ++hop) {
        QNetworkRequest request(current);
        request.setHeader(QNetworkRequest::UserAgentHeader, s_userAgent);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(timeoutMs);

        QNetworkReply *handle = manager.get(request);
        QEventLoop loop;
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(handle, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(handle, &QNetworkReply::readyRead, &loop, [&]() {
            const QByteArray chunk = handle->readAll();
            if (!chunk.isEmpty())
                file.write(chunk);
        });
        QObject::connect(handle, &QNetworkReply::downloadProgress, &loop,
                         [&progress](qint64 got, qint64 total) {
                             if (progress)
                                 progress(got, total);
                         });
        QObject::connect(&guard, &QTimer::timeout, &loop, [&loop, handle]() {
            handle->abort();
            loop.quit();
        });
        guard.start(timeoutMs);
        loop.exec();

        const QByteArray rest = handle->readAll();
        if (!rest.isEmpty())
            file.write(rest);

        const int status = handle->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QVariant location = handle->attribute(QNetworkRequest::RedirectionTargetAttribute);
        const QString netErrorText = handle->errorString();
        const QNetworkReply::NetworkError netError = handle->error();
        handle->deleteLater();

        if (status >= 300 && status < 400 && location.isValid()) {
            current = current.resolved(location.toUrl());
            continue;
        }
        if (netError != QNetworkReply::NoError || status < 200 || status >= 300) {
            file.cancelWriting();
            return failWith(netErrorText.isEmpty()
                                ? QStringLiteral("download failed with http %1").arg(status)
                                : QStringLiteral("download failed: %1").arg(netErrorText));
        }

        if (!file.commit())
            return failWith(QStringLiteral("cannot write %1").arg(targetPath));
        Logger::info(QStringLiteral("http"),
                     QStringLiteral("downloaded %1").arg(QFileInfo(targetPath).fileName()));
        return true;
    }

    file.cancelWriting();
    return failWith(QStringLiteral("too many redirects while downloading"));
}

} // namespace mcsm
