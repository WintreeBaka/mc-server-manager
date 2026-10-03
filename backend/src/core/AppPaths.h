#pragma once

#include <QString>

namespace mcsm {

/// Resolves every on-disk location used by the backend.
///
/// Root priority:  --home flag  >  MCSM_HOME env  >  %APPDATA%/McServerManager
class AppPaths
{
public:
    static QString root();
    static void setRootOverride(const QString &root);

    static QString serversDir();
    static QString cacheDir();
    static QString logsDir();
    static QString tmpDir();
    static QString templatesDir();
    static QString registryFile();
    static QString settingsFile();

    static QString serverDir(const QString &serverId);
    static QString serverBackupDir(const QString &serverId);
    static QString serverConfigBackupDir(const QString &serverId);
    static QString serverPluginDir(const QString &serverId);
    static QString serverMetaFile(const QString &serverId);
    static QString serverJarPath(const QString &serverId, const QString &jarName);
    static QString logFilePath();

    static bool ensure(QString *error = nullptr);
    static QString executablePath();

private:
    static QString s_rootOverride;
};

} // namespace mcsm
