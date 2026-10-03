#include "server/ServerInstaller.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "config/ConfigManager.h"
#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/ProcessRunner.h"
#include "core/StringUtil.h"
#include "docker/DockerManager.h"
#include "docker/TemplateWriter.h"
#include "java/JavaManager.h"
#include "net/HttpClient.h"
#include "net/VersionResolver.h"

namespace mcsm {
namespace {

void report(const InstallProgressFn &progress, const QString &stage, int percent, const QString &detail)
{
    Logger::info(QStringLiteral("install"), QStringLiteral("[%1%] %2 %3").arg(percent).arg(stage, detail));
    if (progress)
        progress(stage, percent, detail);
}

QMap<QString, QString> buildOverrideMap(const InstallRequest &request, const ServerRecord &record)
{
    QMap<QString, QString> values;
    values.insert(QStringLiteral("server-port"), QString::number(record.port));
    values.insert(QStringLiteral("level-name"), record.levelName);
    values.insert(QStringLiteral("motd"), record.motd.isEmpty() ? record.name : record.motd);
    values.insert(QStringLiteral("enable-rcon"), QStringLiteral("true"));
    values.insert(QStringLiteral("rcon.port"), QString::number(record.rconPort));
    values.insert(QStringLiteral("rcon.password"), record.rconPassword);
    if (!request.gamemode.isEmpty())
        values.insert(QStringLiteral("gamemode"), request.gamemode);
    if (!request.difficulty.isEmpty())
        values.insert(QStringLiteral("difficulty"), request.difficulty);
    if (request.maxPlayers > 0)
        values.insert(QStringLiteral("max-players"), QString::number(request.maxPlayers));
    for (auto it = request.overrides.constBegin(); it != request.overrides.constEnd(); ++it) {
        const QString key = it.key();
        QString value;
        if (it.value().isBool())
            value = it.value().toBool() ? QStringLiteral("true") : QStringLiteral("false");
        else if (it.value().isDouble())
            value = QString::number(it.value().toDouble(), 'g', 12);
        else
            value = it.value().toString();
        values.insert(key, value);
    }
    return values;
}

} // namespace

bool ServerInstaller::downloadJar(ServerRecord *record, QString *error, const InstallProgressFn &progress)
{
    if (!record)
        return false;
    if (record->jarUrl.isEmpty()) {
        if (error)
            *error = QStringLiteral("no download url resolved for %1 %2")
                         .arg(record->type, record->mcVersion);
        return false;
    }

    const QString target = record->jarPath();
    QDir().mkpath(record->dir);

    if (QFileInfo::exists(target) && QFileInfo(target).size() > 4096) {
        report(progress, QStringLiteral("下载服务端"), 70,
               QStringLiteral("已存在 %1，跳过下载").arg(record->jarName));
        return true;
    }

    const QString reportUrl = record->jarUrl;
    QString lastReported;
    const bool ok = HttpClient::download(
        QUrl(reportUrl), target, error,
        [&progress, &lastReported](qint64 received, qint64 total) {
            if (total <= 0)
                return;
            const int percent = int((received * 100) / total);
            if (percent - lastReported.toInt() >= 5 || percent >= 100) {
                lastReported = QString::number(percent);
                report(progress, QStringLiteral("下载服务端"), 45 + percent / 4,
                       QStringLiteral("已下载 %1%").arg(percent));
            }
        });
    if (!ok)
        return false;
    if (QFileInfo(target).size() < 1024) {
        if (error)
            *error = QStringLiteral("下载到的服务端文件异常（过小），请检查网络或代理设置");
        QFile::remove(target);
        return false;
    }
    return true;
}

bool ServerInstaller::importJar(const ServerRecord &record, const QString &sourceJar, QString *error)
{
    const QFileInfo source(sourceJar);
    if (!source.exists() || !source.isFile()) {
        if (error)
            *error = QStringLiteral("服务端文件不存在: %1").arg(sourceJar);
        return false;
    }
    if (source.size() < 1024) {
        if (error)
            *error = QStringLiteral("服务端文件过小，可能不是有效的 jar: %1").arg(sourceJar);
        return false;
    }
    QDir().mkpath(record.dir);
    const QString target = record.jarPath();
    if (QFileInfo(source.absoluteFilePath()) == QFileInfo(target)) {
        return true;
    }
    if (QFileInfo::exists(target))
        QFile::remove(target);
    if (!QFile::copy(source.absoluteFilePath(), target)) {
        if (error)
            *error = QStringLiteral("无法复制 %1 到 %2").arg(sourceJar, target);
        return false;
    }
    return true;
}

bool ServerInstaller::prepareDirectory(const ServerRecord &record,
                                       const QMap<QString, QString> &propertyOverrides,
                                       QString *error)
{
    if (!QDir().mkpath(record.dir)) {
        if (error)
            *error = QStringLiteral("无法创建目录 %1").arg(record.dir);
        return false;
    }
    QDir().mkpath(record.backupDir());
    QDir().mkpath(record.configBackupDir());
    if (record.type.compare(QLatin1String("vanilla"), Qt::CaseInsensitive) != 0)
        QDir().mkpath(record.pluginDir());

    QString writeError;
    const QString propertiesPath = record.propertiesPath();
    if (!QFileInfo::exists(propertiesPath)) {
        TemplateWriter::writeDefaultProperties(record, true);
    }
    QMap<QString, QString> values = propertyOverrides;
    if (!values.isEmpty()) {
        QVector<ConfigDiffEntry> diff;
        if (!ConfigManager::applyValues(record, values, QStringLiteral("install"),
                                        QStringLiteral("创建服务器时的初始配置"), &diff, &writeError)) {
            if (error)
                *error = writeError;
            return false;
        }
    }
    if (!TemplateWriter::writeStartScript(record, &writeError)
        || !TemplateWriter::writeEula(record, record.eulaAccepted, &writeError)
        || !TemplateWriter::writeComposeFile(record, &writeError)
        || !TemplateWriter::writeReadme(record, &writeError)) {
        if (error)
            *error = writeError;
        return false;
    }
    ConfigManager::ensureBaseline(record, nullptr);
    return true;
}

Result ServerInstaller::install(ServerStore &store,
                                const InstallRequest &request,
                                const InstallProgressFn &progress)
{
    if (request.name.trimmed().isEmpty())
        return Result::fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("请填写服务器名称"));

    const QString flavour = request.type.trimmed().toLower();
    if (!VersionResolver::isSupported(flavour)) {
        return Result::fail(QStringLiteral("UNSUPPORTED_TYPE"),
                            QStringLiteral("暂不支持的服务端类型: %1").arg(request.type),
                            QStringLiteral("支持: %1").arg(VersionResolver::supportedTypes().join(QStringLiteral(", "))));
    }
    if (!request.manual && request.mcVersion.trimmed().isEmpty())
        return Result::fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("请选择 Minecraft 版本"));
    if (request.manual && request.manualJarPath.trimmed().isEmpty())
        return Result::fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("手动模式需要选择本地服务端 jar 文件"));

    ServerRecord record;
    record.id = request.preferredId.trimmed().isEmpty() ? store.allocateId(request.name)
                                                        : StringUtil::slugify(request.preferredId);
    if (store.contains(record.id))
        return Result::fail(QStringLiteral("ALREADY_EXISTS"),
                            QStringLiteral("服务器标识 %1 已存在").arg(record.id));
    record.name = request.name.trimmed();
    record.type = flavour;
    record.mcVersion = request.mcVersion.trimmed();
    record.port = request.port > 0 ? request.port : store.allocatePort();
    record.rconPort = request.rconPort > 0 ? request.rconPort : store.allocateRconPort();
    record.memory = request.memory.trimmed().isEmpty() ? QStringLiteral("4G") : request.memory.trimmed();
    record.memoryLimit = request.memoryLimit.trimmed();
    record.levelName = request.levelName.trimmed().isEmpty() ? QStringLiteral("world")
                                                             : request.levelName.trimmed();
    record.motd = request.motd.trimmed().isEmpty() ? record.name : request.motd.trimmed();
    record.extraJavaOptions = request.javaOptions.trimmed();
    record.eulaAccepted = request.acceptEula;
    record.manualInstall = request.manual;
    record.autoRestart = request.autoRestart;
    record.autoStart = request.autoStart;
    record.dir = AppPaths::serverDir(record.id);
    record.status = QStringLiteral("stopped");
    record.runtime = request.runtime.compare(QLatin1String("host"), Qt::CaseInsensitive) == 0
                         ? QStringLiteral("host")
                         : QStringLiteral("docker");

    if (record.runtime == QLatin1String("host")) {
        // experimental: run on a JDK cloned from this machine instead of a container
        const JavaManager::HostJdk jdk = JavaManager::describeJdk(request.jdkHome);
        if (jdk.major <= 0) {
            return Result::fail(QStringLiteral("INVALID_ARGUMENT"),
                                QStringLiteral("所选 JDK 不可用"),
                                QStringLiteral("%1 不是有效的 JDK 目录").arg(request.jdkHome));
        }
        record.javaMajor = request.javaMajor > 0 ? request.javaMajor : jdk.major;
        record.image.clear();
        record.jdkHome = QDir(record.dir).filePath(QStringLiteral("jdk"));
    }

    if (store.all().size() > 0) {
        for (const ServerRecord &other : store.all()) {
            if (other.port == record.port)
                return Result::fail(QStringLiteral("PORT_IN_USE"),
                                    QStringLiteral("端口 %1 已被服务器 %2 占用")
                                        .arg(record.port)
                                        .arg(other.name));
        }
    }

    report(progress, QStringLiteral("校验参数"), 5,
           QStringLiteral("%1 · %2 · %3").arg(record.name, flavour, record.mcVersion));

    if (!QDir().mkpath(record.dir))
        return Result::fail(QStringLiteral("IO_ERROR"), QStringLiteral("无法创建服务器目录"),
                            record.dir);

    report(progress, QStringLiteral("解析版本"), 15,
           QStringLiteral("正在查询 %1 的下载信息").arg(flavour));
    if (!request.manual || !request.manualJarPath.isEmpty()) {
        if (request.manual) {
            record.javaMajor = request.javaMajor > 0 ? request.javaMajor : 17;
            record.jarName = QFileInfo(request.manualJarPath).fileName();
            record.jarUrl.clear();
        } else {
            ResolvedServer resolved;
            QString resolveError;
            if (!VersionResolver::resolve(flavour, record.mcVersion, &resolved, &resolveError)) {
                return Result::fail(QStringLiteral("VERSION_RESOLVE_FAILED"),
                                    QStringLiteral("无法获取 %1 %2 的服务端下载地址")
                                        .arg(flavour, record.mcVersion),
                                    resolveError);
            }
            record.jarUrl = resolved.jarUrl;
            record.jarName = QStringLiteral("server.jar");
            record.build = resolved.build;
            record.javaMajor = request.javaMajor > 0 ? request.javaMajor : resolved.javaMajor;
        }
    }
    if (record.javaMajor <= 0)
        record.javaMajor = VersionResolver::recommendedJavaMajor(record.mcVersion);
    if (!record.usesHostJdk()) {
        record.image = request.image.trimmed().isEmpty() ? VersionResolver::javaImage(record.javaMajor)
                                                         : request.image.trimmed();
    }

    report(progress, QStringLiteral("下载服务端"), 45, record.jarUrl);
    QString jarError;
    if (request.manual) {
        if (!importJar(record, request.manualJarPath, &jarError)) {
            return Result::fail(QStringLiteral("JAR_IMPORT_FAILED"),
                                QStringLiteral("导入本地服务端失败"), jarError);
        }
    } else if (!downloadJar(&record, &jarError, progress)) {
        return Result::fail(QStringLiteral("DOWNLOAD_FAILED"),
                            QStringLiteral("下载服务端失败"), jarError);
    }

    report(progress, QStringLiteral("写入配置"), 80, QStringLiteral("生成 server.properties / start.sh"));
    QString prepareError;
    if (!prepareDirectory(record, buildOverrideMap(request, record), &prepareError))
        return Result::fail(QStringLiteral("IO_ERROR"), QStringLiteral("写入服务器配置失败"), prepareError);
    if (record.usesHostJdk()) {
        report(progress, QStringLiteral("克隆本机 JDK"), 80,
               QStringLiteral("从 %1 复制到服务器目录…").arg(request.jdkHome));
        QString cloneError;
        if (!cloneJdk(request.jdkHome, record.jdkHome, &cloneError, progress))
            return Result::fail(QStringLiteral("JDK_CLONE_FAILED"),
                                QStringLiteral("克隆本机 JDK 失败"), cloneError);
        TemplateWriter::writeHostStartScript(record, nullptr);
    }

    QStringList warningList;
    if (!request.skipDocker && !record.usesHostJdk()) {
        report(progress, QStringLiteral("准备 JDK 容器"), 88,
               QStringLiteral("检查镜像 %1").arg(record.image));
        const DockerStatus docker = DockerManager::status();
        if (!docker.cliFound || !docker.daemonRunning) {
            return Result::fail(
                QStringLiteral("DOCKER_UNAVAILABLE"),
                QStringLiteral("Docker 不可用，无法容器化运行环境"),
                docker.error.isEmpty() ? QStringLiteral("请先启动 Docker Desktop") : docker.error +
                    QStringLiteral("\n服务器文件已准备好，启动 Docker 后可重新执行一次安装（会复用已下载的服务端）。"));
        }
        QString imageError;
        if (!DockerManager::ensureImage(record.image, &imageError, 1800000)) {
            return Result::fail(QStringLiteral("IMAGE_PULL_FAILED"),
                                QStringLiteral("拉取 JDK 镜像失败"), imageError);
        }
    } else if (request.skipDocker) {
        warningList << QStringLiteral("已跳过 Docker 检查，服务器暂时无法启动");
    }

    report(progress, QStringLiteral("登记服务器"), 95, record.id);
    QString storeError;
    if (!store.add(record, &storeError)) {
        return Result::fail(QStringLiteral("STORE_ERROR"), QStringLiteral("保存服务器记录失败"), storeError);
    }

    report(progress, QStringLiteral("完成"), 100, QStringLiteral("服务器已就绪"));
    Result result = Result::ok(record.toJson());
    for (const QString &warning : warningList)
        result.warn(warning);
    return result;
}

Result ServerInstaller::deleteServer(ServerStore &store, const QString &id, bool purgeFiles)
{
    const ServerRecord record = store.get(id);
    if (record.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("服务器 %1 不存在").arg(id));

    DockerManager::removeContainer(id, true);
    QString storeError;
    if (!store.remove(id, &storeError))
        return Result::fail(QStringLiteral("STORE_ERROR"), QStringLiteral("删除记录失败"), storeError);

    if (purgeFiles) {
        QDir dir(record.dir);
        if (dir.exists() && !dir.removeRecursively())
            return Result::ok().warn(QStringLiteral("记录已删除，但目录 %1 未能完全删除").arg(record.dir));
    }
    return Result::ok(QJsonObject {{QStringLiteral("id"), id},
                                   {QStringLiteral("purged"), purgeFiles}});
}

bool ServerInstaller::cloneJdk(const QString &sourceHome,
                               const QString &targetHome,
                               QString *error,
                               const InstallProgressFn &progress)
{
    const QDir source(sourceHome);
    if (!source.exists()) {
        if (error)
            *error = QStringLiteral("JDK 目录不存在: %1").arg(sourceHome);
        return false;
    }
#ifdef Q_OS_WIN
    const QString javaRelative = QStringLiteral("bin/java.exe");
#else
    const QString javaRelative = QStringLiteral("bin/java");
#endif
    if (!QFileInfo::exists(source.filePath(javaRelative))) {
        if (error)
            *error = QStringLiteral("%1 不是有效的 JDK 目录（缺少 %2）").arg(sourceHome, javaRelative);
        return false;
    }

    QDir target(targetHome);
    if (target.exists() && !target.removeRecursively()) {
        if (error)
            *error = QStringLiteral("无法清理旧的 JDK 目录: %1").arg(targetHome);
        return false;
    }
    if (!QDir().mkpath(targetHome)) {
        if (error)
            *error = QStringLiteral("无法创建 %1").arg(targetHome);
        return false;
    }

    // count first so the GUI can show a percentage
    int totalFiles = 0;
    QDirIterator counter(sourceHome, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (counter.hasNext()) {
        counter.next();
        ++totalFiles;
    }

    int copied = 0;
    QDirIterator iterator(sourceHome, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QString relative = source.relativeFilePath(iterator.filePath());
        const QString destination = target.filePath(relative);
        QDir().mkpath(QFileInfo(destination).absolutePath());
        if (!QFile::copy(iterator.filePath(), destination)) {
            // some JDK files are read-only; retry after making the copy writable
            QFile::setPermissions(destination, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            if (!QFile::copy(iterator.filePath(), destination)) {
                if (error)
                    *error = QStringLiteral("复制 JDK 文件失败: %1").arg(relative);
                return false;
            }
        }
        ++copied;
        if (progress && totalFiles > 0 && copied % 200 == 0) {
            progress(QStringLiteral("克隆本机 JDK"), 80 + (copied * 15) / totalFiles,
                     QStringLiteral("已复制 %1/%2 个文件").arg(copied).arg(totalFiles));
        }
    }
    if (progress)
        progress(QStringLiteral("克隆本机 JDK"), 95,
                 QStringLiteral("已复制 %1 个文件").arg(copied));
    Logger::info(QStringLiteral("install"),
                 QStringLiteral("cloned jdk from %1 (%2 files)").arg(sourceHome).arg(copied));
    return true;
}
} // namespace mcsm
