#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "model/ServerTypes.h"

namespace mcsm {

class ServerModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        StatusRole,
        StatusTextRole,
        TypeRole,
        VersionRole,
        PortRole,
        RunningRole,
        ErrorRole,
        InfoRole,
    };

    explicit ServerModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setServers(const QVector<ServerInfo> &servers);
    void updateServer(const ServerInfo &server);
    void removeServer(const QString &id);
    void clear();

    QVector<ServerInfo> servers() const { return m_servers; }
    ServerInfo serverAt(int row) const;
    ServerInfo serverById(const QString &id) const;
    int rowOf(const QString &id) const;
    int runningCount() const;
    int errorCount() const;

signals:
    void changed();

private:
    QVector<ServerInfo> m_servers;
};

} // namespace mcsm
