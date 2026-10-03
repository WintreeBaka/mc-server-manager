#pragma once

#include <QString>
#include <QVector>

#include "core/JsonUtil.h"
#include "server/ServerRecord.h"

namespace mcsm {

/// Registry of all managed servers (servers.json inside the data root).
class ServerStore
{
public:
    explicit ServerStore(const QString &registryPath = QString());

    QVector<ServerRecord> all() const { return m_records; }
    bool reload();
    bool save() const;

    bool contains(const QString &id) const;
    ServerRecord get(const QString &id) const;
    bool add(const ServerRecord &record, QString *error = nullptr);
    bool update(const ServerRecord &record, QString *error = nullptr);
    bool remove(const QString &id, QString *error = nullptr);
    int indexOf(const QString &id) const;

    QString allocateId(const QString &name) const;
    int allocatePort(int preferred = 25565) const;
    int allocateRconPort(int preferred = 25575) const;

    const QString &registryPath() const { return m_path; }

private:
    QString m_path;
    QVector<ServerRecord> m_records;
};

} // namespace mcsm
