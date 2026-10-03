#pragma once

#include <QString>

namespace mcsm {

enum class LogLevel { Debug, Info, Warn, Error };

/// Tiny append-only logger. Writes JSON lines to <root>/logs/backend.log and
/// mirrors to stderr unless disabled (the CLI prints machine readable JSON on
/// stdout, therefore diagnostics must never pollute stdout).
class Logger
{
public:
    static void init(bool verbose, bool quiet);
    static void log(LogLevel level, const QString &scope, const QString &message);

    static void debug(const QString &scope, const QString &message);
    static void info(const QString &scope, const QString &message);
    static void warn(const QString &scope, const QString &message);
    static void error(const QString &scope, const QString &message);

    static QString lastError();
    static void setCommandContext(const QString &context);
};

} // namespace mcsm
