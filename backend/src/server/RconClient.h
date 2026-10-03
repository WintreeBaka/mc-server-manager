#pragma once

#include <QString>
#include <QTcpSocket>

namespace mcsm {

/// Minimal Source-RCON client (TCP, little endian packets).
///
/// Console commands are routed through RCON instead of `docker attach`, which
/// keeps the container console usable while the manager sends commands.
class RconClient
{
public:
    RconClient(const QString &host, quint16 port, const QString &password, int timeoutMs = 6000);

    bool authenticate(QString *error = nullptr);
    bool sendCommand(const QString &command, QString *response, QString *error = nullptr);
    bool isConnected() const;
    void disconnect();

    QString lastError() const { return m_lastError; }

private:
    enum PacketType { Response = 0, Command = 2, Auth = 3 };

    bool sendPacket(int id, int type, const QByteArray &body, QString *error);
    bool readPacket(int *id, int *type, QByteArray *body, QString *error);
    static QByteArray buildPacket(int id, int type, const QByteArray &body);

    QTcpSocket m_socket;
    QString m_host;
    quint16 m_port = 0;
    QString m_password;
    int m_timeoutMs = 6000;
    int m_requestId = 1;
    bool m_authenticated = false;
    QString m_lastError;
};

} // namespace mcsm
