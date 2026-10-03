#include "core/JsonUtil.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QTextStream>

namespace mcsm {

Result Result::ok(const QJsonObject &data)
{
    Result r;
    r.m_ok = true;
    r.m_data = data;
    return r;
}

Result Result::fail(const QString &code, const QString &message, const QString &detail)
{
    Result r;
    r.m_ok = false;
    r.m_code = code;
    r.m_message = message;
    r.m_detail = detail;
    return r;
}

Result &Result::with(const QString &key, const QJsonValue &value)
{
    m_data.insert(key, value);
    return *this;
}

Result &Result::with(const QJsonObject &extra)
{
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        m_data.insert(it.key(), it.value());
    return *this;
}

Result &Result::warn(const QString &message)
{
    if (!message.isEmpty())
        m_warnings.append(message);
    return *this;
}

Result &Result::setDetail(const QString &detail)
{
    m_detail = detail;
    return *this;
}

QJsonObject Result::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("ok"), m_ok);
    // data is always present so that failures can still carry context such as
    // the captured log tail or the affected server id.
    root.insert(QStringLiteral("data"), m_data);
    if (m_ok) {
        root.insert(QStringLiteral("warnings"), Json::toStringArray(m_warnings));
    } else {
        QJsonObject error;
        error.insert(QStringLiteral("code"), m_code);
        error.insert(QStringLiteral("message"), m_message);
        error.insert(QStringLiteral("detail"), m_detail);
        root.insert(QStringLiteral("error"), error);
    }
    return root;
}

QByteArray Result::toBytes(bool pretty) const
{
    const QJsonDocument::JsonFormat format = pretty ? QJsonDocument::Indented : QJsonDocument::Compact;
    return QJsonDocument(toJson()).toJson(format);
}

namespace Json {

QJsonObject readObjectFile(const QString &path, bool *ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return QJsonObject();
    }
    const QByteArray raw = file.readAll();
    file.close();
    return parseObject(raw, ok);
}

bool writeObjectFile(const QString &path, const QJsonObject &object, QString *error)
{
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("cannot open %1 for writing").arg(path);
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("cannot commit %1").arg(path);
        return false;
    }
    return true;
}

QJsonObject parseObject(const QByteArray &bytes, bool *ok)
{
    QJsonParseError parseError {};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok)
            *ok = false;
        return QJsonObject();
    }
    if (ok)
        *ok = true;
    return doc.object();
}

QJsonObject parseObject(const QString &text, bool *ok)
{
    return parseObject(text.toUtf8(), ok);
}

QString stringify(const QJsonObject &object, bool pretty)
{
    const QJsonDocument::JsonFormat format = pretty ? QJsonDocument::Indented : QJsonDocument::Compact;
    return QString::fromUtf8(QJsonDocument(object).toJson(format));
}

QJsonArray toStringArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    return array;
}

QStringList toList(const QJsonValue &value)
{
    QStringList list;
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) {
            if (item.isString())
                list << item.toString();
            else if (item.isDouble())
                list << QString::number(item.toDouble());
        }
    } else if (value.isString()) {
        list << value.toString();
    }
    return list;
}

QJsonObject fromMap(const QMap<QString, QString> &map)
{
    QJsonObject object;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        object.insert(it.key(), it.value());
    return object;
}

QString str(const QJsonObject &obj, const QString &key, const QString &fallback)
{
    const QJsonValue value = obj.value(key);
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toDouble());
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return fallback;
}

int integer(const QJsonObject &obj, const QString &key, int fallback)
{
    const QJsonValue value = obj.value(key);
    if (value.isDouble())
        return value.toInt();
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        if (ok)
            return parsed;
    }
    return fallback;
}

bool boolean(const QJsonObject &obj, const QString &key, bool fallback)
{
    const QJsonValue value = obj.value(key);
    if (value.isBool())
        return value.toBool();
    if (value.isString()) {
        const QString text = value.toString().trimmed().toLower();
        if (text == QLatin1String("true") || text == QLatin1String("1"))
            return true;
        if (text == QLatin1String("false") || text == QLatin1String("0"))
            return false;
    }
    if (value.isDouble())
        return value.toInt() != 0;
    return fallback;
}

qint64 bigint(const QJsonObject &obj, const QString &key, qint64 fallback)
{
    const QJsonValue value = obj.value(key);
    if (value.isDouble())
        return static_cast<qint64>(value.toDouble());
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        if (ok)
            return parsed;
    }
    return fallback;
}

} // namespace Json

} // namespace mcsm
