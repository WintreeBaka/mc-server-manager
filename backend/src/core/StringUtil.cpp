#include "core/StringUtil.h"

#include <QRandomGenerator>
#include <QRegularExpression>

namespace mcsm {

QString StringUtil::slugify(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar &ch : text) {
        if (ch.isLetterOrNumber()) {
            out.append(ch.toLower());
        } else if (ch == QLatin1Char('-') || ch == QLatin1Char('_') || ch == QLatin1Char(' ')) {
            if (!out.endsWith(QLatin1Char('-')))
                out.append(QLatin1Char('-'));
        }
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    if (out.isEmpty())
        out = QStringLiteral("server");
    return out;
}

QString StringUtil::humanBytes(qint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1).arg(QLatin1String(units[unit]));
}

QString StringUtil::trimmed(const QString &s)
{
    return s.trimmed();
}

bool StringUtil::toBool(const QString &value, bool fallback)
{
    const QString v = value.trimmed().toLower();
    if (v == QLatin1String("true") || v == QLatin1String("1") || v == QLatin1String("yes")
        || v == QLatin1String("on") || v == QLatin1String("enable") || v == QLatin1String("enabled")) {
        return true;
    }
    if (v == QLatin1String("false") || v == QLatin1String("0") || v == QLatin1String("no")
        || v == QLatin1String("off") || v == QLatin1String("disable") || v == QLatin1String("disabled")) {
        return false;
    }
    return fallback;
}

QString StringUtil::yesNo(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QStringList StringUtil::splitLines(const QString &text)
{
    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return normalized.split(QLatin1Char('\n'));
}

QString StringUtil::tailLines(const QString &text, int maxLines)
{
    if (maxLines <= 0)
        return text;
    const QStringList lines = splitLines(text);
    if (lines.size() <= maxLines)
        return text;
    return lines.mid(lines.size() - maxLines).join(QLatin1Char('\n'));
}

QString StringUtil::stripAnsi(const QString &text)
{
    static const QRegularExpression ansi(QStringLiteral("\x1B\\[[0-9;?]*[ -/]*[@-~]"));
    QString copy = text;
    copy.remove(ansi);
    return copy;
}

QString StringUtil::ellipsize(const QString &text, int maxChars)
{
    if (text.size() <= maxChars)
        return text;
    return text.left(qMax(0, maxChars - 1)) + QChar(0x2026);
}

QString StringUtil::randomToken(int length)
{
    static const QString alphabet = QStringLiteral("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    QString out;
    out.reserve(length);
    for (int i = 0; i < length; ++i)
        out.append(alphabet.at(QRandomGenerator::global()->bounded(alphabet.size())));
    return out;
}

QString StringUtil::joinArgs(const QStringList &args)
{
    QStringList quoted;
    for (const QString &arg : args) {
        if (arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('"')))
            quoted << QStringLiteral("\"%1\"").arg(QString(arg).replace(QLatin1Char('"'), QLatin1String("\\\"")));
        else
            quoted << arg;
    }
    return quoted.join(QLatin1Char(' '));
}

} // namespace mcsm
