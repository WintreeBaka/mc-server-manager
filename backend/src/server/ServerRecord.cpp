#include "server/ServerRecord.h"

#include <QDir>
#include <QJsonArray>
#include <QStringList>

#include "core/AppPaths.h"
#include "core/JsonUtil.h"
#include "core/StringUtil.h"
#include "docker/DockerManager.h"
#include "net/VersionResolver.h"

namespace mcsm {

ServerRecord::ServerRecord()
{
    memory = QStringLiteral("4G");
    memoryLimit = QStringLiteral("5g");
    rconPassword = StringUtil::randomToken(20);
    createdAt = QDateTime::currentDateTime();
    backupSchedule = defaultBackupSchedule();
}

QJsonObject ServerRecord::defaultBackupSchedule()
{
    QJsonObject object;
    object.insert(QStringLiteral("enabled"), false);
    object.insert(QStringLiteral("intervalMinutes"), 180);
    object.insert(QStringLiteral("keep"), 10);
    object.insert(QStringLiteral("includePlugins"), true);
    object.insert(QStringLiteral("saveBeforeBackup"), true);
    return object;
}

ServerRecord ServerRecord::fromJson(const QJsonObject &object)
{
    ServerRecord record;
    record.id = Json::str(object, QStringLiteral("id"));
    record.name = Json::str(object, QStringLiteral("name"), record.id);
    record.type = Json::str(object, QStringLiteral("type"), QStringLiteral("paper"));
    record.mcVersion = Json::str(object, QStringLiteral("mcVersion"));
    record.build = Json::str(object, QStringLiteral("build"));
    record.jarName = Json::str(object, QStringLiteral("jarName"), QStringLiteral("server.jar"));
    record.jarUrl = Json::str(object, QStringLiteral("jarUrl"));
    record.javaMajor = Json::integer(object, QStringLiteral("javaMajor"), 17);
    record.image = Json::str(object, QStringLiteral("image"), VersionResolver::javaImage(record.javaMajor));
    record.runtime = Json::str(object, QStringLiteral("runtime"), QStringLiteral("docker"));
    record.jdkHome = Json::str(object, QStringLiteral("jdkHome"));
    record.memory = Json::str(object, QStringLiteral("memory"), QStringLiteral("4G"));
    record.memoryLimit = Json::str(object, QStringLiteral("memoryLimit"), QStringLiteral("5g"));
    record.extraJavaOptions = Json::str(object, QStringLiteral("extraJavaOptions"));
    record.port = Json::integer(object, QStringLiteral("port"), 25565);
    record.rconPort = Json::integer(object, QStringLiteral("rconPort"), 25575);
    record.rconPassword = Json::str(object, QStringLiteral("rconPassword"));
    if (record.rconPassword.isEmpty())
        record.rconPassword = StringUtil::randomToken(20);
    record.motd = Json::str(object, QStringLiteral("motd"));
    record.levelName = Json::str(object, QStringLiteral("levelName"), QStringLiteral("world"));
    record.dir = Json::str(object, QStringLiteral("dir"), AppPaths::serverDir(record.id));
    record.eulaAccepted = Json::boolean(object, QStringLiteral("eulaAccepted"), false);
    record.autoRestart = Json::boolean(object, QStringLiteral("autoRestart"), true);
    record.autoStart = Json::boolean(object, QStringLiteral("autoStart"), false);
    record.manualInstall = Json::boolean(object, QStringLiteral("manualInstall"), false);
    record.status = Json::str(object, QStringLiteral("status"), QStringLiteral("stopped"));
    record.lastError = Json::str(object, QStringLiteral("lastError"));
    record.createdAt = QDateTime::fromString(Json::str(object, QStringLiteral("createdAt")), Qt::ISODate);
    record.lastStartAt = QDateTime::fromString(Json::str(object, QStringLiteral("lastStartAt")), Qt::ISODate);
    record.lastBackupAt = QDateTime::fromString(Json::str(object, QStringLiteral("lastBackupAt")), Qt::ISODate);
    const QJsonValue schedule = object.value(QStringLiteral("backupSchedule"));
    record.backupSchedule = schedule.isObject() ? schedule.toObject() : defaultBackupSchedule();
    return record;
}

QJsonObject ServerRecord::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("mcVersion"), mcVersion);
    object.insert(QStringLiteral("build"), build);
    object.insert(QStringLiteral("jarName"), jarName);
    object.insert(QStringLiteral("jarUrl"), jarUrl);
    object.insert(QStringLiteral("javaMajor"), javaMajor);
    object.insert(QStringLiteral("image"), image);
    object.insert(QStringLiteral("runtime"), runtime);
    object.insert(QStringLiteral("jdkHome"), jdkHome);
    object.insert(QStringLiteral("memory"), memory);
    object.insert(QStringLiteral("memoryLimit"), memoryLimit);
    object.insert(QStringLiteral("extraJavaOptions"), extraJavaOptions);
    object.insert(QStringLiteral("port"), port);
    object.insert(QStringLiteral("rconPort"), rconPort);
    object.insert(QStringLiteral("rconPassword"), rconPassword);
    object.insert(QStringLiteral("motd"), motd);
    object.insert(QStringLiteral("levelName"), levelName);
    object.insert(QStringLiteral("dir"), dir);
    object.insert(QStringLiteral("eulaAccepted"), eulaAccepted);
    object.insert(QStringLiteral("autoRestart"), autoRestart);
    object.insert(QStringLiteral("autoStart"), autoStart);
    object.insert(QStringLiteral("manualInstall"), manualInstall);
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("lastError"), lastError);
    object.insert(QStringLiteral("createdAt"), createdAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("lastStartAt"), lastStartAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("lastBackupAt"), lastBackupAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("backupSchedule"), backupSchedule);
    return object;
}

QString ServerRecord::containerName() const
{
    return DockerManager::containerName(id);
}

QString ServerRecord::containerMemoryLimit() const
{
    return memoryLimit.trimmed().isEmpty() ? memory : memoryLimit.trimmed();
}

QString ServerRecord::effectiveJavaOptions() const
{
    QStringList parts;
    if (!extraJavaOptions.trimmed().isEmpty())
        parts << extraJavaOptions.trimmed();
    return parts.join(QLatin1Char(' '));
}

QString ServerRecord::jarPath() const
{
    return QDir(dir).filePath(jarName.isEmpty() ? QStringLiteral("server.jar") : jarName);
}

QString ServerRecord::propertiesPath() const
{
    return QDir(dir).filePath(QStringLiteral("server.properties"));
}

QString ServerRecord::backupDir() const
{
    return AppPaths::serverBackupDir(id);
}

QString ServerRecord::configBackupDir() const
{
    return AppPaths::serverConfigBackupDir(id);
}

QString ServerRecord::pluginDir() const
{
    return AppPaths::serverPluginDir(id);
}

QString ServerRecord::primaryEntryPoint() const
{
    return QStringLiteral("/data/start.sh");
}

bool ServerRecord::usesHostJdk() const
{
    return runtime.compare(QLatin1String("host"), Qt::CaseInsensitive) == 0;
}

QString ServerRecord::javaExecutable() const
{
    const QString home = jdkHome.isEmpty() ? QDir(dir).filePath(QStringLiteral("jdk")) : jdkHome;
#ifdef Q_OS_WIN
    return QDir(home).filePath(QStringLiteral("bin/java.exe"));
#else
    return QDir(home).filePath(QStringLiteral("bin/java"));
#endif
}

QString ServerRecord::hostConsoleLog() const
{
    return QDir(dir).filePath(QStringLiteral("logs/console.log"));
}

QString ServerRecord::hostPidFile() const
{
    return QDir(dir).filePath(QStringLiteral(".mcsm.pid"));
}

bool ServerRecord::isValid(QString *error) const
{
    if (id.isEmpty()) {
        if (error)
            *error = QStringLiteral("id is required");
        return false;
    }
    if (name.isEmpty()) {
        if (error)
            *error = QStringLiteral("name is required");
        return false;
    }
    if (port <= 0 || port > 65535) {
        if (error)
            *error = QStringLiteral("port %1 is out of range").arg(port);
        return false;
    }
    if (dir.isEmpty()) {
        if (error)
            *error = QStringLiteral("dir is required");
        return false;
    }
    return true;
}

} // namespace mcsm
