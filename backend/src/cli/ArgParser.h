#pragma once

#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

namespace mcsm {

/// Small flag parser: supports `--key value`, `--key=value`, `-k value` and
/// switches such as `--follow` that take no value.
class Args
{
public:
    Args() = default;
    explicit Args(const QStringList &arguments);

    bool has(const QString &name) const;
    QString value(const QString &name, const QString &fallback = QString()) const;
    QStringList list(const QString &name) const;
    int intValue(const QString &name, int fallback = 0) const;
    qint64 bigint(const QString &name, qint64 fallback = 0) const;
    bool boolValue(const QString &name, bool fallback = false) const;
    QJsonObject jsonObject(const QString &name) const;

    QString positional(int index, const QString &fallback = QString()) const;
    int positionalCount() const { return m_positionals.size(); }
    QStringList positionals() const { return m_positionals; }
    QStringList raw() const { return m_raw; }
    QString subcommand() const { return positional(0); }

private:
    static QString normalized(const QString &name);

    QMap<QString, QString> m_options;
    QStringList m_positionals;
    QStringList m_raw;
};

} // namespace mcsm
