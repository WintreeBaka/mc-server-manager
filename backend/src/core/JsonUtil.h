#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace mcsm {

/// Uniform result envelope shared with the desktop frontend.
///
/// stdout payload:
///   {"ok":true,"data":{...},"warnings":[...]}
///   {"ok":false,"error":{"code":"...","message":"...","detail":"..."}}
class Result
{
public:
    static Result ok(const QJsonObject &data = QJsonObject());
    static Result fail(const QString &code, const QString &message, const QString &detail = QString());

    bool isOk() const { return m_ok; }
    Result &with(const QString &key, const QJsonValue &value);
    Result &with(const QJsonObject &extra);
    Result &warn(const QString &message);
    Result &setDetail(const QString &detail);

    QJsonObject data() const { return m_data; }
    QString errorCode() const { return m_code; }
    QString errorMessage() const { return m_message; }
    QString errorDetail() const { return m_detail; }
    QStringList warnings() const { return m_warnings; }

    QJsonObject toJson() const;
    QByteArray toBytes(bool pretty) const;

private:
    bool m_ok = true;
    QJsonObject m_data;
    QStringList m_warnings;
    QString m_code;
    QString m_message;
    QString m_detail;
};

namespace Json {

QJsonObject readObjectFile(const QString &path, bool *ok = nullptr);
bool writeObjectFile(const QString &path, const QJsonObject &object, QString *error = nullptr);
QJsonObject parseObject(const QByteArray &bytes, bool *ok = nullptr);
QJsonObject parseObject(const QString &text, bool *ok = nullptr);
QString stringify(const QJsonObject &object, bool pretty = true);
QJsonArray toStringArray(const QStringList &values);
QStringList toList(const QJsonValue &value);
QJsonObject fromMap(const QMap<QString, QString> &map);

QString str(const QJsonObject &obj, const QString &key, const QString &fallback = QString());
int integer(const QJsonObject &obj, const QString &key, int fallback = 0);
bool boolean(const QJsonObject &obj, const QString &key, bool fallback = false);
qint64 bigint(const QJsonObject &obj, const QString &key, qint64 fallback = 0);

} // namespace Json

} // namespace mcsm
