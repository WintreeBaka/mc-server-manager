#include "server/RconClient.h"

#include <QDataStream>
#include <QElapsedTimer>

namespace mcsm {
namespace {

constexpr int kMaxPacketSize = 4096 * 8;

} // namespace

RconClient::RconClient(const QString &host, quint16 port, const QString &password, int timeoutMs)
    : m_host(host.isEmpty() ? QStringLiteral("127.0.0.1") : host)
    , m_port(port)
    , m_password(password)
    , m_timeoutMs(timeoutMs)
{
}

QByteArray RconClient::buildPacket(int id, int type, const QByteArray &body)
{
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << qint32(body.size() + 10);
    stream << qint32(id);
    stream << qint32(type);
    stream.writeRawData(body.constData(), body.size());
    stream.writeRawData("\0\0", 2);
    return packet;
}

bool RconClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

void RconClient::disconnect()
{
    m_authenticated = false;
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.disconnectFromHost();
        if (m_socket.state() != QAbstractSocket::UnconnectedState)
            m_socket.abort();
    }
}

bool RconClient::sendPacket(int id, int type, const QByteArray &body, QString *error)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState) {
        if (error)
            *error = QStringLiteral("socket not connected");
        return false;
    }
    const QByteArray packet = buildPacket(id, type, body);
    if (m_socket.write(packet) != packet.size()) {
        if (error)
            *error = QStringLiteral("short write to rcon socket");
        return false;
    }
    if (!m_socket.waitForBytesWritten(m_timeoutMs)) {
        if (error)
            *error = QStringLiteral("timeout while writing to rcon");
        return false;
    }
    return true;
}

bool RconClient::readPacket(int *id, int *type, QByteArray *body, QString *error)
{
    QElapsedTimer timer;
    timer.start();
    // waitForReadyRead() only reports *newly* arrived data, therefore it may
    // only be called while the buffer really lacks the bytes we need.
    while (m_socket.bytesAvailable() < 4) {
        const int remaining = m_timeoutMs - int(timer.elapsed());
        if (remaining <= 0 || !m_socket.waitForReadyRead(qMax(50, remaining))) {
            if (error)
                *error = QStringLiteral("timeout while waiting for the rcon packet length");
            return false;
        }
    }

    QByteArray header = m_socket.read(4);
    QDataStream headerStream(&header, QIODevice::ReadOnly);
    headerStream.setByteOrder(QDataStream::LittleEndian);
    qint32 length = 0;
    headerStream >> length;
    if (length < 10 || length > kMaxPacketSize) {
        if (error)
            *error = QStringLiteral("invalid rcon packet length %1").arg(length);
        return false;
    }

    // The length field already counts id + type + body + two NUL bytes.
    const int payloadSize = length;
    while (m_socket.bytesAvailable() < payloadSize) {
        const int remaining = m_timeoutMs - int(timer.elapsed());
        if (remaining <= 0 || !m_socket.waitForReadyRead(qMax(50, remaining))) {
            if (error)
                *error = QStringLiteral("timeout while reading the rcon payload");
            return false;
        }
    }
    const QByteArray payload = m_socket.read(payloadSize);

    QDataStream payloadStream(payload);
    payloadStream.setByteOrder(QDataStream::LittleEndian);
    qint32 responseId = 0;
    qint32 responseType = 0;
    payloadStream >> responseId >> responseType;
    const QByteArray content = payload.mid(8, payload.size() - 10);
    if (id)
        *id = responseId;
    if (type)
        *type = responseType;
    if (body)
        *body = content;
    return true;
}

bool RconClient::authenticate(QString *error)
{
    m_authenticated = false;
    if (m_socket.state() == QAbstractSocket::ConnectedState)
        disconnect();

    m_socket.connectToHost(m_host, m_port);
    if (!m_socket.waitForConnected(m_timeoutMs)) {
        m_lastError = QStringLiteral("无法连接 RCON %1:%2 (%3)")
                          .arg(m_host)
                          .arg(m_port)
                          .arg(m_socket.errorString());
        if (error)
            *error = m_lastError;
        return false;
    }

    QString sendError;
    if (!sendPacket(m_requestId, Auth, m_password.toUtf8(), &sendError)) {
        m_lastError = sendError;
        if (error)
            *error = sendError;
        return false;
    }

    int id = 0;
    int type = 0;
    QByteArray body;
    QString readError;
    if (!readPacket(&id, &type, &body, &readError)) {
        m_lastError = readError;
        if (error)
            *error = readError;
        return false;
    }
    if (id == -1) {
        m_lastError = QStringLiteral("RCON 密码错误");
        if (error)
            *error = m_lastError;
        return false;
    }
    m_authenticated = true;
    return true;
}

bool RconClient::sendCommand(const QString &command, QString *response, QString *error)
{
    if (!m_authenticated) {
        QString authError;
        if (!authenticate(&authError)) {
            if (error)
                *error = authError;
            return false;
        }
    }
    if (!sendPacket(m_requestId++, Command, command.toUtf8(), error))
        return false;

    int id = 0;
    int type = 0;
    QByteArray body;
    if (!readPacket(&id, &type, &body, error))
        return false;
    if (response)
        *response = QString::fromUtf8(body);
    return true;
}

} // namespace mcsm
