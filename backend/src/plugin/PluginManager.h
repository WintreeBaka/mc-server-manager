#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include "core/JsonUtil.h"
#include "server/ServerRecord.h"

namespace mcsm {

struct PluginHit
{
    QString source;          // modrinth | hangar | spiget
    QString id;
    QString slug;
    QString title;
    QString description;
    QString author;
    QString iconUrl;
    QString pageUrl;
    QString downloadUrl;
    QString fileName;
    QString versionName;
    qint64 downloads = 0;
    bool installable = true;
    QStringList gameVersions;
    QStringList loaders;
    QString note;

    QJsonObject toJson() const;
};

struct InstalledPlugin
{
    QString file;
    QString name;
    qint64 size = 0;
    QString modifiedAt;
    bool enabled = true;

    QJsonObject toJson() const;
};

/// Plugin discovery + installation into <server>/plugins.
class PluginManager
{
public:
    static QStringList sources();
    static QVector<PluginHit> search(const QString &source,
                                     const QString &query,
                                     const ServerRecord &record,
                                     int limit,
                                     QString *error);
    static QVector<InstalledPlugin> installed(const ServerRecord &record);
    static Result install(const ServerRecord &record, const PluginHit &hit);
    static Result setEnabled(const ServerRecord &record, const QString &file, bool enabled);
    static Result remove(const ServerRecord &record, const QString &file);
    static bool supportsPlugins(const ServerRecord &record);

private:
    static QVector<PluginHit> searchModrinth(const QString &query, const ServerRecord &record,
                                             int limit, QString *error);
    static QVector<PluginHit> searchHangar(const QString &query, const ServerRecord &record,
                                           int limit, QString *error);
    static QVector<PluginHit> searchSpiget(const QString &query, int limit, QString *error);
    static QStringList loaderTokens(const ServerRecord &record);
    static bool resolveDownload(PluginHit *hit, const ServerRecord &record, QString *error);
};

} // namespace mcsm
