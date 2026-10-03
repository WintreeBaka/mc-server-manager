#include "cli/ArgParser.h"

#include "core/JsonUtil.h"
#include "core/StringUtil.h"

namespace mcsm {
namespace {

/// Command names of the CLI. A global switch written before the command
/// (`mcsm-cli --pretty versions`) must not swallow the command as its value.
bool isCommandName(const QString &token)
{
    static const QSet<QString> commands = {
        QStringLiteral("help"),     QStringLiteral("doctor"),   QStringLiteral("types"),
        QStringLiteral("versions"), QStringLiteral("java"),     QStringLiteral("install"),
        QStringLiteral("create"),   QStringLiteral("server"),   QStringLiteral("config"),
        QStringLiteral("backup"),   QStringLiteral("plugin"),   QStringLiteral("schedule"),
        QStringLiteral("daemon"),   QStringLiteral("settings"), QStringLiteral("version"),
    };
    return commands.contains(token.toLower());
}

} // namespace

Args::Args(const QStringList &arguments)
    : m_raw(arguments)
{
    for (int i = 0; i < arguments.size(); ++i) {
        const QString token = arguments.at(i);
        if (token == QLatin1String("--")) {
            for (int j = i + 1; j < arguments.size(); ++j)
                m_positionals << arguments.at(j);
            break;
        }
        const bool looksLikeFlag = token.startsWith(QLatin1String("--"))
                                   || (token.startsWith(QLatin1Char('-')) && token.size() > 1
                                       && !token.at(1).isDigit());
        if (looksLikeFlag) {
            QString name = token;
            while (name.startsWith(QLatin1Char('-')))
                name.remove(0, 1);
            QString val;
            const int equals = name.indexOf(QLatin1Char('='));
            if (equals >= 0) {
                val = name.mid(equals + 1);
                name = name.left(equals);
            } else if (i + 1 < arguments.size() && !arguments.at(i + 1).startsWith(QLatin1String("--"))
                       && !(m_positionals.isEmpty() && isCommandName(arguments.at(i + 1)))) {
                val = arguments.at(++i);
            }
            m_options.insert(normalized(name), val);
            continue;
        }
        m_positionals << token;
    }
}

QString Args::normalized(const QString &name)
{
    return name.trimmed().toLower();
}

bool Args::has(const QString &name) const
{
    return m_options.contains(normalized(name));
}

QString Args::value(const QString &name, const QString &fallback) const
{
    const auto it = m_options.constFind(normalized(name));
    if (it == m_options.constEnd() || it.value().isEmpty())
        return fallback;
    return it.value();
}

QStringList Args::list(const QString &name) const
{
    const QString raw = value(name);
    if (raw.isEmpty())
        return QStringList();
    const QStringList parts = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
    QStringList out;
    for (const QString &part : parts)
        out << part.trimmed();
    return out;
}

int Args::intValue(const QString &name, int fallback) const
{
    bool ok = false;
    const int parsed = value(name).toInt(&ok);
    return ok ? parsed : fallback;
}

qint64 Args::bigint(const QString &name, qint64 fallback) const
{
    bool ok = false;
    const qint64 parsed = value(name).toLongLong(&ok);
    return ok ? parsed : fallback;
}

bool Args::boolValue(const QString &name, bool fallback) const
{
    if (!has(name))
        return fallback;
    const QString raw = value(name);
    if (raw.isEmpty())
        return true;
    return StringUtil::toBool(raw, fallback);
}

QJsonObject Args::jsonObject(const QString &name) const
{
    const QString raw = value(name);
    if (raw.trimmed().isEmpty())
        return QJsonObject();
    bool ok = false;
    const QJsonObject object = Json::parseObject(raw, &ok);
    return ok ? object : QJsonObject();
}

QString Args::positional(int index, const QString &fallback) const
{
    if (index < 0 || index >= m_positionals.size())
        return fallback;
    return m_positionals.at(index);
}

} // namespace mcsm
