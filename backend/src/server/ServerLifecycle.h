#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "core/JsonUtil.h"
#include "server/ServerRecord.h"
#include "server/ServerStore.h"

namespace mcsm {

struct StartOutcome
{
    bool ok = false;
    QString status;             // running | error | stopped
    QString message;
    QString hint;
    QStringList logTail;
    QString failedMarker;
    qint64 elapsedMs = 0;
    bool dockerUnavailable = false;

    Result toResult() const;
};

/// Start / stop / restart, status probing and console piping for one server.
class ServerLifecycle
{
public:
    static StartOutcome start(ServerStore &store, const QString &id, int waitSeconds = 90);
    static Result stop(ServerStore &store, const QString &id, bool force = false);
    static Result restart(ServerStore &store, const QString &id, int waitSeconds = 90);
    static Result kill(ServerStore &store, const QString &id);

    static QString runtimeState(const ServerRecord &record);
    static QJsonObject status(const ServerRecord &record, bool withStats = false);
    static Result refreshAll(ServerStore &store);
    static QStringList logTail(const ServerRecord &record, int lines);

    static Result sendCommand(ServerStore &store, const QString &id, const QString &command);
    static Result saveWorld(ServerStore &store, const QString &id);

    /// Scans the container log for well known failure signatures.
    static QString detectFailure(const QString &logText);

private:
    /// Implementation of start(): wrapped by start() so the `server.afterStart`
    /// plugin hook runs for every attempt, successful or not.
    static StartOutcome startRecord(ServerStore &store, const QString &id, int waitSeconds);
    /// Implementation of stop(): the public entry point wraps it with the
    /// `server.beforeStop` / `server.afterStop` plugin hooks.
    static Result stopRecord(ServerStore &store, ServerRecord record, bool force);
};

} // namespace mcsm
