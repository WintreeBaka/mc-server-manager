#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include "core/JsonUtil.h"
#include "server/ServerRecord.h"
#include "server/ServerStore.h"

namespace mcsm {

struct WorldBackup
{
    QString name;
    QString path;
    qint64 size = 0;
    QString createdAt;
    QString note;
    QString trigger;     // manual | schedule | pre-restore
    bool automatic = false;
    QStringList entries;

    QJsonObject toJson() const;
};

/// World archiving, retention and the "save first" handshake with a live server.
class BackupService
{
public:
    static QVector<WorldBackup> list(const ServerRecord &record);
    static Result create(ServerStore &store,
                         const QString &id,
                         const QString &note,
                         bool automatic = false,
                         bool includePlugins = true,
                         bool saveFirst = true,
                         const QString &trigger = QString());
    /// scope = "world" (default) restores the save and keeps plugins/configs,
    /// scope = "all" rolls the whole server directory back.
    static Result restore(ServerStore &store,
                          const QString &id,
                          const QString &name,
                          bool restartAfter,
                          const QString &scope = QStringLiteral("world"));
    static Result remove(const ServerRecord &record, const QString &name);

    static Result setSchedule(ServerStore &store, const QString &id, const QJsonObject &schedule);
    static QJsonObject scheduleInfo(const ServerRecord &record);
    static qint64 totalSize(const ServerRecord &record);
    static int prune(const ServerRecord &record, int keep);

private:
    static QStringList collectEntries(const ServerRecord &record, bool includePlugins);
    static bool archive(const ServerRecord &record,
                        const QString &outputName,
                        const QStringList &entries,
                        QString *error);
    static bool extract(const ServerRecord &record,
                        const QString &archivePath,
                        const QStringList &members,
                        QString *error);
    static QStringList archiveMembers(const QString &archivePath);
    static QStringList gameDataEntries(const ServerRecord &record);
    static QJsonObject readIndex(const ServerRecord &record);
    static bool writeIndex(const ServerRecord &record, const QJsonObject &index);
};

} // namespace mcsm
