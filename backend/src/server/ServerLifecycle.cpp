#include "server/ServerLifecycle.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>

#include "config/ConfigManager.h"
#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/ProcessRunner.h"
#include "core/StringUtil.h"
#include "docker/DockerManager.h"
#include "docker/TemplateWriter.h"
#include "server/RconClient.h"

namespace mcsm {
namespace {

const QString kReadyMarker = QStringLiteral("Done (");

QString tailSince(const QString &text, int maxLines)
{
    return StringUtil::tailLines(StringUtil::stripAnsi(text), maxLines);
}

bool looksLikeReady(const QString &logText)
{
    return logText.contains(kReadyMarker);
}

bool processRunning(qint64 pid)
{
    if (pid <= 0)
        return false;
#ifdef Q_OS_WIN
    const ProcessResult result = ProcessRunner::run(
        QStringLiteral("tasklist"),
        {QStringLiteral("/FI"), QStringLiteral("PID eq %1").arg(pid), QStringLiteral("/NH")}, 20000);
    return result.stdOut.contains(QString::number(pid));
#else
    return QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid));
#endif
}

/// Cheap liveness probe that does not depend on pid bookkeeping: if the RCON
/// port accepts a connection the server process is definitely alive.
bool portAcceptsConnection(quint16 port)
{
    if (port == 0)
        return false;
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), port);
    const bool connected = socket.waitForConnected(400);
    socket.abort();
    return connected;
}

qint64 readPidFile(const ServerRecord &record)
{
    QFile file(record.hostPidFile());
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    bool ok = false;
    const qint64 pid = QString::fromLatin1(file.readAll()).trimmed().toLongLong(&ok);
    file.close();
    return ok ? pid : 0;
}

void writePidFile(const ServerRecord &record, qint64 pid)
{
    QFile file(record.hostPidFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QByteArray::number(pid));
        file.close();
    }
}

void clearPidFile(const ServerRecord &record)
{
    QFile::remove(record.hostPidFile());
}

/// The launcher (cmd.exe) can be killed while the JVM survives as an orphan, so
/// stopping also looks for java processes whose command line points at this
/// server directory.
QList<qint64> orphanJavaPids(const ServerRecord &record)
{
    QList<qint64> pids;
#ifdef Q_OS_WIN
    const QString script = QStringLiteral(
        "Get-CimInstance Win32_Process -Filter \"Name='java.exe'\" | "
        "Where-Object { $_.CommandLine -like '*%1*' } | "
        "ForEach-Object { $_.ProcessId }")
                               .arg(QDir::toNativeSeparators(record.dir));
    const ProcessResult result = ProcessRunner::run(
        QStringLiteral("powershell"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-Command"), script}, 30000);
    const QStringList lines = StringUtil::splitLines(result.stdOut);
    for (const QString &line : lines) {
        bool ok = false;
        const qint64 pid = line.trimmed().toLongLong(&ok);
        if (ok && pid > 0)
            pids << pid;
    }
#else
    Q_UNUSED(record);
#endif
    return pids;
}

void forceKill(qint64 pid)
{
#ifdef Q_OS_WIN
    ProcessRunner::run(QStringLiteral("taskkill"),
                       {QStringLiteral("/PID"), QString::number(pid), QStringLiteral("/T"),
                        QStringLiteral("/F")},
                       60000);
#else
    ProcessRunner::run(QStringLiteral("kill"), {QStringLiteral("-9"), QString::number(pid)}, 30000);
#endif
}

QString readHostLog(const ServerRecord &record, int lines)
{
    QFile file(record.hostConsoleLog());
    if (!file.exists())
        return QString();
    // reading the tail is enough: consoles can grow fast while loading
    const qint64 maxBytes = 512 * 1024;
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const qint64 size = file.size();
    if (size > maxBytes)
        file.seek(size - maxBytes);
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    return lines > 0 ? StringUtil::tailLines(text, lines) : text;
}

} // namespace

Result StartOutcome::toResult() const
{
    QJsonObject object;
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("message"), message);
    object.insert(QStringLiteral("hint"), hint);
    object.insert(QStringLiteral("elapsedMs"), double(elapsedMs));
    object.insert(QStringLiteral("failedMarker"), failedMarker);
    object.insert(QStringLiteral("logTail"), QJsonArray::fromStringList(logTail));
    object.insert(QStringLiteral("dockerUnavailable"), dockerUnavailable);
    if (ok)
        return Result::ok(object);
    const QString code = dockerUnavailable ? QStringLiteral("DOCKER_UNAVAILABLE")
                                           : QStringLiteral("SERVER_START_FAILED");
    // The detail carries every clue the UI needs: the friendly hint first, then
    // the raw container output.
    QStringList details;
    if (!hint.isEmpty())
        details << hint;
    if (!logTail.isEmpty())
        details << logTail.join(QLatin1Char('\n'));
    Result result = Result::fail(code, message, details.join(QLatin1Char('\n')));
    result.with(object);
    return result;
}

QString ServerLifecycle::detectFailure(const QString &logText)
{
    static const QVector<QPair<QRegularExpression, QString>> patterns = {
        {QRegularExpression(QStringLiteral("UnsupportedClassVersionError")),
         QStringLiteral("JDK 版本过低，请把运行环境切换到更高的 Java 版本")},
        {QRegularExpression(QStringLiteral("Unable to access jarfile"), QRegularExpression::CaseInsensitiveOption),
         QStringLiteral("找不到服务端 jar，请重新执行安装或手动指定文件")},
        {QRegularExpression(QStringLiteral("You need to agree to the EULA"), QRegularExpression::CaseInsensitiveOption),
         QStringLiteral("尚未同意 Minecraft EULA，请在服务器设置中勾选并保存")},
        {QRegularExpression(QStringLiteral("Failed to bind to port|Address already in use"),
                            QRegularExpression::CaseInsensitiveOption),
         QStringLiteral("端口被占用，请更换端口或关闭占用该端口的程序")},
        {QRegularExpression(QStringLiteral("Permission denied"), QRegularExpression::CaseInsensitiveOption),
         QStringLiteral("容器内权限不足，请检查挂载目录的可写权限")},
        {QRegularExpression(QStringLiteral("OutOfMemoryError|Java heap space")),
         QStringLiteral("内存不足，请提高最大内存或降低容器内存上限")},
        {QRegularExpression(QStringLiteral("Invalid or corrupt jarfile|"
                                           "Unable to access jarfile|"
                                           "Could not find or load main class|"
                                           "no main manifest attribute"),
                            QRegularExpression::CaseInsensitiveOption),
         QStringLiteral("服务端文件损坏或不是可执行服务端，请重新下载")},
        {QRegularExpression(QStringLiteral("A JNI error has occurred|"
                                           "Unsupported class file major version")),
         QStringLiteral("JVM 与 jar 不兼容，请更换 JDK 版本")},
        {QRegularExpression(QStringLiteral("UnknownHostException|Connection refused:|"
                                           "java.net.SocketException")),
         QStringLiteral("网络异常，容器无法访问外部服务")},
        {QRegularExpression(QStringLiteral("Failed to load eula|eula.txt")),
         QStringLiteral("eula.txt 缺失或格式错误，请在设置中重新生成")},
    };

    for (const auto &entry : patterns) {
        if (entry.first.match(logText).hasMatch())
            return entry.second;
    }
    return QString();
}

QString ServerLifecycle::runtimeState(const ServerRecord &record)
{
    if (record.usesHostJdk()) {
        const qint64 pid = readPidFile(record);
        if (processRunning(pid) || portAcceptsConnection(quint16(record.rconPort)))
            return QStringLiteral("running");
        return QStringLiteral("stopped");
    }
    const QString state = DockerManager::containerState(record.id);
    if (state == QLatin1String("running"))
        return QStringLiteral("running");
    if (state == QLatin1String("restarting"))
        return QStringLiteral("restarting");
    if (state == QLatin1String("created") || state == QLatin1String("exited")
        || state == QLatin1String("dead") || state == QLatin1String("paused")) {
        return QStringLiteral("stopped");
    }
    return QStringLiteral("stopped");
}

QJsonObject ServerLifecycle::status(const ServerRecord &record, bool withStats)
{
    QJsonObject object = record.toJson();
    const QString state = runtimeState(record);
    object.insert(QStringLiteral("status"), state);
    object.insert(QStringLiteral("containerState"), DockerManager::containerState(record.id));
    object.insert(QStringLiteral("containerName"), record.containerName());
    if (withStats && state == QLatin1String("running"))
        object.insert(QStringLiteral("stats"), DockerManager::stats(record.id));
    return object;
}

QStringList ServerLifecycle::logTail(const ServerRecord &record, int lines)
{
    const QString text = record.usesHostJdk() ? readHostLog(record, lines)
                                              : DockerManager::logs(record.id, lines);
    if (text.trimmed().isEmpty())
        return QStringList();
    return StringUtil::splitLines(text);
}

StartOutcome ServerLifecycle::start(ServerStore &store, const QString &id, int waitSeconds)
{
    StartOutcome outcome;
    QElapsedTimer timer;
    timer.start();

    ServerRecord record = store.get(id);
    if (record.id.isEmpty()) {
        outcome.status = QStringLiteral("error");
        outcome.message = QStringLiteral("服务器 %1 不存在").arg(id);
        return outcome;
    }
    Logger::info(QStringLiteral("lifecycle"), QStringLiteral("starting %1").arg(record.id));

    const bool hostRuntime = record.usesHostJdk();
    if (!hostRuntime) {
        const DockerStatus docker = DockerManager::status();
        if (!docker.cliFound || !docker.daemonRunning) {
            outcome.dockerUnavailable = true;
            outcome.status = QStringLiteral("error");
            outcome.message = QStringLiteral("Docker 未运行，无法启动服务器");
            outcome.hint = docker.error.isEmpty() ? QStringLiteral("请先启动 Docker Desktop")
                                                  : docker.error;
            record.status = QStringLiteral("error");
            record.lastError = outcome.message;
            store.update(record);
            return outcome;
        }
    }

    if (!QDir(record.dir).exists()) {
        outcome.status = QStringLiteral("error");
        outcome.message = QStringLiteral("服务器目录不存在：%1").arg(record.dir);
        record.status = QStringLiteral("error");
        record.lastError = outcome.message;
        store.update(record);
        return outcome;
    }
    if (!QFileInfo::exists(record.jarPath())) {
        outcome.status = QStringLiteral("error");
        outcome.message = QStringLiteral("缺少服务端文件 %1").arg(record.jarName);
        outcome.hint = QStringLiteral("请在服务器详情中重新执行安装，或手动放入服务端 jar");
        record.status = QStringLiteral("error");
        record.lastError = outcome.message;
        store.update(record);
        return outcome;
    }
    if (!record.eulaAccepted) {
        outcome.status = QStringLiteral("error");
        outcome.message = QStringLiteral("尚未同意 Minecraft EULA");
        outcome.hint = QStringLiteral("勾选 EULA 选项后重试");
        record.status = QStringLiteral("error");
        record.lastError = outcome.message;
        store.update(record);
        return outcome;
    }

    // keep generated files in sync with the record
    TemplateWriter::writeEula(record, true, nullptr);
    if (hostRuntime) {
        TemplateWriter::writeHostStartScript(record, nullptr);
    } else {
        TemplateWriter::writeStartScript(record, nullptr);
        TemplateWriter::writeComposeFile(record, nullptr);
    }
    if (!QFileInfo::exists(record.propertiesPath()))
        TemplateWriter::writeDefaultProperties(record, true);
    ConfigManager::ensureBaseline(record, nullptr);

    // ---------------- experimental: run on a JDK cloned from this machine ----
    if (hostRuntime) {
        if (!QFileInfo::exists(record.javaExecutable())) {
            outcome.status = QStringLiteral("error");
            outcome.message = QStringLiteral("克隆的 JDK 不存在：%1").arg(record.javaExecutable());
            outcome.hint = QStringLiteral("请重新安装该服务器，或改回容器运行方式");
            record.status = QStringLiteral("error");
            record.lastError = outcome.message;
            store.update(record);
            return outcome;
        }

        const qint64 stale = readPidFile(record);
        if (stale > 0 && processRunning(stale)) {
            outcome.ok = true;
            outcome.status = QStringLiteral("running");
            outcome.message = QStringLiteral("服务器已在运行");
            return outcome;
        }
        clearPidFile(record);
        record.status = QStringLiteral("starting");
        record.lastError.clear();
        store.update(record);

#ifdef Q_OS_WIN
        const QString program = QStringLiteral("cmd.exe");
        // Redirect the launcher's own stdio: otherwise the detached cmd inherits
        // our stdout pipe and whoever called us waits for the server to exit.
        const QStringList arguments = {
            QStringLiteral("/c"),
            TemplateWriter::hostStartScriptName() + QStringLiteral(" < nul > nul 2>&1"),
        };
#else
        const QString program = QStringLiteral("/bin/sh");
        const QStringList arguments = {TemplateWriter::hostStartScriptName()};
#endif
        qint64 pid = 0;
        if (!ProcessRunner::startDetachedOk(program, arguments, record.dir, &pid)) {
            outcome.status = QStringLiteral("error");
            outcome.message = QStringLiteral("无法启动服务器进程");
            record.status = QStringLiteral("error");
            record.lastError = outcome.message;
            store.update(record);
            return outcome;
        }
        writePidFile(record, pid);

        const int deadline = qMax(5, waitSeconds);
        int waited = 0;
        QString lastLog;
        while (waited < deadline) {
            QThread::msleep(1000);
            ++waited;
            // scan the whole tail buffer for the banner: a chatty console can push
            // the "Done (" line out of a short tail window
            lastLog = readHostLog(record, 0);
            if (looksLikeReady(lastLog)) {
                outcome.ok = true;
                outcome.status = QStringLiteral("running");
                outcome.message = QStringLiteral("服务器已启动");
                outcome.elapsedMs = timer.elapsed();
                outcome.logTail = StringUtil::splitLines(tailSince(lastLog, 60));
                record.status = QStringLiteral("running");
                record.lastError.clear();
                record.lastStartAt = QDateTime::currentDateTime();
                store.update(record);
                return outcome;
            }
            const QString failure = detectFailure(lastLog);
            if (!failure.isEmpty()) {
                outcome.status = QStringLiteral("error");
                outcome.message = failure;
                outcome.failedMarker = failure;
                outcome.elapsedMs = timer.elapsed();
                outcome.logTail = StringUtil::splitLines(tailSince(lastLog, 80));
                record.status = QStringLiteral("error");
                record.lastError = failure;
                store.update(record);
                return outcome;
            }
            if (!processRunning(pid)) {
                outcome.status = QStringLiteral("error");
                outcome.message = detectFailure(lastLog).isEmpty()
                                      ? QStringLiteral("服务器进程已退出，请查看日志输出")
                                      : detectFailure(lastLog);
                outcome.elapsedMs = timer.elapsed();
                outcome.logTail = StringUtil::splitLines(tailSince(lastLog, 80));
                clearPidFile(record);
                record.status = QStringLiteral("error");
                record.lastError = outcome.message;
                store.update(record);
                return outcome;
            }
        }
        outcome.ok = true;
        outcome.status = QStringLiteral("running");
        outcome.message = QStringLiteral("进程已启动，但尚未检测到启动完成标记");
        outcome.hint = QStringLiteral("服务器可能仍在生成世界，可稍后在控制台查看进度");
        outcome.elapsedMs = timer.elapsed();
        outcome.logTail = StringUtil::splitLines(tailSince(readHostLog(record, 0), 60));
        record.status = QStringLiteral("running");
        record.lastStartAt = QDateTime::currentDateTime();
        store.update(record);
        return outcome;
    }

    QString imageError;
    if (!DockerManager::ensureImage(record.image, &imageError)) {
        outcome.status = QStringLiteral("error");
        outcome.message = QStringLiteral("JDK 镜像不可用：%1").arg(record.image);
        outcome.hint = imageError;
        record.status = QStringLiteral("error");
        record.lastError = outcome.message;
        store.update(record);
        return outcome;
    }

    record.status = QStringLiteral("starting");
    record.lastError.clear();
    store.update(record);

    QString runError;
    const ProcessResult runResult = DockerManager::runContainer(record, record.image,
                                                               record.primaryEntryPoint(), &runError);
    if (!runResult.ok()) {
        outcome.status = QStringLiteral("error");
        outcome.message = QStringLiteral("容器启动失败");
        outcome.hint = runError;
        outcome.logTail = logTail(record, 80);
        record.status = QStringLiteral("error");
        record.lastError = runError;
        store.update(record);
        return outcome;
    }

    // wait for the "Done (" banner, surfacing failures immediately
    const int deadline = qMax(5, waitSeconds);
    int waited = 0;
    QString lastLog;
    while (waited < deadline) {
        QThread::msleep(1000);
        ++waited;
        lastLog = DockerManager::logs(record.id, 200);
        if (looksLikeReady(lastLog)) {
            outcome.ok = true;
            outcome.status = QStringLiteral("running");
            outcome.message = QStringLiteral("服务器已启动");
            outcome.elapsedMs = timer.elapsed();
            outcome.logTail = StringUtil::splitLines(tailSince(lastLog, 60));
            record.status = QStringLiteral("running");
            record.lastError.clear();
            record.lastStartAt = QDateTime::currentDateTime();
            store.update(record);
            return outcome;
        }
        const QString failure = detectFailure(lastLog);
        if (!failure.isEmpty()) {
            DockerManager::stopContainer(record.id, 5, true);
            outcome.status = QStringLiteral("error");
            outcome.message = failure;
            outcome.failedMarker = failure;
            outcome.elapsedMs = timer.elapsed();
            outcome.logTail = StringUtil::splitLines(tailSince(lastLog, 80));
            record.status = QStringLiteral("error");
            record.lastError = failure;
            store.update(record);
            return outcome;
        }
        // A crashing server is kept alive by the container restart policy, so a
        // "restarting" state is just as fatal as an exited container.
        const QString state = DockerManager::containerState(record.id);
        if (state == QLatin1String("exited") || state == QLatin1String("restarting")
            || state == QLatin1String("dead")) {
            const QString text = DockerManager::logs(record.id, 200);
            const QString reason = detectFailure(text);
            DockerManager::removeContainer(record.id, true);
            outcome.status = QStringLiteral("error");
            outcome.message = reason.isEmpty()
                                  ? QStringLiteral("容器%s，请查看日志输出")
                                        .arg(state == QLatin1String("restarting")
                                                 ? QStringLiteral("反复重启")
                                                 : QStringLiteral("已退出"))
                                  : reason;
            outcome.failedMarker = reason;
            outcome.elapsedMs = timer.elapsed();
            outcome.logTail = StringUtil::splitLines(tailSince(text, 80));
            record.status = QStringLiteral("error");
            record.lastError = outcome.message;
            store.update(record);
            return outcome;
        }
    }

    // Still running but no readiness banner: only report success when the
    // container is genuinely healthy, otherwise surface the log.
    const QString finalState = DockerManager::containerState(record.id);
    if (finalState != QLatin1String("running")) {
        const QString text = DockerManager::logs(record.id, 200);
        const QString reason = detectFailure(text);
        DockerManager::removeContainer(record.id, true);
        outcome.status = QStringLiteral("error");
        outcome.message = reason.isEmpty() ? QStringLiteral("容器未处于运行状态（%1）").arg(finalState)
                                           : reason;
        outcome.failedMarker = reason;
        outcome.elapsedMs = timer.elapsed();
        outcome.logTail = StringUtil::splitLines(tailSince(text, 80));
        record.status = QStringLiteral("error");
        record.lastError = outcome.message;
        store.update(record);
        return outcome;
    }

    outcome.ok = true;
    outcome.status = QStringLiteral("running");
    outcome.message = QStringLiteral("容器已启动，但尚未检测到启动完成标记");
    outcome.hint = QStringLiteral("服务器可能仍在加载区块或下载依赖，可稍后在控制台查看进度");
    outcome.elapsedMs = timer.elapsed();
    outcome.logTail = StringUtil::splitLines(tailSince(DockerManager::logs(record.id, 80), 60));
    record.status = QStringLiteral("running");
    record.lastStartAt = QDateTime::currentDateTime();
    store.update(record);
    return outcome;
}

Result ServerLifecycle::stop(ServerStore &store, const QString &id, bool force)
{
    ServerRecord record = store.get(id);
    if (record.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("服务器 %1 不存在").arg(id));

    // experimental host runtime: stop the cloned-JDK process instead of a container
    if (record.usesHostJdk()) {
        const qint64 pid = readPidFile(record);
        if (pid <= 0 || !processRunning(pid)) {
            clearPidFile(record);
            record.status = QStringLiteral("stopped");
            store.update(record);
            return Result::ok(QJsonObject {{QStringLiteral("status"), QStringLiteral("stopped")},
                                           {QStringLiteral("note"), QStringLiteral("进程未在运行")}});
        }

        QString note;
        if (!force) {
            RconClient rcon(QStringLiteral("127.0.0.1"), quint16(record.rconPort), record.rconPassword, 4000);
            QString response;
            QString rconError;
            if (rcon.sendCommand(QStringLiteral("save-all flush"), &response, &rconError)
                && rcon.sendCommand(QStringLiteral("stop"), &response, &rconError)) {
                note = QStringLiteral("已通过 RCON 请求停服");
            } else {
                note = QStringLiteral("RCON 不可用，直接结束进程");
            }
        }

        record.status = QStringLiteral("stopping");
        store.update(record);

        bool exited = false;
        for (int second = 0; second < (force ? 2 : 45); ++second) {
            if (!processRunning(pid)) {
                exited = true;
                break;
            }
            QThread::msleep(1000);
        }
        if (!exited) {
            for (const qint64 orphan : orphanJavaPids(record))
                forceKill(orphan);
            forceKill(pid);
        }
        // clean up anything the launcher left behind
        for (const qint64 orphan : orphanJavaPids(record))
            forceKill(orphan);
        clearPidFile(record);
        record.status = QStringLiteral("stopped");
        store.update(record);
        return Result::ok(QJsonObject {{QStringLiteral("status"), QStringLiteral("stopped")},
                                       {QStringLiteral("note"), note}});
    }

    const QString state = DockerManager::containerState(id);
    if (state == QLatin1String("missing")) {
        record.status = QStringLiteral("stopped");
        store.update(record);
        return Result::ok(QJsonObject {{QStringLiteral("status"), QStringLiteral("stopped")},
                                       {QStringLiteral("note"), QStringLiteral("容器不存在，已标记为停止")}});
    }

    QString gracefulNote;
    if (!force && state == QLatin1String("running")) {
        RconClient rcon(QStringLiteral("127.0.0.1"), quint16(record.rconPort), record.rconPassword, 4000);
        QString response;
        QString rconError;
        if (rcon.sendCommand(QStringLiteral("save-all flush"), &response, &rconError))
            gracefulNote = QStringLiteral("已执行存档");
        else
            gracefulNote = QStringLiteral("RCON 不可用，直接发送停止信号");
    }

    record.status = QStringLiteral("stopping");
    store.update(record);

    const ProcessResult result = DockerManager::stopContainer(id, force ? 5 : 45, force);
    DockerManager::removeContainer(id, true);

    record.status = QStringLiteral("stopped");
    store.update(record);

    if (!result.ok() && !force) {
        Result failed = Result::fail(QStringLiteral("STOP_FAILED"),
                                     QStringLiteral("停止容器失败"), result.errorText());
        failed.with(QStringLiteral("status"), QStringLiteral("stopped"));
        return failed;
    }
    return Result::ok(QJsonObject {{QStringLiteral("status"), QStringLiteral("stopped")},
                                   {QStringLiteral("note"), gracefulNote}});
}

Result ServerLifecycle::kill(ServerStore &store, const QString &id)
{
    return stop(store, id, true);
}

Result ServerLifecycle::restart(ServerStore &store, const QString &id, int waitSeconds)
{
    const Result stopped = stop(store, id, false);
    if (!stopped.isOk())
        Logger::warn(QStringLiteral("lifecycle"), QStringLiteral("stop before restart failed"));
    return start(store, id, waitSeconds).toResult();
}

Result ServerLifecycle::refreshAll(ServerStore &store)
{
    store.reload();
    QVector<ServerRecord> records = store.all();
    QJsonArray array;
    for (ServerRecord &record : records) {
        const QString state = runtimeState(record);
        if (record.status != state) {
            record.status = state;
            if (state == QLatin1String("running"))
                record.lastError.clear();
            store.update(record);
        }
        array.append(status(record, false));
    }
    QJsonObject data;
    data.insert(QStringLiteral("servers"), array);
    data.insert(QStringLiteral("count"), array.size());
    return Result::ok(data);
}

Result ServerLifecycle::sendCommand(ServerStore &store, const QString &id, const QString &command)
{
    const ServerRecord record = store.get(id);
    if (record.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("服务器 %1 不存在").arg(id));
    if (command.trimmed().isEmpty())
        return Result::fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("指令不能为空"));
    if (runtimeState(record) != QLatin1String("running"))
        return Result::fail(QStringLiteral("NOT_RUNNING"), QStringLiteral("服务器未在运行，无法发送指令"));

    RconClient rcon(QStringLiteral("127.0.0.1"), quint16(record.rconPort), record.rconPassword, 8000);
    QString response;
    QString error;
    if (!rcon.sendCommand(command, &response, &error)) {
        return Result::fail(QStringLiteral("RCON_ERROR"),
                            QStringLiteral("RCON 指令发送失败"),
                            error + QStringLiteral("\n提示：服务器内需要 enable-rcon=true 且端口可用"));
    }
    return Result::ok(QJsonObject {{QStringLiteral("command"), command},
                                   {QStringLiteral("response"), response}});
}

Result ServerLifecycle::saveWorld(ServerStore &store, const QString &id)
{
    return sendCommand(store, id, QStringLiteral("save-all flush"));
}

} // namespace mcsm
