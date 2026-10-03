#include "config/ConfigManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include "config/ConfigSchema.h"
#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/StringUtil.h"
#include "server/ServerRecord.h"

namespace mcsm {
namespace {

const char *kIndexFile = "index.json";

bool writeTextLf(const QString &path, const QString &content, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    QByteArray bytes = content.toUtf8();
    bytes.replace("\r\n", "\n");
    file.write(bytes);
    file.close();
    return true;
}

bool isCommentLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    return trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))
           || trimmed.startsWith(QLatin1Char('!'));
}

bool parsePropertyLine(const QString &line, QString *key, QString *value)
{
    if (isCommentLine(line))
        return false;
    static const QRegularExpression re(QStringLiteral("^\\s*([^=:\\s][^=:]*?)\\s*[=:]\\s*(.*)$"));
    const QRegularExpressionMatch match = re.match(line);
    if (!match.hasMatch())
        return false;
    if (key)
        *key = match.captured(1).trimmed();
    if (value)
        *value = match.captured(2).trimmed();
    return true;
}

QJsonObject readIndex(const QString &dir)
{
    return Json::readObjectFile(QDir(dir).filePath(QLatin1String(kIndexFile)));
}

bool writeIndex(const QString &dir, const QJsonObject &object, QString *error)
{
    QDir().mkpath(dir);
    return Json::writeObjectFile(QDir(dir).filePath(QLatin1String(kIndexFile)), object, error);
}

QString timestampId()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
}

} // namespace

QJsonObject ConfigDiffEntry::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("key"), key);
    object.insert(QStringLiteral("before"), before);
    object.insert(QStringLiteral("after"), after);
    return object;
}

QJsonObject ConfigBackup::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("file"), file);
    object.insert(QStringLiteral("note"), note);
    object.insert(QStringLiteral("createdAt"), createdAt);
    object.insert(QStringLiteral("mode"), mode);
    QJsonArray changesArray;
    for (const ConfigDiffEntry &entry : changes)
        changesArray.append(entry.toJson());
    object.insert(QStringLiteral("changes"), changesArray);
    return object;
}

QMap<QString, QString> ConfigManager::readProperties(const QString &path)
{
    QMap<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return values;
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    const QStringList lines = StringUtil::splitLines(text);
    for (const QString &line : lines) {
        QString key;
        QString value;
        if (parsePropertyLine(line, &key, &value))
            values.insert(key, value);
    }
    return values;
}

QMap<QString, QString> ConfigManager::readProperties(const ServerRecord &record)
{
    QMap<QString, QString> values = readProperties(record.propertiesPath());
    for (const ConfigField &field : ConfigSchema::fields()) {
        if (!values.contains(field.key))
            values.insert(field.key, field.defaultValue);
    }
    return values;
}

QString ConfigManager::readRaw(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    return text;
}

QString ConfigManager::readRaw(const ServerRecord &record)
{
    return readRaw(record.propertiesPath());
}

QVector<ConfigDiffEntry> ConfigManager::diff(const QMap<QString, QString> &before,
                                             const QMap<QString, QString> &after)
{
    QVector<ConfigDiffEntry> entries;
    QSet<QString> keys;
    for (auto it = before.constBegin(); it != before.constEnd(); ++it)
        keys.insert(it.key());
    for (auto it = after.constBegin(); it != after.constEnd(); ++it)
        keys.insert(it.key());

    QStringList sorted = keys.values();
    sorted.sort();
    for (const QString &key : sorted) {
        const QString oldValue = before.value(key);
        const QString newValue = after.value(key);
        if (oldValue == newValue)
            continue;
        ConfigDiffEntry entry;
        entry.key = key;
        entry.before = oldValue;
        entry.after = newValue;
        entries.append(entry);
    }
    return entries;
}

QStringList ConfigManager::validate(const QString &text)
{
    QStringList issues;
    const QStringList lines = StringUtil::splitLines(text);
    QSet<QString> seen;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (line.contains(QLatin1Char('\t')))
            issues << QStringLiteral("第 %1 行包含制表符，建议改为空格").arg(i + 1);
        if (isCommentLine(line))
            continue;
        QString key;
        QString value;
        if (!parsePropertyLine(line, &key, &value)) {
            issues << QStringLiteral("第 %1 行不是合法的 key=value").arg(i + 1);
            continue;
        }
        if (seen.contains(key))
            issues << QStringLiteral("第 %1 行重复定义了 %2").arg(i + 1).arg(key);
        seen.insert(key);
        const ConfigField *field = ConfigSchema::find(key);
        if (!field)
            continue;
        if (field->type == ConfigFieldType::Boolean) {
            if (value.compare(QLatin1String("true"), Qt::CaseInsensitive) != 0
                && value.compare(QLatin1String("false"), Qt::CaseInsensitive) != 0) {
                issues << QStringLiteral("%1 需要 true/false，当前为 '%2'").arg(key, value);
            }
        } else if (field->type == ConfigFieldType::Integer) {
            bool ok = false;
            const int number = value.toInt(&ok);
            if (!ok) {
                issues << QStringLiteral("%1 需要整数，当前为 '%2'").arg(key, value);
            } else if (field->max > field->min && (number < field->min || number > field->max)) {
                issues << QStringLiteral("%1 建议范围 %2 ~ %3，当前为 %4")
                              .arg(key)
                              .arg(field->min)
                              .arg(field->max)
                              .arg(number);
            }
        } else if (field->type == ConfigFieldType::Choice && !field->options.isEmpty()) {
            if (!field->options.contains(value))
                issues << QStringLiteral("%1 的值 '%2' 不在推荐列表中").arg(key, value);
        }
    }
    return issues;
}

bool ConfigManager::snapshot(const ServerRecord &record,
                             const QString &previousText,
                             const QString &mode,
                             const QString &note,
                             const QVector<ConfigDiffEntry> &changes,
                             QString *error)
{
    const QString dir = record.configBackupDir();
    QString mkError;
    if (!QDir().mkpath(dir)) {
        if (error)
            *error = mkError.isEmpty() ? QStringLiteral("cannot create %1").arg(dir) : mkError;
        return false;
    }
    if (previousText.isEmpty())
        return true;

    const QString suffix = mode.isEmpty() ? QStringLiteral("config") : mode;
    const QString id = QStringLiteral("%1-%2").arg(timestampId(), StringUtil::slugify(suffix));
    const QString file = QDir(dir).filePath(id + QStringLiteral(".properties"));
    if (!writeTextLf(file, previousText, error))
        return false;

    ConfigBackup backup;
    backup.id = id;
    backup.file = file;
    backup.note = note;
    backup.mode = suffix;
    backup.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    backup.changes = changes;

    QJsonObject index = readIndex(dir);
    QJsonArray entries = index.value(QStringLiteral("entries")).toArray();
    entries.append(backup.toJson());
    index.insert(QStringLiteral("entries"), entries);
    QJsonObject settings = index.value(QStringLiteral("settings")).toObject();
    settings.insert(QStringLiteral("keep"), Json::integer(settings, QStringLiteral("keep"), 50));
    index.insert(QStringLiteral("settings"), settings);
    writeIndex(dir, index, nullptr);
    return true;
}

bool ConfigManager::applyValues(const ServerRecord &record,
                                const QMap<QString, QString> &values,
                                const QString &mode,
                                const QString &note,
                                QVector<ConfigDiffEntry> *diffOut,
                                QString *error)
{
    const QString path = record.propertiesPath();
    QDir().mkpath(record.dir);

    const QMap<QString, QString> before = readProperties(path);
    QStringList lines = StringUtil::splitLines(readRaw(path));
    if (lines.size() == 1 && lines.first().isEmpty())
        lines.clear();

    QMap<QString, QString> remaining = values;
    QStringList output;
    output.reserve(lines.size() + values.size());

    for (const QString &line : lines) {
        QString key;
        QString value;
        if (!parsePropertyLine(line, &key, &value)) {
            output << line;
            continue;
        }
        const QString canonical = ConfigSchema::canonicalKey(key);
        const bool known = values.contains(canonical) || values.contains(key);
        if (known) {
            const QString newValue = values.contains(canonical) ? values.value(canonical)
                                                               : values.value(key);
            output << QStringLiteral("%1=%2").arg(key, newValue);
            remaining.remove(canonical);
            remaining.remove(key);
        } else {
            output << line;
        }
    }

    if (!remaining.isEmpty()) {
        if (!output.isEmpty() && !output.last().trimmed().isEmpty())
            output << QString();
        output << QStringLiteral("# --- managed by McServerManager ---");
        QStringList keys = remaining.keys();
        keys.sort();
        for (const QString &key : keys)
            output << QStringLiteral("%1=%2").arg(key, remaining.value(key));
    }

    // Only keys that were actually submitted count as changes: comparing the
    // whole file against the payload would report every untouched property as
    // "changed to empty".
    QVector<ConfigDiffEntry> changes;
    QStringList changedKeys = values.keys();
    changedKeys.sort();
    for (const QString &key : changedKeys) {
        const QString oldValue = before.value(key);
        const QString newValue = values.value(key);
        if (oldValue == newValue)
            continue;
        ConfigDiffEntry entry;
        entry.key = key;
        entry.before = oldValue;
        entry.after = newValue;
        changes.append(entry);
    }
    if (!changes.isEmpty()) {
        QString backupError;
        if (!snapshot(record, readRaw(path), mode, note, changes, &backupError))
            Logger::warn(QStringLiteral("config"), backupError);
    }

    QString writeError;
    if (!writeTextLf(path, output.join(QLatin1Char('\n')) + QLatin1Char('\n'), &writeError)) {
        if (error)
            *error = writeError;
        return false;
    }
    if (diffOut)
        *diffOut = changes;
    return true;
}

bool ConfigManager::applyRaw(const ServerRecord &record,
                             const QString &text,
                             const QString &note,
                             QVector<ConfigDiffEntry> *diffOut,
                             QString *error)
{
    const QString path = record.propertiesPath();
    const QString previousRaw = readRaw(path);
    const QMap<QString, QString> before = readProperties(path);

    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (!normalized.endsWith(QLatin1Char('\n')))
        normalized.append(QLatin1Char('\n'));

    QMap<QString, QString> after;
    const QStringList lines = StringUtil::splitLines(normalized);
    for (const QString &line : lines) {
        QString key;
        QString value;
        if (parsePropertyLine(line, &key, &value))
            after.insert(key, value);
    }

    const QVector<ConfigDiffEntry> changes = diff(before, after);
    QString backupError;
    if (!snapshot(record, previousRaw, QStringLiteral("expert"),
                  note.isEmpty() ? QStringLiteral("专家模式编辑") : note, changes, &backupError)
        && !previousRaw.isEmpty()) {
        Logger::warn(QStringLiteral("config"), backupError);
    }

    if (!writeTextLf(path, normalized, error))
        return false;
    if (diffOut)
        *diffOut = changes;
    return true;
}

QVector<ConfigBackup> ConfigManager::listBackups(const ServerRecord &record)
{
    QVector<ConfigBackup> backups;
    const QJsonObject index = readIndex(record.configBackupDir());
    const QJsonArray entries = index.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        ConfigBackup backup;
        backup.id = Json::str(object, QStringLiteral("id"));
        backup.file = Json::str(object, QStringLiteral("file"));
        backup.note = Json::str(object, QStringLiteral("note"));
        backup.createdAt = Json::str(object, QStringLiteral("createdAt"));
        backup.mode = Json::str(object, QStringLiteral("mode"));
        const QJsonArray changes = object.value(QStringLiteral("changes")).toArray();
        for (const QJsonValue &change : changes) {
            const QJsonObject entry = change.toObject();
            ConfigDiffEntry diffEntry;
            diffEntry.key = Json::str(entry, QStringLiteral("key"));
            diffEntry.before = Json::str(entry, QStringLiteral("before"));
            diffEntry.after = Json::str(entry, QStringLiteral("after"));
            backup.changes.append(diffEntry);
        }
        if (!QFileInfo::exists(backup.file) && !backup.id.isEmpty()) {
            const QString guess = QDir(record.configBackupDir())
                                      .filePath(backup.id + QStringLiteral(".properties"));
            if (QFileInfo::exists(guess))
                backup.file = guess;
        }
        backups.append(backup);
    }
    return backups;
}

bool ConfigManager::rollback(const ServerRecord &record, const QString &backupId, QString *error)
{
    QString sourceFile;
    const QVector<ConfigBackup> backups = listBackups(record);
    for (const ConfigBackup &backup : backups) {
        if (backup.id == backupId) {
            sourceFile = backup.file;
            break;
        }
    }
    if (sourceFile.isEmpty()) {
        const QString direct = QFileInfo(backupId).isAbsolute()
                                   ? backupId
                                   : QDir(record.configBackupDir())
                                         .filePath(backupId + QStringLiteral(".properties"));
        if (QFileInfo::exists(direct))
            sourceFile = direct;
    }
    if (sourceFile.isEmpty() || !QFileInfo::exists(sourceFile)) {
        if (error)
            *error = QStringLiteral("backup '%1' not found").arg(backupId);
        return false;
    }

    const QString current = readRaw(record.propertiesPath());
    const QString restoreText = readRaw(sourceFile);
    if (!current.isEmpty()) {
        const QString safety = QDir(record.configBackupDir())
                                   .filePath(timestampId() + QStringLiteral("-before-rollback.properties"));
        writeTextLf(safety, current, nullptr);
        QVector<ConfigDiffEntry> changes;
        snapshot(record, current, QStringLiteral("rollback"),
                 QStringLiteral("回滚到 %1").arg(backupId), changes, nullptr);
    }
    return writeTextLf(record.propertiesPath(), restoreText, error);
}

bool ConfigManager::ensureBaseline(const ServerRecord &record, QString *error)
{
    if (!QDir().mkpath(record.configBackupDir())) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(record.configBackupDir());
        return false;
    }
    const QString baseline = QDir(record.configBackupDir()).filePath(QStringLiteral("baseline.properties"));
    if (QFileInfo::exists(baseline))
        return true;
    const QString current = readRaw(record.propertiesPath());
    if (current.isEmpty())
        return true;
    return writeTextLf(baseline, current, error);
}

} // namespace mcsm
