#include "backup/BackupService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QThread>

#include <algorithm>

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/ProcessRunner.h"
#include "core/StringUtil.h"
#include "docker/DockerManager.h"
#include "docker/TemplateWriter.h"
#include "server/RconClient.h"
#include "server/ServerLifecycle.h"

namespace mcsm {
namespace {

QString backupFileName(const QString &trigger, const QDateTime &when)
{
    return QStringLiteral("%1-%2-%3.tar.gz")
        .arg(trigger,
             when.toString(QStringLiteral("yyyyMMdd-HHmmss")),
             StringUtil::randomToken(4).toLower());
}

QString findHostTar()
{
    static QString cached;
    if (!cached.isEmpty())
        return cached;
    const QStringList candidates = {QStringLiteral("tar"),
                                    QStringLiteral("C:/Windows/System32/tar.exe"),
                                    QStringLiteral("/bin/tar"),
                                    QStringLiteral("/usr/bin/tar")};
    for (const QString &candidate : candidates) {
        if (ProcessRunner::programExists(candidate)) {
            cached = candidate;
            return cached;
        }
    }
    return QString();
}

} // namespace

QJsonObject WorldBackup::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("path"), path);
    object.insert(QStringLiteral("size"), double(size));
    object.insert(QStringLiteral("sizeText"), StringUtil::humanBytes(size));
    object.insert(QStringLiteral("createdAt"), createdAt);
    object.insert(QStringLiteral("note"), note);
    object.insert(QStringLiteral("trigger"), trigger);
    object.insert(QStringLiteral("automatic"), automatic);
    object.insert(QStringLiteral("entries"), QJsonArray::fromStringList(entries));
    return object;
}

QJsonObject BackupService::readIndex(const ServerRecord &record)
{
    return Json::readObjectFile(QDir(record.backupDir()).filePath(QStringLiteral("index.json")));
}

bool BackupService::writeIndex(const ServerRecord &record, const QJsonObject &index)
{
    QDir().mkpath(record.backupDir());
    return Json::writeObjectFile(QDir(record.backupDir()).filePath(QStringLiteral("index.json")),
                                 index, nullptr);
}

QVector<WorldBackup> BackupService::list(const ServerRecord &record)
{
    QVector<WorldBackup> backups;
    const QDir dir(record.backupDir());
    if (!dir.exists())
        return backups;

    const QJsonObject index = readIndex(record);
    QHash<QString, QJsonObject> meta;
    const QJsonArray entries = index.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        meta.insert(Json::str(object, QStringLiteral("name")), object);
    }

    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.tar.gz"), QStringLiteral("*.zip")},
                                                  QDir::Files, QDir::Time);
    for (const QFileInfo &info : files) {
        WorldBackup backup;
        backup.name = info.fileName();
        backup.path = info.absoluteFilePath();
        backup.size = info.size();
        backup.createdAt = info.lastModified().toString(Qt::ISODate);
        backup.trigger = backup.name.startsWith(QLatin1String("schedule")) ? QStringLiteral("schedule")
                                                                          : QStringLiteral("manual");
        backup.automatic = backup.trigger == QLatin1String("schedule");
        const auto it = meta.constFind(backup.name);
        if (it != meta.constEnd()) {
            backup.note = Json::str(it.value(), QStringLiteral("note"));
            backup.trigger = Json::str(it.value(), QStringLiteral("trigger"), backup.trigger);
            backup.automatic = Json::boolean(it.value(), QStringLiteral("automatic"), backup.automatic);
            backup.createdAt = Json::str(it.value(), QStringLiteral("createdAt"), backup.createdAt);
            backup.entries = Json::toList(it.value().value(QStringLiteral("entries")));
        }
        backups.append(backup);
    }
    return backups;
}

qint64 BackupService::totalSize(const ServerRecord &record)
{
    qint64 total = 0;
    for (const WorldBackup &backup : list(record))
        total += backup.size;
    return total;
}

QStringList BackupService::collectEntries(const ServerRecord &record, bool includePlugins)
{
    QStringList entries;
    for (const QString &entry : TemplateWriter::protectedEntries()) {
        if (!includePlugins && entry == QLatin1String("plugins"))
            continue;
        if (QFileInfo::exists(QDir(record.dir).filePath(entry)))
            entries << entry;
    }
    if (!entries.contains(record.levelName) && QFileInfo::exists(QDir(record.dir).filePath(record.levelName)))
        entries << record.levelName;
    return entries;
}

bool BackupService::archive(const ServerRecord &record,
                            const QString &outputName,
                            const QStringList &entries,
                            QString *error)
{
    if (entries.isEmpty()) {
        if (error)
            *error = QStringLiteral("没有可备份的存档内容（世界目录尚未生成？）");
        return false;
    }
    QDir().mkpath(record.backupDir());
    const QString relativeTarget = QStringLiteral("backups/") + outputName;
    const QString hostTarget = QDir(record.backupDir()).filePath(outputName);

    // 1) preferred: archive inside the container (bind mounted, guaranteed tar)
    if (DockerManager::containerState(record.id) == QLatin1String("running")) {
        QStringList escaped;
        for (const QString &entry : entries)
            escaped << QStringLiteral("'%1'").arg(QString(entry).replace(QLatin1Char('\''), QLatin1String("'\\''")));
        const QString script = QStringLiteral("cd /data && tar czf '%1' %2")
                                   .arg(relativeTarget, escaped.join(QLatin1Char(' ')));
        const ProcessResult result = DockerManager::exec(record.id,
                                                         {QStringLiteral("/bin/sh"), QStringLiteral("-c"), script},
                                                         600000);
        if (result.ok() && QFileInfo::exists(hostTarget) && QFileInfo(hostTarget).size() > 512)
            return true;
        Logger::warn(QStringLiteral("backup"),
                     QStringLiteral("container archive failed, falling back to host tar: %1")
                         .arg(result.errorText()));
    }

    // 2) host tar (bsdtar ships with Windows 10+)
    const QString hostTar = findHostTar();
    if (!hostTar.isEmpty()) {
        QStringList args {QStringLiteral("-czf"), QStringLiteral("backups/") + outputName};
        args << entries;
        const ProcessResult result = ProcessRunner::run(hostTar, args, 900000, QByteArray(), record.dir);
        if (result.ok() || QFileInfo::exists(hostTarget))
            return true;
        if (error)
            *error = result.errorText();
    } else if (error) {
        *error = QStringLiteral("系统中未找到 tar，请安装 7-Zip/bsdtar 或在运行中的服务器上创建备份");
    }
    return false;
}

QStringList BackupService::archiveMembers(const QString &archivePath)
{
    QStringList members;
    if (!archivePath.endsWith(QLatin1String(".tar.gz")))
        return members;
    const QString hostTar = findHostTar();
    if (hostTar.isEmpty())
        return members;
    const ProcessResult result = ProcessRunner::run(hostTar, {QStringLiteral("-tzf"), archivePath}, 300000);
    if (!result.ok())
        return members;
    const QStringList lines = StringUtil::splitLines(result.stdOut);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            members << trimmed;
    }
    return members;
}

QStringList BackupService::gameDataEntries(const ServerRecord &record)
{
    QStringList entries;
    const QStringList candidates = {record.levelName, QStringLiteral("world"),
                                    QStringLiteral("world_nether"), QStringLiteral("world_the_end")};
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty() || entries.contains(candidate))
            continue;
        entries << candidate;
    }
    return entries;
}

bool BackupService::extract(const ServerRecord &record,
                            const QString &archivePath,
                            const QStringList &members,
                            QString *error)
{
    if (!QFileInfo::exists(archivePath)) {
        if (error)
            *error = QStringLiteral("备份文件不存在: %1").arg(archivePath);
        return false;
    }
    const QString hostTar = findHostTar();
    if (archivePath.endsWith(QLatin1String(".tar.gz")) && !hostTar.isEmpty()) {
        QStringList arguments {QStringLiteral("-xzf"), archivePath};
        arguments << members;
        const ProcessResult result = ProcessRunner::run(hostTar, arguments, 900000, QByteArray(), record.dir);
        if (result.ok())
            return true;
        if (error)
            *error = result.errorText();
        return false;
    }
    if (archivePath.endsWith(QLatin1String(".zip"))) {
        const ProcessResult result = ProcessRunner::run(
            QStringLiteral("powershell"),
            {QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
             QStringLiteral("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                 .arg(archivePath, record.dir)},
            900000);
        if (result.ok())
            return true;
        if (error)
            *error = result.errorText();
        return false;
    }
    if (error)
        *error = QStringLiteral("无法解压 %1（缺少 tar 或 7-Zip）").arg(archivePath);
    return false;
}

Result BackupService::create(ServerStore &store,
                             const QString &id,
                             const QString &note,
                             bool automatic,
                             bool includePlugins,
                             bool saveFirst,
                             const QString &trigger)
{
    ServerRecord record = store.get(id);
    if (record.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("服务器 %1 不存在").arg(id));

    const bool running = DockerManager::containerState(record.id) == QLatin1String("running");
    const QString effectiveTrigger = trigger.isEmpty()
                                         ? (automatic ? QStringLiteral("schedule")
                                                      : QStringLiteral("manual"))
                                         : trigger;
    QStringList notes;

    // live servers: flush the world and pause chunk writing while we archive
    bool saveSuspended = false;
    if (running && saveFirst) {
        RconClient rcon(QStringLiteral("127.0.0.1"), quint16(record.rconPort), record.rconPassword, 5000);
        QString response;
        QString rconError;
        if (rcon.sendCommand(QStringLiteral("save-off"), &response, &rconError)
            && rcon.sendCommand(QStringLiteral("save-all flush"), &response, &rconError)) {
            saveSuspended = true;
            notes << QStringLiteral("已执行 save-all flush");
            QThread::msleep(1200);
        } else {
            notes << QStringLiteral("RCON 不可用，直接备份当前磁盘内容");
        }
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString name = backupFileName(effectiveTrigger, now);
    const QStringList entries = collectEntries(record, includePlugins);
    QString archiveError;
    const bool archived = archive(record, name, entries, &archiveError);

    if (saveSuspended) {
        RconClient rcon(QStringLiteral("127.0.0.1"), quint16(record.rconPort), record.rconPassword, 5000);
        QString response;
        QString rconError;
        rcon.sendCommand(QStringLiteral("save-on"), &response, &rconError);
    }

    if (!archived)
        return Result::fail(QStringLiteral("BACKUP_FAILED"), QStringLiteral("创建备份失败"), archiveError);

    WorldBackup backup;
    backup.name = name;
    backup.path = QDir(record.backupDir()).filePath(name);
    backup.size = QFileInfo(backup.path).size();
    backup.createdAt = now.toString(Qt::ISODate);
    backup.note = note;
    backup.trigger = effectiveTrigger;
    backup.automatic = automatic;
    backup.entries = entries;

    QJsonObject index = readIndex(record);
    QJsonArray list = index.value(QStringLiteral("entries")).toArray();
    list.append(backup.toJson());
    index.insert(QStringLiteral("entries"), list);
    writeIndex(record, index);

    record.lastBackupAt = now;
    store.update(record);

    const int keep = Json::integer(record.backupSchedule, QStringLiteral("keep"), 0);
    int pruned = 0;
    if (keep > 0)
        pruned = prune(record, keep);

    Logger::info(QStringLiteral("backup"),
                 QStringLiteral("created %1 (%2)").arg(name, StringUtil::humanBytes(backup.size)));
    Result result = Result::ok(backup.toJson());
    result.with(QStringLiteral("notes"), QJsonArray::fromStringList(notes));
    result.with(QStringLiteral("pruned"), pruned);
    if (!saveSuspended && running && saveFirst)
        result.warn(QStringLiteral("未能通过 RCON 暂停存档，备份为热备份"));
    return result;
}

Result BackupService::restore(ServerStore &store,
                              const QString &id,
                              const QString &name,
                              bool restartAfter,
                              const QString &scope)
{
    ServerRecord record = store.get(id);
    if (record.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("服务器 %1 不存在").arg(id));

    QString archivePath = QFileInfo(name).isAbsolute()
                              ? name
                              : QDir(record.backupDir()).filePath(name);
    if (!QFileInfo::exists(archivePath))
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("备份 %1 不存在").arg(name));

    Result safety = Result::ok();
    if (DockerManager::containerState(record.id) == QLatin1String("running")) {
        const Result stopped = ServerLifecycle::stop(store, id, false);
        if (!stopped.isOk())
            safety.warn(QStringLiteral("停服时出现警告，已尝试继续恢复"));
        record = store.get(id);
    }
    const Result preRestore = create(store, id, QStringLiteral("恢复 %1 前的自动快照").arg(name),
                                     true, true, false, QStringLiteral("pre-restore"));
    if (!preRestore.isOk())
        safety.warn(QStringLiteral("恢复前自动快照失败：%1").arg(preRestore.errorMessage()));

    // Only restore what the archive really contains, and by default only the
    // save: plugins and configuration stay untouched unless scope=all.
    const bool fullRestore = scope.compare(QLatin1String("all"), Qt::CaseInsensitive) == 0;
    const QStringList wanted = fullRestore ? collectEntries(record, true) : gameDataEntries(record);
    const QStringList available = archiveMembers(archivePath);

    QStringList targets;
    for (const QString &entry : wanted) {
        if (available.isEmpty()) {
            // zip archive or tar unavailable: fall back to the full list
            targets << entry;
            continue;
        }
        const QString prefix = entry + QLatin1Char('/');
        for (const QString &member : available) {
            if (member == entry || member.startsWith(prefix)) {
                targets << entry;
                break;
            }
        }
    }
    if (targets.isEmpty()) {
        return Result::fail(QStringLiteral("RESTORE_FAILED"),
                            QStringLiteral("备份中没有可恢复的存档内容"),
                            QStringLiteral("归档 %1 内未找到 %2")
                                .arg(name, wanted.join(QStringLiteral(", "))));
    }

    for (const QString &entry : targets) {
        const QString path = QDir(record.dir).filePath(entry);
        QFileInfo info(path);
        if (info.isDir()) {
            QDir(path).removeRecursively();
        } else if (info.isFile()) {
            QFile::remove(path);
        }
    }

    QString extractError;
    if (!extract(record, archivePath, targets, &extractError))
        return Result::fail(QStringLiteral("RESTORE_FAILED"), QStringLiteral("解压备份失败"), extractError);

    QJsonObject data;
    data.insert(QStringLiteral("restored"), name);
    data.insert(QStringLiteral("restarted"), false);
    data.insert(QStringLiteral("scope"), fullRestore ? QStringLiteral("all") : QStringLiteral("world"));
    data.insert(QStringLiteral("restoredEntries"), QJsonArray::fromStringList(targets));
    if (restartAfter) {
        const StartOutcome outcome = ServerLifecycle::start(store, id, 90);
        data = outcome.toResult().data();
        data.insert(QStringLiteral("restored"), name);
        data.insert(QStringLiteral("restarted"), outcome.ok);
        if (!outcome.ok)
            return Result::fail(QStringLiteral("RESTORE_RESTART_FAILED"),
                                QStringLiteral("存档已恢复，但服务器启动失败"), outcome.message);
    }
    Result result = Result::ok(data);
    result.with(safety.data());
    return result;
}

Result BackupService::remove(const ServerRecord &record, const QString &name)
{
    const QString path = QFileInfo(name).isAbsolute() ? name : QDir(record.backupDir()).filePath(name);
    if (!QFileInfo::exists(path))
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("备份 %1 不存在").arg(name));
    if (!QFile::remove(path))
        return Result::fail(QStringLiteral("IO_ERROR"), QStringLiteral("无法删除 %1").arg(path));

    QJsonObject index = readIndex(record);
    QJsonArray entries = index.value(QStringLiteral("entries")).toArray();
    QJsonArray filtered;
    for (const QJsonValue &value : entries) {
        if (Json::str(value.toObject(), QStringLiteral("name")) != QFileInfo(name).fileName())
            filtered.append(value);
    }
    index.insert(QStringLiteral("entries"), filtered);
    writeIndex(record, index);
    return Result::ok(QJsonObject {{QStringLiteral("removed"), name}});
}

int BackupService::prune(const ServerRecord &record, int keep)
{
    if (keep <= 0)
        return 0;
    QVector<WorldBackup> all = list(record);
    QVector<WorldBackup> automatic;
    for (const WorldBackup &backup : all) {
        if (backup.automatic)
            automatic.append(backup);
    }
    std::sort(automatic.begin(), automatic.end(), [](const WorldBackup &a, const WorldBackup &b) {
        return a.createdAt > b.createdAt;
    });
    int removed = 0;
    for (int i = keep; i < automatic.size(); ++i) {
        if (QFile::remove(automatic.at(i).path))
            ++removed;
    }
    if (removed > 0) {
        QJsonObject index = readIndex(record);
        QJsonArray entries = index.value(QStringLiteral("entries")).toArray();
        QJsonArray filtered;
        QStringList kept;
        for (const WorldBackup &backup : automatic.mid(0, keep))
            kept << backup.name;
        for (const QJsonValue &value : entries) {
            const QJsonObject object = value.toObject();
            const QString entryName = Json::str(object, QStringLiteral("name"));
            if (Json::boolean(object, QStringLiteral("automatic"), false) && !kept.contains(entryName)) {
                if (QFileInfo::exists(QDir(record.backupDir()).filePath(entryName)))
                    filtered.append(value);
            } else {
                filtered.append(value);
            }
        }
        index.insert(QStringLiteral("entries"), filtered);
        writeIndex(record, index);
    }
    return removed;
}

Result BackupService::setSchedule(ServerStore &store, const QString &id, const QJsonObject &schedule)
{
    ServerRecord record = store.get(id);
    if (record.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("服务器 %1 不存在").arg(id));

    QJsonObject merged = record.backupSchedule;
    for (auto it = schedule.constBegin(); it != schedule.constEnd(); ++it)
        merged.insert(it.key(), it.value());
    const int interval = Json::integer(merged, QStringLiteral("intervalMinutes"), 180);
    if (interval < 5)
        merged.insert(QStringLiteral("intervalMinutes"), 5);
    if (Json::integer(merged, QStringLiteral("keep"), 10) < 1)
        merged.insert(QStringLiteral("keep"), 1);
    record.backupSchedule = merged;
    store.update(record);
    return Result::ok(QJsonObject {{QStringLiteral("schedule"), merged}});
}

QJsonObject BackupService::scheduleInfo(const ServerRecord &record)
{
    QJsonObject object = record.backupSchedule;
    object.insert(QStringLiteral("backupCount"), list(record).size());
    object.insert(QStringLiteral("totalSize"), double(totalSize(record)));
    object.insert(QStringLiteral("totalSizeText"), StringUtil::humanBytes(totalSize(record)));
    object.insert(QStringLiteral("lastBackupAt"), record.lastBackupAt.toString(Qt::ISODate));
    return object;
}

} // namespace mcsm
