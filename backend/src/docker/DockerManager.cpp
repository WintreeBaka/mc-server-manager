#include "docker/DockerManager.h"

#include <QDir>
#include <QRegularExpression>

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/StringUtil.h"
#include "net/VersionResolver.h"
#include "server/ServerRecord.h"

namespace mcsm {
namespace {

const QString kDockerCandidates[] = {
    QStringLiteral("docker"),
    QStringLiteral("C:/Program Files/Docker/Docker/resources/bin/docker.exe"),
    QStringLiteral("/usr/bin/docker"),
    QStringLiteral("/usr/local/bin/docker"),
};

QString resolveDocker()
{
    static QString cached;
    if (!cached.isEmpty())
        return cached;
    for (const QString &candidate : kDockerCandidates) {
        if (ProcessRunner::programExists(candidate)) {
            cached = candidate;
            return cached;
        }
    }
    return QString();
}

} // namespace

QJsonObject DockerStatus::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("cliFound"), cliFound);
    object.insert(QStringLiteral("daemonRunning"), daemonRunning);
    object.insert(QStringLiteral("version"), version);
    object.insert(QStringLiteral("error"), error);
    return object;
}

QString DockerManager::program()
{
    return resolveDocker();
}

QString DockerManager::containerName(const QString &serverId)
{
    return QStringLiteral("mcsm-") + serverId;
}

QString DockerManager::mountPath(const QString &hostPath)
{
    return QString::fromLocal8Bit(ProcessRunner::toDockerPath(hostPath));
}

DockerStatus DockerManager::status()
{
    DockerStatus result;
    const QString docker = program();
    if (docker.isEmpty()) {
        result.error = QStringLiteral("docker CLI not found in PATH");
        return result;
    }
    result.cliFound = true;

    const ProcessResult version = ProcessRunner::run(
        docker, {QStringLiteral("version"), QStringLiteral("--format"), QStringLiteral("{{.Server.Version}}")},
        20000);
    if (version.ok() && !version.stdOut.trimmed().isEmpty()) {
        result.daemonRunning = true;
        result.version = version.stdOut.trimmed();
        return result;
    }

    const ProcessResult info = ProcessRunner::run(docker, {QStringLiteral("info"), QStringLiteral("--format"),
                                                           QStringLiteral("{{.ServerVersion}}")},
                                                  20000);
    if (info.ok()) {
        result.daemonRunning = true;
        result.version = info.stdOut.trimmed();
        return result;
    }

    result.error = QStringLiteral("docker daemon is not reachable")
                       + (info.stdErr.trimmed().isEmpty() ? QString()
                                                          : QStringLiteral(": ")
                                                                + StringUtil::ellipsize(info.stdErr.trimmed(), 300));
    return result;
}

bool DockerManager::imageExists(const QString &image)
{
    const QString docker = program();
    if (docker.isEmpty() || image.isEmpty())
        return false;
    const ProcessResult result = ProcessRunner::run(
        docker, {QStringLiteral("image"), QStringLiteral("inspect"), image}, 30000);
    return result.ok();
}

bool DockerManager::ensureImage(const QString &image, QString *error, int timeoutMs)
{
    if (imageExists(image)) {
        Logger::debug(QStringLiteral("docker"), QStringLiteral("image present: %1").arg(image));
        return true;
    }
    const QString docker = program();
    if (docker.isEmpty()) {
        if (error)
            *error = QStringLiteral("docker CLI not found");
        return false;
    }
    Logger::info(QStringLiteral("docker"), QStringLiteral("pulling image %1").arg(image));
    const ProcessResult result = ProcessRunner::run(docker, {QStringLiteral("pull"), image}, timeoutMs);
    if (!result.ok()) {
        if (error)
            *error = QStringLiteral("docker pull %1 failed: %2").arg(image, result.errorText());
        return false;
    }
    return true;
}

QStringList DockerManager::localImages(const QString &filterPrefix)
{
    QStringList images;
    const QString docker = program();
    if (docker.isEmpty())
        return images;
    const ProcessResult result = ProcessRunner::run(
        docker,
        {QStringLiteral("images"), QStringLiteral("--format"),
         QStringLiteral("{{.Repository}}:{{.Tag}}")},
        30000);
    if (!result.ok())
        return images;
    const QStringList lines = StringUtil::splitLines(result.stdOut);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (!filterPrefix.isEmpty() && !trimmed.startsWith(filterPrefix))
            continue;
        images << trimmed;
    }
    images.removeDuplicates();
    return images;
}

ProcessResult DockerManager::runContainer(const ServerRecord &record,
                                          const QString &image,
                                          const QString &startScript,
                                          QString *error)
{
    const QString docker = program();
    if (docker.isEmpty()) {
        if (error)
            *error = QStringLiteral("docker CLI not found");
        return ProcessResult {};
    }

    const QString name = containerName(record.id);
    removeContainer(record.id, true);

    const QString mount = mountPath(record.dir);
    const QString memory = record.memory.isEmpty() ? QStringLiteral("4G") : record.memory;

    QStringList args = {
        QStringLiteral("run"), QStringLiteral("-d"),
        QStringLiteral("--name"), name,
        QStringLiteral("--restart"), record.autoRestart ? QStringLiteral("unless-stopped")
                                                        : QStringLiteral("no"),
        QStringLiteral("-p"), QStringLiteral("%1:%1/tcp").arg(record.port),
        // RCON is how the manager sends console commands: publish it on the
        // loopback interface only, never on the LAN.
        QStringLiteral("-p"), QStringLiteral("127.0.0.1:%1:%1/tcp").arg(record.rconPort),
        QStringLiteral("-e"), QStringLiteral("TZ=Asia/Shanghai"),
        QStringLiteral("-e"), QStringLiteral("SERVER_PORT=%1").arg(record.port),
        QStringLiteral("-e"), QStringLiteral("MEMORY=%1").arg(memory),
        QStringLiteral("-e"), QStringLiteral("JAVA_OPTS=%1").arg(record.effectiveJavaOptions()),
        QStringLiteral("-e"), QStringLiteral("MCSM_SERVER_ID=%1").arg(record.id),
        QStringLiteral("--label"), QStringLiteral("mcsm.managed=true"),
        QStringLiteral("--label"), QStringLiteral("mcsm.server=%1").arg(record.id),
        QStringLiteral("-v"), QStringLiteral("%1:/data").arg(mount),
        QStringLiteral("-w"), QStringLiteral("/data"),
        QStringLiteral("--memory"), record.containerMemoryLimit(),
        image,
        QStringLiteral("/bin/sh"), startScript,
    };

    ProcessResult result = ProcessRunner::run(docker, args, 120000);
    if (!result.ok() && error)
        *error = result.errorText();
    return result;
}

bool DockerManager::containerExists(const QString &serverId)
{
    const ProcessResult result = inspect(serverId, QStringLiteral("{{.Id}}"));
    return result.ok() && !result.stdOut.trimmed().isEmpty();
}

QString DockerManager::containerState(const QString &serverId)
{
    const ProcessResult result = inspect(serverId, QStringLiteral("{{.State.Status}}"));
    if (!result.ok())
        return QStringLiteral("missing");
    const QString state = result.stdOut.trimmed();
    return state.isEmpty() ? QStringLiteral("missing") : state;
}

ProcessResult DockerManager::startExisting(const QString &serverId)
{
    return ProcessRunner::run(program(), {QStringLiteral("start"), containerName(serverId)}, 60000);
}

ProcessResult DockerManager::stopContainer(const QString &serverId, int timeoutSec, bool force)
{
    QStringList args {QStringLiteral("stop"), QStringLiteral("-t"), QString::number(timeoutSec)};
    if (force)
        args << QStringLiteral("--signal=SIGKILL");
    args << containerName(serverId);
    return ProcessRunner::run(program(), args, (timeoutSec + 30) * 1000);
}

ProcessResult DockerManager::removeContainer(const QString &serverId, bool force)
{
    QStringList args {QStringLiteral("rm")};
    if (force)
        args << QStringLiteral("-f");
    args << containerName(serverId);
    return ProcessRunner::run(program(), args, 90000);
}

ProcessResult DockerManager::exec(const QString &serverId,
                                  const QStringList &command,
                                  int timeoutMs,
                                  const QByteArray &stdinData)
{
    QStringList args {QStringLiteral("exec"), QStringLiteral("-i"), containerName(serverId)};
    args << command;
    return ProcessRunner::run(program(), args, timeoutMs, stdinData);
}

QString DockerManager::logs(const QString &serverId, int tailLines, bool timestamps)
{
    // container gone (server never started or was deleted): an empty log is the
    // right answer, not docker's error message
    if (containerState(serverId) == QLatin1String("missing"))
        return QString();
    QStringList args {QStringLiteral("logs")};
    if (tailLines > 0)
        args << QStringLiteral("--tail") << QString::number(tailLines);
    if (timestamps)
        args << QStringLiteral("--timestamps");
    args << containerName(serverId);
    const ProcessResult result = ProcessRunner::run(program(), args, 60000);
    if (!result.started)
        return QString();
    // docker logs writes the container stream to stderr
    QString text = result.stdOut;
    text.append(result.stdErr);
    return StringUtil::stripAnsi(text);
}

QJsonObject DockerManager::stats(const QString &serverId)
{
    QJsonObject object;
    const ProcessResult result = ProcessRunner::run(
        program(),
        {QStringLiteral("stats"), QStringLiteral("--no-stream"), QStringLiteral("--format"),
         QStringLiteral("{{.CPUPerc}}|{{.MemUsage}}|{{.MemPerc}}"),
         containerName(serverId)},
        30000);
    if (!result.ok())
        return object;
    const QStringList parts = result.stdOut.trimmed().split(QLatin1Char('|'));
    if (parts.size() >= 3) {
        object.insert(QStringLiteral("cpu"), parts.at(0).trimmed());
        object.insert(QStringLiteral("memory"), parts.at(1).trimmed());
        object.insert(QStringLiteral("memoryPercent"), parts.at(2).trimmed());
    }
    return object;
}

ProcessResult DockerManager::inspect(const QString &serverId, const QString &format)
{
    return ProcessRunner::run(program(),
                              {QStringLiteral("inspect"), QStringLiteral("-f"), format,
                               containerName(serverId)},
                              30000);
}

} // namespace mcsm
