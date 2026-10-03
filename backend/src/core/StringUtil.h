#pragma once

#include <QString>
#include <QStringList>
#include <QMap>

namespace mcsm {

class StringUtil
{
public:
    static QString slugify(const QString &text);
    static QString humanBytes(qint64 bytes);
    static QString trimmed(const QString &s);
    static bool toBool(const QString &value, bool fallback = false);
    static QString yesNo(bool value);
    static QStringList splitLines(const QString &text);
    static QString tailLines(const QString &text, int maxLines);
    static QString stripAnsi(const QString &text);
    static QString ellipsize(const QString &text, int maxChars);
    static QString randomToken(int length);
    static QString joinArgs(const QStringList &args);
};

} // namespace mcsm
