#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace mcsm {

/// Persisted description of one managed Minecraft server.
struct ServerRecord
{
    QString id;
    QString name;
    QString type = QStringLiteral("paper");   // vanilla | paper | purpur | fabric
    QString mcVersion;
    QString build;
    QString jarName = QStringLiteral("server.jar");
    QString jarUrl;
    int javaMajor = 17;
    QString image;                            // eclipse-temurin:17-jre
    /// "docker" (default: JDK lives in a container image) or "host" (a JDK
    /// cloned from this machine into <dir>/jdk - experimental feature).
    QString runtime = QStringLiteral("docker");
    QString jdkHome;                          // cloned JDK path when runtime=host
    QString memory;                           // heap size, e.g. 4G
    QString memoryLimit;                      // container limit, e.g. 5g
    QString extraJavaOptions;
    int port = 25565;
    int rconPort = 25575;
    QString rconPassword;
    QString motd;
    QString levelName = QStringLiteral("world");
    QString dir;
    bool eulaAccepted = false;
    bool autoRestart = true;
    bool autoStart = false;
    bool manualInstall = false;               // jar supplied by the user
    QString status = QStringLiteral("stopped");
    QString lastError;
    QDateTime createdAt;
    QDateTime lastStartAt;
    QDateTime lastBackupAt;
    QJsonObject backupSchedule;

    ServerRecord();

    static ServerRecord fromJson(const QJsonObject &object);
    QJsonObject toJson() const;

    QString containerName() const;
    QString containerMemoryLimit() const;
    QString effectiveJavaOptions() const;
    QString jarPath() const;
    QString propertiesPath() const;
    QString backupDir() const;
    QString configBackupDir() const;
    QString pluginDir() const;
    QString primaryEntryPoint() const;
    bool usesHostJdk() const;
    QString javaExecutable() const;
    QString hostConsoleLog() const;
    QString hostPidFile() const;
    bool isValid(QString *error = nullptr) const;

    static QJsonObject defaultBackupSchedule();
};

} // namespace mcsm
