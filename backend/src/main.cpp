#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>

#include "cli/ArgParser.h"
#include "cli/CommandRouter.h"
#include "core/AppPaths.h"
#include "core/JsonUtil.h"
#include "core/Logger.h"
#include "core/StringUtil.h"
#include "docker/DockerManager.h"
#include "schedule/Scheduler.h"

using namespace mcsm;

namespace {

void emitLine(const QJsonObject &object)
{
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + "\n";
    fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    fflush(stdout);
}

void emitResult(const Result &result, bool pretty)
{
    const QByteArray bytes = result.toBytes(pretty);
    fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

/// `server logs --follow` streams newline delimited JSON so the GUI can append
/// console output live without buffering the whole log.
int streamContainerLogs(const QString &serverId, int tail)
{
    const QString docker = DockerManager::program();
    if (docker.isEmpty()) {
        emitResult(Result::fail(QStringLiteral("DOCKER_UNAVAILABLE"),
                                QStringLiteral("未找到 docker 命令")), false);
        return 3;
    }

    QStringList args {QStringLiteral("logs"), QStringLiteral("--follow")};
    if (tail > 0)
        args << QStringLiteral("--tail") << QString::number(tail);
    args << DockerManager::containerName(serverId);

    QProcess process;
    process.setProgram(docker);
    process.setArguments(args);
    process.setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(&process, &QProcess::readyReadStandardOutput, [&process]() {
        const QByteArray chunk = process.readAllStandardOutput();
        const QStringList lines = StringUtil::splitLines(QString::fromUtf8(chunk));
        for (const QString &line : lines) {
            if (line.isEmpty())
                continue;
            QJsonObject object;
            object.insert(QStringLiteral("type"), QStringLiteral("log"));
            object.insert(QStringLiteral("line"), StringUtil::stripAnsi(line));
            emitLine(object);
        }
    });

    QObject::connect(&process, &QProcess::errorOccurred, [&process](QProcess::ProcessError error) {
        QJsonObject object;
        object.insert(QStringLiteral("type"), QStringLiteral("error"));
        object.insert(QStringLiteral("message"), process.errorString());
        object.insert(QStringLiteral("code"), int(error));
        emitLine(object);
    });

    QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     [](int code, QProcess::ExitStatus) {
                         QJsonObject object;
                         object.insert(QStringLiteral("type"), QStringLiteral("end"));
                         object.insert(QStringLiteral("code"), code);
                         emitLine(object);
                         QCoreApplication::quit();
                     });

    process.start();
    if (!process.waitForStarted(15000)) {
        emitResult(Result::fail(QStringLiteral("DOCKER_UNAVAILABLE"),
                                QStringLiteral("无法启动 docker logs 进程"), process.errorString()),
                   false);
        return 3;
    }
    return QCoreApplication::exec();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("mcsm-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("McServerManager"));

    // Global flags are parsed manually so that command specific flags can be
    // passed through untouched.
    Args args(QCoreApplication::arguments().mid(1));
    if (args.has(QStringLiteral("home"))) {
        AppPaths::setRootOverride(args.value(QStringLiteral("home")));
    }
    Logger::init(args.boolValue(QStringLiteral("verbose"), false),
                 args.boolValue(QStringLiteral("quiet"), false));

    QString ensureError;
    if (!AppPaths::ensure(&ensureError)) {
        emitResult(Result::fail(QStringLiteral("IO_ERROR"),
                                QStringLiteral("无法初始化数据目录"), ensureError),
                   true);
        return 2;
    }
    Logger::setCommandContext(args.positionals().join(QLatin1Char(' ')));

    CommandRouter router(args.raw());

    // long running helpers
    if (args.positional(0) == QLatin1String("daemon")) {
        return Scheduler::runDaemon(args.intValue(QStringLiteral("interval"), 60),
                                    args.boolValue(QStringLiteral("once"), false),
                                    args.boolValue(QStringLiteral("verbose"), false));
    }
    if (args.positional(0) == QLatin1String("server") && args.positional(1) == QLatin1String("logs")
        && args.boolValue(QStringLiteral("follow"), false)) {
        return streamContainerLogs(args.value(QStringLiteral("id"), args.positional(2)),
                                   args.intValue(QStringLiteral("tail"), 200));
    }

    const Result result = router.run();
    emitResult(result, args.boolValue(QStringLiteral("pretty"), false));
    return result.isOk() ? 0 : 1;
}
