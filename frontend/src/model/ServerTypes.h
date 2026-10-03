#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace mcsm {

enum class ServerStatus { Stopped, Starting, Running, Stopping, Error, Unknown };

struct ServerInfo
{
    QString id;
    QString name;
    QString type;
    QString mcVersion;
    QString build;
    QString image;
    QString memory;
    QString memoryLimit;
    QString levelName;
    QString motd;
    QString status;
    QString containerState;
    QString lastError;
    QString dir;
    QString jarName;
    int javaMajor = 17;
    int port = 25565;
    int rconPort = 25575;
    bool eulaAccepted = false;
    bool autoRestart = true;
    bool autoStart = false;
    bool manualInstall = false;
    QDateTime createdAt;
    QDateTime lastStartAt;
    QDateTime lastBackupAt;
    QJsonObject backupSchedule;
    QJsonObject raw;

    static ServerInfo fromJson(const QJsonObject &object);

    ServerStatus state() const;
    bool isRunning() const { return state() == ServerStatus::Running; }
    bool isBusy() const;
    QString statusText() const;
    QString typeLabel() const;
    QString memoryText() const;
    QString javaLabel() const;
    QString summary() const;
};

} // namespace mcsm

Q_DECLARE_METATYPE(mcsm::ServerInfo)
