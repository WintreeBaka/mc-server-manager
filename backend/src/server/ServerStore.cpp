#include "server/ServerStore.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/StringUtil.h"

namespace mcsm {

ServerStore::ServerStore(const QString &registryPath)
    : m_path(registryPath.isEmpty() ? AppPaths::registryFile() : registryPath)
{
    reload();
}

bool ServerStore::reload()
{
    m_records.clear();
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        Logger::error(QStringLiteral("store"), QStringLiteral("cannot read %1").arg(m_path));
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    QJsonArray array;
    if (doc.isArray())
        array = doc.array();
    else if (doc.isObject())
        array = doc.object().value(QStringLiteral("servers")).toArray();

    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        ServerRecord record = ServerRecord::fromJson(value.toObject());
        if (record.dir.isEmpty())
            record.dir = AppPaths::serverDir(record.id);
        m_records.append(record);
    }
    return true;
}

bool ServerStore::save() const
{
    QJsonArray array;
    for (const ServerRecord &record : m_records)
        array.append(record.toJson());
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("servers"), array);
    QString error;
    if (!Json::writeObjectFile(m_path, root, &error)) {
        Logger::error(QStringLiteral("store"), error);
        return false;
    }
    return true;
}

int ServerStore::indexOf(const QString &id) const
{
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records.at(i).id == id)
            return i;
    }
    return -1;
}

bool ServerStore::contains(const QString &id) const
{
    return indexOf(id) >= 0;
}

ServerRecord ServerStore::get(const QString &id) const
{
    const int index = indexOf(id);
    return index >= 0 ? m_records.at(index) : ServerRecord {};
}

bool ServerStore::add(const ServerRecord &record, QString *error)
{
    QString validation;
    if (!record.isValid(&validation)) {
        if (error)
            *error = validation;
        return false;
    }
    if (contains(record.id)) {
        if (error)
            *error = QStringLiteral("server id '%1' already exists").arg(record.id);
        return false;
    }
    m_records.append(record);
    return save();
}

bool ServerStore::update(const ServerRecord &record, QString *error)
{
    const int index = indexOf(record.id);
    if (index < 0) {
        if (error)
            *error = QStringLiteral("server '%1' not found").arg(record.id);
        return false;
    }
    m_records[index] = record;
    return save();
}

bool ServerStore::remove(const QString &id, QString *error)
{
    const int index = indexOf(id);
    if (index < 0) {
        if (error)
            *error = QStringLiteral("server '%1' not found").arg(id);
        return false;
    }
    m_records.removeAt(index);
    return save();
}

QString ServerStore::allocateId(const QString &name) const
{
    const QString base = StringUtil::slugify(name);
    QString candidate = base;
    int suffix = 2;
    while (contains(candidate)) {
        candidate = QStringLiteral("%1-%2").arg(base).arg(suffix);
        ++suffix;
    }
    return candidate;
}

int ServerStore::allocatePort(int preferred) const
{
    QSet<int> used;
    for (const ServerRecord &record : m_records)
        used.insert(record.port);
    int port = preferred > 0 ? preferred : 25565;
    while (used.contains(port) && port < 65535)
        ++port;
    return port;
}

int ServerStore::allocateRconPort(int preferred) const
{
    QSet<int> used;
    for (const ServerRecord &record : m_records)
        used.insert(record.rconPort);
    int port = preferred > 0 ? preferred : 25575;
    while (used.contains(port) && port < 65535)
        ++port;
    return port;
}

} // namespace mcsm
