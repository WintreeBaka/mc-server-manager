#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mcsm {

enum class ConfigFieldType { Text, Integer, Boolean, Choice, Password, Decimal };

struct ConfigField
{
    QString key;
    QString label;
    QString group;
    QString hint;
    QString defaultValue;
    QString unit;
    ConfigFieldType type = ConfigFieldType::Text;
    int min = 0;
    int max = 0;
    bool advanced = false;
    bool restartRequired = true;
    QStringList options;

    QJsonObject toJson() const;
};

/// Field catalogue for server.properties, used by the GUI quick-config mode.
class ConfigSchema
{
public:
    static const QVector<ConfigField> &fields();
    static QJsonArray toJson();
    static QStringList groups();
    static const ConfigField *find(const QString &key);
    static QString canonicalKey(const QString &key);
};

} // namespace mcsm
