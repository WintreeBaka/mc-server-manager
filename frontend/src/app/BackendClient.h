#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QStringList>

#include <functional>

namespace mcsm {

struct Reply
{
    bool ok = false;
    QJsonObject data;
    QStringList warnings;
    QString code;
    QString message;
    QString detail;
    QString rawOutput;

    QString errorText() const;
    QJsonValue value(const QString &key) const { return data.value(key); }
};

class BackendClient : public QObject
{
    Q_OBJECT

public:
    using ResultHandler = std::function<void(const Reply &)>;
    using ProgressHandler = std::function<void(const QString &stage, int percent, const QString &detail)>;
    using LineHandler = std::function<void(const QJsonObject &line)>;

    explicit BackendClient(QObject *parent = nullptr);

    QString executable() const { return m_executable; }
    void setExecutable(const QString &path);
    bool isConfigured() const;
    static QString locateBackend();
    /// Path of the mcsm-cli that ships next to the GUI executable.
    static QString siblingBackend();

    QString dataHome() const { return m_dataHome; }
    void setDataHome(const QString &home);

    void request(const QStringList &arguments,
                 QObject *context,
                 ResultHandler handler,
                 ProgressHandler progress = ProgressHandler());

    Reply requestSync(const QStringList &arguments, int timeoutMs = 120000);

    QProcess *streamLogs(const QString &serverId, int tail, QObject *context, LineHandler onLine);

    int pendingCount() const { return m_active.size(); }

signals:
    void availabilityChanged(bool available);
    void commandStarted(const QStringList &arguments);
    void commandFinished(const QStringList &arguments, bool ok);

private:
    QStringList globalFlags() const;

    QString m_executable;
    QString m_dataHome;
    QSet<QProcess *> m_active;
};

} // namespace mcsm
