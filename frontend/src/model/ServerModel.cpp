#include "model/ServerModel.h"

namespace mcsm {

ServerModel::ServerModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ServerModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_servers.size();
}

QVariant ServerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_servers.size())
        return QVariant();
    const ServerInfo &info = m_servers.at(index.row());
    switch (role) {
    case IdRole: return info.id;
    case NameRole: return info.name;
    case StatusRole: return int(info.state());
    case StatusTextRole: return info.statusText();
    case TypeRole: return info.typeLabel();
    case VersionRole: return info.mcVersion;
    case PortRole: return info.port;
    case RunningRole: return info.isRunning();
    case ErrorRole: return info.lastError;
    case InfoRole: return QVariant::fromValue(info);
    case Qt::DisplayRole: return info.name;
    default: break;
    }
    return QVariant();
}

QHash<int, QByteArray> ServerModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {StatusRole, "status"},
        {StatusTextRole, "statusText"},
        {TypeRole, "type"},
        {VersionRole, "version"},
        {PortRole, "port"},
        {RunningRole, "running"},
        {ErrorRole, "error"},
    };
}

void ServerModel::setServers(const QVector<ServerInfo> &servers)
{
    beginResetModel();
    m_servers = servers;
    endResetModel();
    emit changed();
}

void ServerModel::updateServer(const ServerInfo &server)
{
    const int row = rowOf(server.id);
    if (row < 0) {
        beginInsertRows(QModelIndex(), m_servers.size(), m_servers.size());
        m_servers.append(server);
        endInsertRows();
        emit changed();
        return;
    }
    m_servers[row] = server;
    const QModelIndex modelIndex = index(row, 0);
    emit dataChanged(modelIndex, modelIndex);
    emit changed();
}

void ServerModel::removeServer(const QString &id)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_servers.removeAt(row);
    endRemoveRows();
    emit changed();
}

void ServerModel::clear()
{
    beginResetModel();
    m_servers.clear();
    endResetModel();
    emit changed();
}

ServerInfo ServerModel::serverAt(int row) const
{
    if (row < 0 || row >= m_servers.size())
        return ServerInfo {};
    return m_servers.at(row);
}

ServerInfo ServerModel::serverById(const QString &id) const
{
    const int row = rowOf(id);
    return row >= 0 ? m_servers.at(row) : ServerInfo {};
}

int ServerModel::rowOf(const QString &id) const
{
    for (int i = 0; i < m_servers.size(); ++i) {
        if (m_servers.at(i).id == id)
            return i;
    }
    return -1;
}

int ServerModel::runningCount() const
{
    int count = 0;
    for (const ServerInfo &info : m_servers) {
        if (info.isRunning())
            ++count;
    }
    return count;
}

int ServerModel::errorCount() const
{
    int count = 0;
    for (const ServerInfo &info : m_servers) {
        if (info.state() == ServerStatus::Error)
            ++count;
    }
    return count;
}

} // namespace mcsm
