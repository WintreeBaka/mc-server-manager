#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "core/JsonUtil.h"
#include "core/ProcessRunner.h"

namespace mcsm {

struct ServerRecord;

struct DockerStatus
{
    bool cliFound = false;
    bool daemonRunning = false;
    QString version;
    QString error;

    QJsonObject toJson() const;
};

/// Container lifecycle for the JDK runtime of every managed server.
///
/// Design: one container per server, bind mounting the server directory to
/// /data. The container only provides the JDK + a start script, which keeps the
/// host clean and makes the runtime version fully reproducible.
class DockerManager
{
public:
    static QString program();
    static QString containerName(const QString &serverId);
    static QString mountPath(const QString &hostPath);

    static DockerStatus status();
    static bool imageExists(const QString &image);
    static bool ensureImage(const QString &image, QString *error, int timeoutMs = 1800000);
    static QStringList localImages(const QString &filterPrefix = QStringLiteral("eclipse-temurin"));

    static ProcessResult runContainer(const ServerRecord &record,
                                      const QString &image,
                                      const QString &startScript,
                                      QString *error);
    static bool containerExists(const QString &serverId);
    static QString containerState(const QString &serverId); // running|exited|created|missing
    static ProcessResult startExisting(const QString &serverId);
    static ProcessResult stopContainer(const QString &serverId, int timeoutSec = 45, bool force = false);
    static ProcessResult removeContainer(const QString &serverId, bool force = true);
    static ProcessResult exec(const QString &serverId,
                              const QStringList &command,
                              int timeoutMs = 120000,
                              const QByteArray &stdinData = QByteArray());
    static QString logs(const QString &serverId, int tailLines = 200, bool timestamps = false);
    static QJsonObject stats(const QString &serverId);
    static ProcessResult inspect(const QString &serverId, const QString &format);
};

} // namespace mcsm
