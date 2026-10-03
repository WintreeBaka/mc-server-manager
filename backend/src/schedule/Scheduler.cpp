#include "schedule/Scheduler.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include "backup/BackupService.h"
#include "core/JsonUtil.h"
#include "core/Logger.h"
#include "server/ServerStore.h"

namespace mcsm {

QJsonObject Scheduler::TickReport::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("serversChecked"), serversChecked);
    object.insert(QStringLiteral("backupsCreated"), backupsCreated);
    object.insert(QStringLiteral("backupsPruned"), backupsPruned);
    object.insert(QStringLiteral("messages"), QJsonArray::fromStringList(messages));
    object.insert(QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    return object;
}

bool Scheduler::isDue(const ServerRecord &record, const QDateTime &now, QDateTime *nextDue)
{
    const QJsonObject schedule = record.backupSchedule;
    if (!Json::boolean(schedule, QStringLiteral("enabled"), false)) {
        if (nextDue)
            *nextDue = QDateTime();
        return false;
    }
    const int interval = qMax(5, Json::integer(schedule, QStringLiteral("intervalMinutes"), 180));
    QDateTime base = record.lastBackupAt.isValid() ? record.lastBackupAt : record.createdAt;
    if (!base.isValid())
        base = now;
    const QDateTime due = base.addSecs(qint64(interval) * 60);
    if (nextDue)
        *nextDue = due;
    return now >= due;
}

Scheduler::TickReport Scheduler::tick(bool verbose)
{
    TickReport report;
    ServerStore store;
    store.reload();
    const QDateTime now = QDateTime::currentDateTime();

    for (const ServerRecord &record : store.all()) {
        ++report.serversChecked;
        QDateTime nextDue;
        if (!isDue(record, now, &nextDue))
            continue;
        const bool includePlugins = Json::boolean(record.backupSchedule,
                                                 QStringLiteral("includePlugins"), true);
        const bool saveFirst = Json::boolean(record.backupSchedule,
                                            QStringLiteral("saveBeforeBackup"), true);
        const Result result = BackupService::create(store, record.id,
                                                    QStringLiteral("定时自动备份"),
                                                    true, includePlugins, saveFirst);
        if (result.isOk()) {
            ++report.backupsCreated;
            report.backupsPruned += Json::integer(result.data(), QStringLiteral("pruned"), 0);
            const QString name = Json::str(result.data(), QStringLiteral("name"));
            report.messages << QStringLiteral("%1: 已创建备份 %2").arg(record.name, name);
            if (verbose)
                Logger::info(QStringLiteral("schedule"), report.messages.last());
        } else {
            report.messages << QStringLiteral("%1: 自动备份失败 - %2")
                                   .arg(record.name, result.errorMessage());
            Logger::warn(QStringLiteral("schedule"), report.messages.last());
        }
    }
    return report;
}

int Scheduler::runDaemon(int intervalSeconds, bool once, bool verbose)
{
    const int interval = qBound(15, intervalSeconds, 3600);

    auto emitReport = [verbose]() {
        const TickReport report = Scheduler::tick(verbose);
        // Same envelope as every other command so machine consumers can parse
        // daemon output with the shared helper.
        const QByteArray line = Result::ok(report.toJson()).toBytes(false) + "\n";
        fwrite(line.constData(), 1, size_t(line.size()), stdout);
        fflush(stdout);
    };

    if (once) {
        // Run a single pass without entering the event loop (quit() before
        // exec() would be a no-op and the daemon would idle for one interval).
        emitReport();
        return 0;
    }

    auto *timer = new QTimer();
    timer->setInterval(interval * 1000);
    QObject::connect(timer, &QTimer::timeout, emitReport);
    timer->start();
    emitReport();
    const int code = QCoreApplication::exec();
    delete timer;
    return code;
}

} // namespace mcsm
