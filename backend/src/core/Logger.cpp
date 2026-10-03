#include "core/Logger.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include "core/AppPaths.h"

namespace mcsm {
namespace {

QMutex g_mutex;
bool g_verbose = false;
bool g_quiet = false;
QString g_context;
QString g_lastError;

QString levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug: return QStringLiteral("debug");
    case LogLevel::Info:  return QStringLiteral("info");
    case LogLevel::Warn:  return QStringLiteral("warn");
    case LogLevel::Error: return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

} // namespace

void Logger::init(bool verbose, bool quiet)
{
    QMutexLocker locker(&g_mutex);
    g_verbose = verbose;
    g_quiet = quiet;
}

void Logger::setCommandContext(const QString &context)
{
    QMutexLocker locker(&g_mutex);
    g_context = context;
}

void Logger::log(LogLevel level, const QString &scope, const QString &message)
{
    QMutexLocker locker(&g_mutex);

    if (level == LogLevel::Debug && !g_verbose)
        return;
    if (level == LogLevel::Error)
        g_lastError = message;

    const QString stamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString line = QStringLiteral("[%1] %2 %3: %4")
                       .arg(stamp, levelName(level).toUpper().leftJustified(5), scope, message);
    if (!g_context.isEmpty())
        line = QStringLiteral("%1 (%2)").arg(line, g_context);

    const bool toStderr = (level == LogLevel::Error || level == LogLevel::Warn || g_verbose) && !g_quiet;
    if (toStderr) {
        const QByteArray utf8 = line.toUtf8();
        fputs(utf8.constData(), stderr);
        fputc('\n', stderr);
        fflush(stderr);
    }

    const QString path = AppPaths::logFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;
    file.write(line.toUtf8());
    file.write("\n");
    file.close();

    if (file.size() > 8LL * 1024 * 1024) {
        QFile::remove(path + QStringLiteral(".1"));
        QFile::rename(path, path + QStringLiteral(".1"));
    }
}

void Logger::debug(const QString &scope, const QString &message) { log(LogLevel::Debug, scope, message); }
void Logger::info(const QString &scope, const QString &message) { log(LogLevel::Info, scope, message); }
void Logger::warn(const QString &scope, const QString &message) { log(LogLevel::Warn, scope, message); }
void Logger::error(const QString &scope, const QString &message) { log(LogLevel::Error, scope, message); }

QString Logger::lastError()
{
    QMutexLocker locker(&g_mutex);
    return g_lastError;
}

} // namespace mcsm
