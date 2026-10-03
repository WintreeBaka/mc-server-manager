#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <functional>

namespace mcsm {

struct HttpReply
{
    bool ok = false;
    int status = 0;
    QByteArray body;
    QString error;
    QString contentType;

    QString text() const { return QString::fromUtf8(body); }
    bool parseJson(QJsonObject *out) const;
};

class HttpClient
{
public:
    using ProgressFn = std::function<void(qint64 received, qint64 total)>;

    static HttpReply get(const QUrl &url, int timeoutMs = 30000, int maxRedirects = 6);
    static bool download(const QUrl &url,
                         const QString &targetPath,
                         QString *error,
                         const ProgressFn &progress = ProgressFn(),
                         int timeoutMs = 900000);
    static void setUserAgent(const QString &userAgent);
    static QString userAgent();

private:
    static QString s_userAgent;
};

} // namespace mcsm
