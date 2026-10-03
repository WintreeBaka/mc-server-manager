#include "core/ProcessRunner.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

#include "core/Logger.h"
#include "core/StringUtil.h"

namespace mcsm {

QString ProcessResult::combined() const
{
    QString text = stdOut;
    if (!stdErr.isEmpty()) {
        if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n')))
            text.append(QLatin1Char('\n'));
        text.append(stdErr);
    }
    return text;
}

QString ProcessResult::errorText() const
{
    if (!started)
        return QStringLiteral("cannot start '%1'").arg(program);
    if (timedOut)
        return QStringLiteral("'%1 %2' timed out").arg(program, StringUtil::joinArgs(arguments));
    QString text = stdErr.trimmed();
    if (text.isEmpty())
        text = stdOut.trimmed();
    if (text.isEmpty())
        text = QStringLiteral("exit code %1").arg(exitCode);
    return StringUtil::ellipsize(text, 800);
}

ProcessResult ProcessRunner::run(const QString &program,
                                 const QStringList &arguments,
                                 int timeoutMs,
                                 const QByteArray &stdinData,
                                 const QString &workingDirectory)
{
    ProcessResult result;
    result.program = program;
    result.arguments = arguments;

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    if (!workingDirectory.isEmpty())
        process.setWorkingDirectory(workingDirectory);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    Logger::debug(QStringLiteral("process"),
                  QStringLiteral("exec: %1 %2 (cwd=%3)")
                      .arg(program, StringUtil::joinArgs(arguments),
                           workingDirectory.isEmpty() ? QDir::currentPath() : workingDirectory));

    process.start();
    if (!process.waitForStarted(15000)) {
        result.started = false;
        Logger::warn(QStringLiteral("process"),
                     QStringLiteral("failed to start %1: %2").arg(program, process.errorString()));
        return result;
    }
    result.started = true;

    if (!stdinData.isEmpty()) {
        process.write(stdinData);
        process.closeWriteChannel();
    }

    if (!process.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        process.kill();
        process.waitForFinished(3000);
    }

    result.exitCode = process.exitCode();
    result.stdOut = QString::fromUtf8(process.readAllStandardOutput());
    result.stdErr = QString::fromUtf8(process.readAllStandardError());
    return result;
}

QProcess *ProcessRunner::startAsync(const QString &program,
                                    const QStringList &arguments,
                                    QObject *parent,
                                    bool mergeChannels)
{
    auto *process = new QProcess(parent);
    process->setProgram(program);
    process->setArguments(arguments);
    process->setProcessChannelMode(mergeChannels ? QProcess::MergedChannels : QProcess::SeparateChannels);
    process->start();
    return process;
}

QProcess *ProcessRunner::startDetached(const QString &program,
                                       const QStringList &arguments,
                                       const QString &workingDirectory)
{
    if (startDetachedOk(program, arguments, workingDirectory))
        return nullptr;
    return nullptr;
}

bool ProcessRunner::startDetachedOk(const QString &program,
                                    const QStringList &arguments,
                                    const QString &workingDirectory,
                                    qint64 *pid)
{
    qint64 childPid = 0;
    const bool started = QProcess::startDetached(program, arguments, workingDirectory, &childPid);
    if (pid)
        *pid = childPid;
    if (!started) {
        Logger::warn(QStringLiteral("process"),
                     QStringLiteral("failed to start detached: %1").arg(program));
    }
    return started;
}

bool ProcessRunner::programExists(const QString &program)
{
    if (program.isEmpty())
        return false;
    if (QFileInfo(program).isAbsolute())
        return QFileInfo::exists(program);
#ifdef Q_OS_WIN
    const QStringList extensions = QStringList {QStringLiteral(".exe"), QStringLiteral(".cmd"),
                                                 QStringLiteral(".bat"), QString()};
    const QStringList dirs = qEnvironmentVariable("PATH").split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &dir : dirs) {
        for (const QString &ext : extensions) {
            const QString candidate = QDir(dir).filePath(program + ext);
            if (QFileInfo::exists(candidate))
                return true;
        }
    }
    return false;
#else
    return !QStandardPaths::findExecutable(program).isEmpty();
#endif
}

QString ProcessRunner::resolveProgram(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (programExists(candidate))
            return candidate;
    }
    return QString();
}

QString ProcessRunner::toNativePath(const QString &path)
{
#ifdef Q_OS_WIN
    return QDir::toNativeSeparators(path);
#else
    return path;
#endif
}

QByteArray ProcessRunner::toDockerPath(const QString &path)
{
    // Docker Desktop on Windows accepts "C:/dir/sub" style paths for bind mounts.
    QString converted = path;
    converted.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return converted.toLocal8Bit();
}

} // namespace mcsm
