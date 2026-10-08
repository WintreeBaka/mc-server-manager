#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace mcsm {

QString AppPaths::s_rootOverride;

void AppPaths::setRootOverride(const QString &root)
{
    s_rootOverride = root.trimmed();
}

QString AppPaths::root()
{
    // Always absolute: every stored path (registry entries, backups, generated
    // scripts) must stay valid no matter which directory a later call runs in.
    if (!s_rootOverride.isEmpty())
        return QDir::cleanPath(QFileInfo(s_rootOverride).absoluteFilePath());

    const QByteArray env = qgetenv("MCSM_HOME");
    if (!env.isEmpty())
        return QDir::cleanPath(QFileInfo(QString::fromLocal8Bit(env)).absoluteFilePath());

    // GenericDataLocation is %APPDATA% on Windows, ~/.local/share on Linux and
    // ~/Library/Application Support on macOS; AppDataLocation would additionally
    // append the organisation and application name.
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.mcservermanager");
    return QDir::cleanPath(base + QStringLiteral("/McServerManager"));
}

QString AppPaths::serversDir() { return root() + QStringLiteral("/servers"); }
QString AppPaths::cacheDir() { return root() + QStringLiteral("/cache"); }
QString AppPaths::logsDir() { return root() + QStringLiteral("/logs"); }
QString AppPaths::tmpDir() { return root() + QStringLiteral("/tmp"); }
QString AppPaths::templatesDir() { return root() + QStringLiteral("/templates"); }
QString AppPaths::pluginsDir() { return root() + QStringLiteral("/plugins"); }
QString AppPaths::pluginRegistryFile() { return pluginsDir() + QStringLiteral("/registry.json"); }
QString AppPaths::pluginDir(const QString &pluginId)
{
    return pluginsDir() + QLatin1Char('/') + pluginId;
}
QString AppPaths::pluginDataDir(const QString &pluginId)
{
    return pluginsDir() + QStringLiteral("/.data/") + pluginId;
}
QString AppPaths::registryFile() { return root() + QStringLiteral("/servers.json"); }
QString AppPaths::settingsFile() { return root() + QStringLiteral("/settings.json"); }

QString AppPaths::serverDir(const QString &serverId)
{
    return serversDir() + QLatin1Char('/') + serverId;
}

QString AppPaths::serverBackupDir(const QString &serverId)
{
    return serverDir(serverId) + QStringLiteral("/backups");
}

QString AppPaths::serverConfigBackupDir(const QString &serverId)
{
    return serverDir(serverId) + QStringLiteral("/config-backups");
}

QString AppPaths::serverPluginDir(const QString &serverId)
{
    return serverDir(serverId) + QStringLiteral("/plugins");
}

QString AppPaths::serverRuntimeDir(const QString &serverId)
{
    return serverDir(serverId) + QStringLiteral("/.mcsm");
}

QString AppPaths::serverStartPatchFile(const QString &serverId)
{
    return serverRuntimeDir(serverId) + QStringLiteral("/start-patch.json");
}

QString AppPaths::serverMetaFile(const QString &serverId)
{
    return serverDir(serverId) + QStringLiteral("/server.json");
}

QString AppPaths::serverJarPath(const QString &serverId, const QString &jarName)
{
    return serverDir(serverId) + QLatin1Char('/') + jarName;
}

QString AppPaths::logFilePath()
{
    return logsDir() + QStringLiteral("/backend.log");
}

bool AppPaths::ensure(QString *error)
{
    const QStringList dirs = {
        root(), serversDir(), cacheDir(), logsDir(), tmpDir(), templatesDir(), pluginsDir()
    };
    for (const QString &dir : dirs) {
        if (!QDir().mkpath(dir)) {
            if (error)
                *error = QStringLiteral("cannot create directory: %1").arg(dir);
            return false;
        }
    }
    return true;
}

QString AppPaths::executablePath()
{
    return QCoreApplication::applicationFilePath();
}

} // namespace mcsm
