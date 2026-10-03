#include "model/ServerTypes.h"

#include <QJsonArray>

namespace mcsm {
namespace {

QString string(const QJsonObject &object, const QString &key, const QString &fallback = QString())
{
    const QJsonValue value = object.value(key);
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toDouble());
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return fallback;
}

int integer(const QJsonObject &object, const QString &key, int fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble())
        return value.toInt();
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

bool boolean(const QJsonObject &object, const QString &key, bool fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isBool())
        return value.toBool();
    if (value.isString()) {
        const QString text = value.toString().trimmed().toLower();
        if (text == QLatin1String("true") || text == QLatin1String("1"))
            return true;
        if (text == QLatin1String("false") || text == QLatin1String("0"))
            return false;
    }
    return fallback;
}

} // namespace

ServerInfo ServerInfo::fromJson(const QJsonObject &object)
{
    ServerInfo info;
    info.raw = object;
    info.id = string(object, QStringLiteral("id"));
    info.name = string(object, QStringLiteral("name"), info.id);
    info.type = string(object, QStringLiteral("type"), QStringLiteral("paper"));
    info.mcVersion = string(object, QStringLiteral("mcVersion"));
    info.build = string(object, QStringLiteral("build"));
    info.image = string(object, QStringLiteral("image"));
    info.memory = string(object, QStringLiteral("memory"), QStringLiteral("4G"));
    info.memoryLimit = string(object, QStringLiteral("memoryLimit"));
    info.levelName = string(object, QStringLiteral("levelName"), QStringLiteral("world"));
    info.motd = string(object, QStringLiteral("motd"));
    info.status = string(object, QStringLiteral("status"), QStringLiteral("stopped"));
    info.containerState = string(object, QStringLiteral("containerState"));
    info.lastError = string(object, QStringLiteral("lastError"));
    info.dir = string(object, QStringLiteral("dir"));
    info.jarName = string(object, QStringLiteral("jarName"), QStringLiteral("server.jar"));
    info.javaMajor = integer(object, QStringLiteral("javaMajor"), 17);
    info.port = integer(object, QStringLiteral("port"), 25565);
    info.rconPort = integer(object, QStringLiteral("rconPort"), 25575);
    info.eulaAccepted = boolean(object, QStringLiteral("eulaAccepted"), false);
    info.autoRestart = boolean(object, QStringLiteral("autoRestart"), true);
    info.autoStart = boolean(object, QStringLiteral("autoStart"), false);
    info.manualInstall = boolean(object, QStringLiteral("manualInstall"), false);
    info.createdAt = QDateTime::fromString(string(object, QStringLiteral("createdAt")), Qt::ISODate);
    info.lastStartAt = QDateTime::fromString(string(object, QStringLiteral("lastStartAt")), Qt::ISODate);
    info.lastBackupAt = QDateTime::fromString(string(object, QStringLiteral("lastBackupAt")), Qt::ISODate);
    info.backupSchedule = object.value(QStringLiteral("backupSchedule")).toObject();
    return info;
}

ServerStatus ServerInfo::state() const
{
    const QString value = status.toLower();
    if (value == QLatin1String("running"))
        return ServerStatus::Running;
    if (value == QLatin1String("starting") || value == QLatin1String("restarting"))
        return ServerStatus::Starting;
    if (value == QLatin1String("stopping"))
        return ServerStatus::Stopping;
    if (value == QLatin1String("error"))
        return ServerStatus::Error;
    if (value == QLatin1String("stopped"))
        return ServerStatus::Stopped;
    return ServerStatus::Unknown;
}

bool ServerInfo::isBusy() const
{
    const ServerStatus current = state();
    return current == ServerStatus::Starting || current == ServerStatus::Stopping;
}

QString ServerInfo::statusText() const
{
    switch (state()) {
    case ServerStatus::Running: return QStringLiteral("运行中");
    case ServerStatus::Starting: return QStringLiteral("启动中");
    case ServerStatus::Stopping: return QStringLiteral("停止中");
    case ServerStatus::Error: return QStringLiteral("异常");
    case ServerStatus::Stopped: return QStringLiteral("已停止");
    case ServerStatus::Unknown: break;
    }
    return QStringLiteral("未知");
}

QString ServerInfo::typeLabel() const
{
    const QString value = type.toLower();
    if (value == QLatin1String("paper"))
        return QStringLiteral("Paper");
    if (value == QLatin1String("purpur"))
        return QStringLiteral("Purpur");
    if (value == QLatin1String("fabric"))
        return QStringLiteral("Fabric");
    if (value == QLatin1String("vanilla"))
        return QStringLiteral("原版");
    return type;
}

QString ServerInfo::memoryText() const
{
    return memory.isEmpty() ? QStringLiteral("4G") : memory;
}

QString ServerInfo::javaLabel() const
{
    return QStringLiteral("JDK %1").arg(javaMajor);
}

QString ServerInfo::summary() const
{
    return QStringLiteral("%1 %2 · 端口 %3").arg(typeLabel(), mcVersion).arg(port);
}

} // namespace mcsm
