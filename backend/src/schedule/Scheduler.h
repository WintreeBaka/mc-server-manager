#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "server/ServerRecord.h"

namespace mcsm {

/// Background daemon: keeps an eye on every server's backup schedule.
class Scheduler
{
public:
    struct TickReport
    {
        int serversChecked = 0;
        int backupsCreated = 0;
        int backupsPruned = 0;
        QStringList messages;

        QJsonObject toJson() const;
    };

    static bool isDue(const ServerRecord &record, const QDateTime &now, QDateTime *nextDue = nullptr);
    static TickReport tick(bool verbose = false);
    /// `parentPid` > 0 makes the daemon exit as soon as that process is gone, so
    /// a killed GUI never leaves an orphan backend polling in the background.
    static int runDaemon(int intervalSeconds, bool once, bool verbose, qint64 parentPid = 0);
};

} // namespace mcsm
