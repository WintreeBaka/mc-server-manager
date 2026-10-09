#include "schedule/Scheduler.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>

#include "backup/BackupService.h"
#include "core/JsonUtil.h"
#include "core/Logger.h"
#include "core/ProcessRunner.h"
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

namespace {

/// Liveness probe for the process that started the daemon.
bool processAlive(qint64 pid)
{
    if (pid <= 0)
        return true;
#ifdef Q_OS_WIN
    const ProcessResult result =
        ProcessRunner::run(QStringLiteral("tasklist"),
                           {QStringLiteral("/FI"), QStringLiteral("PID eq %1").arg(pid),
                            QStringLiteral("/NH")},
                           20000);
    return result.stdOut.contains(QString::number(pid));
#else
    return QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid));
#endif
}

} // namespace

int Scheduler::runDaemon(int intervalSeconds, bool once, bool verbose, qint64 parentPid)
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

    if (parentPid > 0) {
        // checked more often than a tick so an orphan is noticed quickly
        auto *guard = new QTimer();
        guard->setInterval(5000);
        QObject::connect(guard, &QTimer::timeout, [parentPid, guard]() {
            if (processAlive(parentPid))
                return;
            Logger::info(QStringLiteral("daemon"),
                         QStringLiteral("parent process %1 is gone - exiting").arg(parentPid));
            guard->stop();
            QCoreApplication::quit();
        });
        guard->start();
    }

    emitReport();
    const int code = QCoreApplication::exec();
    delete timer;
    return code;
}

} // namespace mcsm
