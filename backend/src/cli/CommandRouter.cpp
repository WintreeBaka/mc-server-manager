#include "cli/CommandRouter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include "backup/BackupService.h"
#include "config/ConfigManager.h"
#include "config/ConfigSchema.h"
#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/ProcessRunner.h"
#include "core/StringUtil.h"
#include "docker/DockerManager.h"
#include "docker/TemplateWriter.h"
#include "java/JavaManager.h"
#include "net/HttpClient.h"
#include "net/VersionResolver.h"
#include "plugin/PluginManager.h"
#include "schedule/Scheduler.h"
#include "server/ServerInstaller.h"
#include "server/ServerLifecycle.h"
#include "server/ServerStore.h"

namespace mcsm {

CommandRouter::CommandRouter(const QStringList &arguments)
    : m_args(arguments)
{
    const QStringList positionals = m_args.positionals();
    if (positionals.isEmpty()) {
        m_command = QStringLiteral("help");
    } else {
        m_command = positionals.first().toLower();
        if (positionals.size() > 1)
            m_sub = positionals.at(1).toLower();
    }
    if (m_command == QLatin1String("server") && m_sub == QLatin1String("logs")
        && m_args.boolValue(QStringLiteral("follow"), false)) {
        m_streaming = true;
    }
}

QStringList CommandRouter::helpLines() const
{
    return {
        QStringLiteral("McServerManager backend (mcsm-cli)"),
        QString(),
        QStringLiteral("  doctor                                    环境自检"),
        QStringLiteral("  types                                     支持的服务端类型"),
        QStringLiteral("  versions --type <paper>                   可用游戏版本列表"),
        QStringLiteral("  java --action list|ensure [--major 17]    JDK 镜像管理"),
        QStringLiteral("  install --name X --type paper --version 1.20.4 [--port 25565]"),
        QStringLiteral("  server list|get|start|stop|restart|kill|status|delete ..."),
        QStringLiteral("  server logs --id X [--tail 200] [--follow]"),
        QStringLiteral("  server command --id X --command \"say hi\""),
        QStringLiteral("  config schema|read|apply|raw|validate|backups|rollback --id X"),
        QStringLiteral("  config apply --id X --values '{...}' | --values-file values.json"),
        QStringLiteral("  backup list|create|restore|remove|schedule --id X"),
        QStringLiteral("  plugin sources|search|install|list|remove|toggle --id X"),
        QStringLiteral("  schedule tick|status                     定时备份守护进程"),
        QStringLiteral("  settings get|set"),
        QString(),
        QStringLiteral("Global flags: --json --pretty --verbose --quiet --home <dir>"),
    };
}

Result CommandRouter::run()
{
    if (m_streaming)
        return Result::ok();
    if (m_command == QLatin1String("help") || m_command == QLatin1String("--help")) {
        return Result::ok(QJsonObject {
            {QStringLiteral("usage"), QJsonArray::fromStringList(helpLines())},
        });
    }
    return dispatch(m_command);
}

Result CommandRouter::dispatch(const QString &command)
{
    if (command == QLatin1String("doctor"))
        return cmdDoctor();
    if (command == QLatin1String("types"))
        return cmdTypes();
    if (command == QLatin1String("versions"))
        return cmdVersions();
    if (command == QLatin1String("java"))
        return cmdJava();
    if (command == QLatin1String("install") || command == QLatin1String("create"))
        return cmdInstall();
    if (command == QLatin1String("server"))
        return cmdServer();
    if (command == QLatin1String("config"))
        return cmdConfig();
    if (command == QLatin1String("backup"))
        return cmdBackup();
    if (command == QLatin1String("plugin"))
        return cmdPlugin();
    if (command == QLatin1String("schedule") || command == QLatin1String("daemon"))
        return cmdSchedule();
    if (command == QLatin1String("settings"))
        return cmdSettings();
    if (command == QLatin1String("version")) {
        return Result::ok(QJsonObject {
            {QStringLiteral("name"), QStringLiteral("mcsm-cli")},
            {QStringLiteral("version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("home"), AppPaths::root()},
            {QStringLiteral("userAgent"), HttpClient::userAgent()},
        });
    }
    return Result::fail(QStringLiteral("UNKNOWN_COMMAND"),
                        QStringLiteral("未知命令 '%1'").arg(command),
                        QStringLiteral("执行 `mcsm-cli help` 查看可用命令"));
}

Result CommandRouter::cmdDoctor()
{
    const DockerStatus docker = DockerManager::status();
    const JavaManager::HostJava hostJava = JavaManager::detectHostJava();

    QJsonObject host;
    host.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
    host.insert(QStringLiteral("arch"), QSysInfo::currentCpuArchitecture());
    host.insert(QStringLiteral("dataRoot"), AppPaths::root());
    host.insert(QStringLiteral("backend"), QCoreApplication::applicationFilePath());
    host.insert(QStringLiteral("qtVersion"), QLatin1String(qVersion()));

    QJsonObject java;
    java.insert(QStringLiteral("hostFound"), hostJava.found);
    java.insert(QStringLiteral("hostVersion"), hostJava.version);
    java.insert(QStringLiteral("hostMajor"), hostJava.major);
    java.insert(QStringLiteral("images"), QJsonArray::fromStringList(JavaManager::installedImages()));
    java.insert(QStringLiteral("strategy"), QStringLiteral("docker"));
    java.insert(QStringLiteral("overview"), JavaManager::overview());

    QJsonObject tools;
    tools.insert(QStringLiteral("docker"), docker.toJson());
    tools.insert(QStringLiteral("tar"), ProcessRunner::programExists(QStringLiteral("tar")));
    tools.insert(QStringLiteral("tarPath"),
                 ProcessRunner::resolveProgram({QStringLiteral("tar"),
                                                QStringLiteral("C:/Windows/System32/tar.exe")}));
    tools.insert(QStringLiteral("powershell"), ProcessRunner::programExists(QStringLiteral("powershell")));

    Result result = Result::ok(QJsonObject {
        {QStringLiteral("host"), host},
        {QStringLiteral("docker"), docker.toJson()},
        {QStringLiteral("java"), java},
        {QStringLiteral("tools"), tools},
        {QStringLiteral("serverCount"), ServerStore().all().size()},
        {QStringLiteral("supportedTypes"), QJsonArray::fromStringList(VersionResolver::supportedTypes())},
    });
    if (!docker.cliFound)
        result.warn(QStringLiteral("未检测到 docker 命令，请先安装 Docker Desktop"));
    else if (!docker.daemonRunning)
        result.warn(QStringLiteral("Docker 服务未运行：%1").arg(docker.error));
    return result;
}

Result CommandRouter::cmdTypes()
{
    QJsonArray types;
    const QVector<QPair<QString, QString>> known = {
        {QStringLiteral("vanilla"), QStringLiteral("官方原版服务端，最稳定，不支持插件")},
        {QStringLiteral("paper"), QStringLiteral("PaperMC，性能优秀，支持 Bukkit 插件（推荐）")},
        {QStringLiteral("purpur"), QStringLiteral("Purpur，Paper 的分支，可调参数更多")},
        {QStringLiteral("fabric"), QStringLiteral("Fabric Loader，适合使用模组 (mods)")},
    };
    for (const auto &entry : known) {
        types.append(QJsonObject {{QStringLiteral("id"), entry.first},
                                  {QStringLiteral("label"), entry.first},
                                  {QStringLiteral("description"), entry.second},
                                  {QStringLiteral("supportsPlugins"),
                                   entry.first == QLatin1String("paper")
                                       || entry.first == QLatin1String("purpur")}});
    }
    return Result::ok(QJsonObject {{QStringLiteral("types"), types}});
}

Result CommandRouter::cmdVersions()
{
    const QString type = m_args.value(QStringLiteral("type"), QStringLiteral("paper"));
    QString error;
    const QVector<ResolvedServer> metadata = VersionResolver::listVersionMetadata(type, &error);
    if (metadata.isEmpty()) {
        return Result::fail(QStringLiteral("VERSION_LIST_FAILED"),
                            QStringLiteral("无法获取 %1 的版本列表").arg(type),
                            error.isEmpty() ? QStringLiteral("请检查网络连接") : error);
    }
    QJsonArray versions;
    for (const ResolvedServer &item : metadata) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), item.mcVersion);
        object.insert(QStringLiteral("type"), item.type);
        object.insert(QStringLiteral("javaMajor"), item.javaMajor);
        object.insert(QStringLiteral("javaLabel"), VersionResolver::javaLabel(item.javaMajor));
        object.insert(QStringLiteral("javaImage"), VersionResolver::javaImage(item.javaMajor));
        object.insert(QStringLiteral("releaseTime"), item.releaseTime);
        object.insert(QStringLiteral("channel"), item.channel);
        versions.append(object);
    }
    return Result::ok(QJsonObject {{QStringLiteral("type"), type},
                                   {QStringLiteral("count"), versions.size()},
                                   {QStringLiteral("versions"), versions}});
}

Result CommandRouter::cmdJava()
{
    const QString action = m_args.value(QStringLiteral("action"), QStringLiteral("list"));
    if (action == QLatin1String("list"))
        return Result::ok(JavaManager::overview());
    if (action == QLatin1String("ensure")) {
        const int major = m_args.intValue(QStringLiteral("major"), 17);
        QString error;
        if (!JavaManager::ensureImageForMajor(major, &error)) {
            return Result::fail(QStringLiteral("IMAGE_PULL_FAILED"),
                                QStringLiteral("JDK 镜像准备失败"), error);
        }
        return Result::ok(QJsonObject {{QStringLiteral("image"), JavaManager::imageForMajor(major)},
                                       {QStringLiteral("major"), major}});
    }
    if (action == QLatin1String("scan")) {
        const QVector<JavaManager::HostJdk> jdks = JavaManager::scanHostJdks();
        QJsonArray array;
        for (const JavaManager::HostJdk &jdk : jdks)
            array.append(jdk.toJson());
        return Result::ok(QJsonObject {{QStringLiteral("count"), array.size()},
                                       {QStringLiteral("jdks"), array}});
    }
    return Result::fail(QStringLiteral("INVALID_ARGUMENT"),
                        QStringLiteral("未知 action '%1'").arg(action));
}

Result CommandRouter::cmdInstall()
{
    InstallRequest request;
    request.name = m_args.value(QStringLiteral("name"));
    request.type = m_args.value(QStringLiteral("type"), QStringLiteral("paper"));
    request.mcVersion = m_args.value(QStringLiteral("version"), m_args.value(QStringLiteral("mc-version")));
    request.preferredId = m_args.value(QStringLiteral("id"));
    request.port = m_args.intValue(QStringLiteral("port"), 0);
    request.rconPort = m_args.intValue(QStringLiteral("rcon-port"), 0);
    request.memory = m_args.value(QStringLiteral("memory"), QStringLiteral("4G"));
    request.memoryLimit = m_args.value(QStringLiteral("memory-limit"));
    request.levelName = m_args.value(QStringLiteral("level-name"), QStringLiteral("world"));
    request.motd = m_args.value(QStringLiteral("motd"));
    request.gamemode = m_args.value(QStringLiteral("gamemode"));
    request.difficulty = m_args.value(QStringLiteral("difficulty"));
    request.maxPlayers = m_args.intValue(QStringLiteral("max-players"), 20);
    request.javaMajor = m_args.intValue(QStringLiteral("java"), 0);
    request.image = m_args.value(QStringLiteral("image"));
    request.javaOptions = m_args.value(QStringLiteral("java-options"));
    request.acceptEula = m_args.boolValue(QStringLiteral("eula"), true);
    request.manual = m_args.boolValue(QStringLiteral("manual"), false);
    request.manualJarPath = m_args.value(QStringLiteral("jar"));
    request.runtime = m_args.value(QStringLiteral("runtime"), QStringLiteral("docker"));
    request.jdkHome = m_args.value(QStringLiteral("jdk-home"), m_args.value(QStringLiteral("jdk")));
    request.skipDocker = m_args.boolValue(QStringLiteral("skip-docker"), false);
    request.autoStart = m_args.boolValue(QStringLiteral("auto-start"), false);
    request.autoRestart = m_args.boolValue(QStringLiteral("auto-restart"), true);
    request.overrides = m_args.jsonObject(QStringLiteral("overrides"));
    if (request.manual) {
        request.mcVersion = m_args.value(QStringLiteral("version"), QStringLiteral("自定义"));
        if (request.type.isEmpty())
            request.type = QStringLiteral("paper");
    }

    ServerStore store;
    InstallProgressFn progress;
    if (m_args.boolValue(QStringLiteral("progress"), false)) {
        progress = [](const QString &stage, int percent, const QString &detail) {
            QJsonObject line;
            line.insert(QStringLiteral("type"), QStringLiteral("progress"));
            line.insert(QStringLiteral("stage"), stage);
            line.insert(QStringLiteral("percent"), percent);
            line.insert(QStringLiteral("detail"), detail);
            const QByteArray bytes = QJsonDocument(line).toJson(QJsonDocument::Compact) + "\n";
            fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
            fflush(stdout);
        };
    }
    return ServerInstaller::install(store, request, progress);
}

Result CommandRouter::cmdServer()
{
    ServerStore store;
    const QString action = m_args.positional(1, QStringLiteral("list"));
    const QString id = m_args.value(QStringLiteral("id"), m_args.positional(2));

    if (action == QLatin1String("list")) {
        QJsonArray array;
        for (const ServerRecord &record : store.all())
            array.append(ServerLifecycle::status(record, m_args.boolValue(QStringLiteral("stats"), false)));
        return Result::ok(QJsonObject {{QStringLiteral("count"), array.size()},
                                       {QStringLiteral("servers"), array},
                                       {QStringLiteral("home"), AppPaths::root()}});
    }
    if (action == QLatin1String("refresh"))
        return ServerLifecycle::refreshAll(store);

    if (id.isEmpty())
        return Result::fail(QStringLiteral("INVALID_ARGUMENT"),
                            QStringLiteral("缺少 --id 参数"));

    const ServerRecord record = store.get(id);
    if (record.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("服务器 %1 不存在").arg(id));

    if (action == QLatin1String("get") || action == QLatin1String("status")) {
        QJsonObject object = ServerLifecycle::status(record, true);
        object.insert(QStringLiteral("schedule"), BackupService::scheduleInfo(record));
        object.insert(QStringLiteral("logTail"),
                      QJsonArray::fromStringList(ServerLifecycle::logTail(record, 60)));
        return Result::ok(object);
    }
    if (action == QLatin1String("logs")) {
        const QStringList lines = ServerLifecycle::logTail(record, m_args.intValue(QStringLiteral("tail"), 200));
        return Result::ok(QJsonObject {{QStringLiteral("id"), id},
                                       {QStringLiteral("lines"), QJsonArray::fromStringList(lines)},
                                       {QStringLiteral("text"), lines.join(QLatin1Char('\n'))}});
    }
    if (action == QLatin1String("start")) {
        return ServerLifecycle::start(store, id, m_args.intValue(QStringLiteral("wait"), 90)).toResult();
    }
    if (action == QLatin1String("restart"))
        return ServerLifecycle::restart(store, id, m_args.intValue(QStringLiteral("wait"), 90));
    if (action == QLatin1String("stop"))
        return ServerLifecycle::stop(store, id, m_args.boolValue(QStringLiteral("force"), false));
    if (action == QLatin1String("kill"))
        return ServerLifecycle::kill(store, id);
    if (action == QLatin1String("command")) {
        const QString command = m_args.value(QStringLiteral("command"), m_args.positional(3));
        return ServerLifecycle::sendCommand(store, id, command);
    }
    if (action == QLatin1String("update") || action == QLatin1String("set")) {
        ServerRecord updated = record;
        bool changed = false;
        auto applyText = [&](const QString &flag, QString *field) {
            if (!m_args.has(flag))
                return;
            *field = m_args.value(flag);
            changed = true;
        };
        applyText(QStringLiteral("name"), &updated.name);
        applyText(QStringLiteral("memory"), &updated.memory);
        applyText(QStringLiteral("memory-limit"), &updated.memoryLimit);
        applyText(QStringLiteral("java-options"), &updated.extraJavaOptions);
        applyText(QStringLiteral("image"), &updated.image);
        applyText(QStringLiteral("level-name"), &updated.levelName);
        applyText(QStringLiteral("motd"), &updated.motd);
        if (m_args.has(QStringLiteral("port"))) {
            updated.port = m_args.intValue(QStringLiteral("port"), updated.port);
            changed = true;
        }
        if (m_args.has(QStringLiteral("eula"))) {
            updated.eulaAccepted = m_args.boolValue(QStringLiteral("eula"), updated.eulaAccepted);
            changed = true;
        }
        if (m_args.has(QStringLiteral("auto-restart"))) {
            updated.autoRestart = m_args.boolValue(QStringLiteral("auto-restart"), updated.autoRestart);
            changed = true;
        }
        if (m_args.has(QStringLiteral("auto-start"))) {
            updated.autoStart = m_args.boolValue(QStringLiteral("auto-start"), updated.autoStart);
            changed = true;
        }
        if (m_args.has(QStringLiteral("java"))) {
            updated.javaMajor = m_args.intValue(QStringLiteral("java"), updated.javaMajor);
            if (updated.image.isEmpty() || m_args.has(QStringLiteral("java")))
                updated.image = VersionResolver::javaImage(updated.javaMajor);
            changed = true;
        }
        if (!changed)
            return Result::fail(QStringLiteral("INVALID_ARGUMENT"),
                                QStringLiteral("没有提供需要更新的字段"));

        QString updateError;
        if (!store.update(updated, &updateError))
            return Result::fail(QStringLiteral("STORE_ERROR"), QStringLiteral("更新服务器失败"), updateError);

        TemplateWriter::writeEula(updated, updated.eulaAccepted, nullptr);
        TemplateWriter::writeStartScript(updated, nullptr);
        TemplateWriter::writeComposeFile(updated, nullptr);

        // keep the managed properties in sync with the record
        QMap<QString, QString> properties;
        if (m_args.has(QStringLiteral("motd")))
            properties.insert(QStringLiteral("motd"), updated.motd);
        if (m_args.has(QStringLiteral("level-name")))
            properties.insert(QStringLiteral("level-name"), updated.levelName);
        if (m_args.has(QStringLiteral("port")))
            properties.insert(QStringLiteral("server-port"), QString::number(updated.port));
        if (!properties.isEmpty()) {
            QVector<ConfigDiffEntry> diff;
            ConfigManager::applyValues(updated, properties, QStringLiteral("update"),
                                       QStringLiteral("通过服务器设置修改"), &diff, nullptr);
        }

        Result result = Result::ok(updated.toJson());
        if (updated.image != record.image)
            result.warn(QStringLiteral("运行环境已改为 %1，下次启动会自动拉取").arg(updated.image));
        if (updated.eulaAccepted && !record.eulaAccepted)
            result.warn(QStringLiteral("已记录 EULA 同意状态"));
        if (m_args.has(QStringLiteral("port")) && updated.port != record.port)
            result.warn(QStringLiteral("端口已改为 %1，重启服务器后生效").arg(updated.port));
        return result;
    }
    if (action == QLatin1String("save"))
        return ServerLifecycle::saveWorld(store, id);
    if (action == QLatin1String("delete") || action == QLatin1String("remove"))
        return ServerInstaller::deleteServer(store, id, m_args.boolValue(QStringLiteral("purge"), false));

    return Result::fail(QStringLiteral("UNKNOWN_ACTION"),
                        QStringLiteral("未知 server 子命令 '%1'").arg(action));
}

Result CommandRouter::cmdConfig()
{
    ServerStore store;
    const QString action = m_args.positional(1, QStringLiteral("schema"));
    const QString id = m_args.value(QStringLiteral("id"), m_args.positional(2));

    if (action == QLatin1String("schema")) {
        return Result::ok(QJsonObject {
            {QStringLiteral("groups"), QJsonArray::fromStringList(ConfigSchema::groups())},
            {QStringLiteral("fields"), ConfigSchema::toJson()},
            {QStringLiteral("rawFile"), QStringLiteral("server.properties")},
        });
    }

    const ServerRecord record = store.get(id);
    if (record.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("服务器 %1 不存在").arg(id));

    if (action == QLatin1String("read")) {
        QJsonObject data;
        data.insert(QStringLiteral("id"), id);
        data.insert(QStringLiteral("values"), Json::fromMap(ConfigManager::readProperties(record)));
        data.insert(QStringLiteral("raw"), ConfigManager::readRaw(record));
        data.insert(QStringLiteral("path"), record.propertiesPath());
        data.insert(QStringLiteral("exists"), QFileInfo::exists(record.propertiesPath()));
        return Result::ok(data);
    }
    if (action == QLatin1String("validate")) {
        const QString text = m_args.has(QStringLiteral("text"))
                                 ? m_args.value(QStringLiteral("text"))
                                 : ConfigManager::readRaw(record);
        const QStringList issues = ConfigManager::validate(text);
        return Result::ok(QJsonObject {{QStringLiteral("issues"), QJsonArray::fromStringList(issues)},
                                       {QStringLiteral("ok"), issues.isEmpty()}});
    }
    if (action == QLatin1String("apply")) {
        QJsonObject values = m_args.jsonObject(QStringLiteral("values"));
        if (values.isEmpty() && m_args.has(QStringLiteral("values-file"))) {
            QFile file(m_args.value(QStringLiteral("values-file")));
            if (!file.open(QIODevice::ReadOnly)) {
                return Result::fail(QStringLiteral("IO_ERROR"),
                                    QStringLiteral("无法读取 --values-file"),
                                    m_args.value(QStringLiteral("values-file")));
            }
            bool ok = false;
            values = Json::parseObject(QString::fromUtf8(file.readAll()), &ok);
            if (!ok) {
                return Result::fail(QStringLiteral("INVALID_ARGUMENT"),
                                    QStringLiteral("--values-file 不是合法的 JSON 对象"),
                                    m_args.value(QStringLiteral("values-file")));
            }
        }
        if (values.isEmpty())
            return Result::fail(QStringLiteral("INVALID_ARGUMENT"),
                                QStringLiteral("缺少 --values 或 --values-file"));
        QMap<QString, QString> map;
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            QString value;
            if (it.value().isBool())
                value = it.value().toBool() ? QStringLiteral("true") : QStringLiteral("false");
            else if (it.value().isDouble())
                value = QString::number(it.value().toDouble(), 'g', 12);
            else
                value = it.value().toString();
            map.insert(it.key(), value);
        }
        QVector<ConfigDiffEntry> changes;
        QString error;
        if (!ConfigManager::applyValues(record, map, QStringLiteral("quick"),
                                        m_args.value(QStringLiteral("note")), &changes, &error)) {
            return Result::fail(QStringLiteral("CONFIG_WRITE_FAILED"),
                                QStringLiteral("写入配置失败"), error);
        }
        QJsonArray changeArray;
        for (const ConfigDiffEntry &entry : changes)
            changeArray.append(entry.toJson());
        QJsonObject data;
        data.insert(QStringLiteral("changes"), changeArray);
        data.insert(QStringLiteral("count"), changeArray.size());
        data.insert(QStringLiteral("raw"), ConfigManager::readRaw(record));
        data.insert(QStringLiteral("values"), Json::fromMap(ConfigManager::readProperties(record)));
        if (!changes.isEmpty())
            data.insert(QStringLiteral("backupId"),
                        ConfigManager::listBackups(record).isEmpty()
                            ? QString()
                            : ConfigManager::listBackups(record).last().id);
        Result result = Result::ok(data);
        if (!changes.isEmpty())
            result.warn(QStringLiteral("修改前已自动备份，可随时回滚"));
        return result;
    }
    if (action == QLatin1String("raw")) {
        QString text = m_args.value(QStringLiteral("text"));
        if (text.isEmpty() && m_args.has(QStringLiteral("file"))) {
            QFile file(m_args.value(QStringLiteral("file")));
            if (!file.open(QIODevice::ReadOnly))
                return Result::fail(QStringLiteral("IO_ERROR"), QStringLiteral("无法读取输入文件"));
            text = QString::fromUtf8(file.readAll());
        }
        if (text.isEmpty())
            return Result::fail(QStringLiteral("INVALID_ARGUMENT"),
                                QStringLiteral("缺少 --text 或 --file"));
        QVector<ConfigDiffEntry> changes;
        QString error;
        if (!ConfigManager::applyRaw(record, text, m_args.value(QStringLiteral("note")), &changes, &error))
            return Result::fail(QStringLiteral("CONFIG_WRITE_FAILED"), QStringLiteral("保存失败"), error);
        QJsonArray changeArray;
        for (const ConfigDiffEntry &entry : changes)
            changeArray.append(entry.toJson());
        Result result = Result::ok(QJsonObject {{QStringLiteral("changes"), changeArray},
                                                {QStringLiteral("count"), changeArray.size()},
                                                {QStringLiteral("raw"), ConfigManager::readRaw(record)}});
        result.warn(QStringLiteral("专家模式内容已保存，旧版本已备份"));
        return result;
    }
    if (action == QLatin1String("backups")) {
        QJsonArray array;
        for (const ConfigBackup &backup : ConfigManager::listBackups(record)) {
            QJsonObject object = backup.toJson();
            object.insert(QStringLiteral("changeCount"), backup.changes.size());
            object.insert(QStringLiteral("preview"), ConfigManager::readRaw(backup.file).left(4000));
            array.append(object);
        }
        return Result::ok(QJsonObject {{QStringLiteral("backups"), array},
                                       {QStringLiteral("count"), array.size()}});
    }
    if (action == QLatin1String("rollback")) {
        const QString backupId = m_args.value(QStringLiteral("backup"), m_args.positional(3));
        if (backupId.isEmpty())
            return Result::fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("缺少 --backup"));
        QString error;
        if (!ConfigManager::rollback(record, backupId, &error))
            return Result::fail(QStringLiteral("ROLLBACK_FAILED"), QStringLiteral("回滚失败"), error);
        return Result::ok(QJsonObject {{QStringLiteral("restored"), backupId},
                                       {QStringLiteral("raw"), ConfigManager::readRaw(record)},
                                       {QStringLiteral("values"),
                                        Json::fromMap(ConfigManager::readProperties(record))}});
    }
    return Result::fail(QStringLiteral("UNKNOWN_ACTION"),
                        QStringLiteral("未知 config 子命令 '%1'").arg(action));
}

Result CommandRouter::cmdBackup()
{
    ServerStore store;
    const QString action = m_args.positional(1, QStringLiteral("list"));
    const QString id = m_args.value(QStringLiteral("id"), m_args.positional(2));
    const ServerRecord record = store.get(id);

    if (record.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("服务器 %1 不存在").arg(id));

    if (action == QLatin1String("list")) {
        QJsonArray array;
        for (const WorldBackup &backup : BackupService::list(record))
            array.append(backup.toJson());
        const qint64 total = BackupService::totalSize(record);
        return Result::ok(QJsonObject {{QStringLiteral("backups"), array},
                                       {QStringLiteral("count"), array.size()},
                                       {QStringLiteral("totalSize"), double(total)},
                                       {QStringLiteral("totalSizeText"), StringUtil::humanBytes(total)},
                                       {QStringLiteral("schedule"), BackupService::scheduleInfo(record)}});
    }
    if (action == QLatin1String("create")) {
        return BackupService::create(store, id,
                                     m_args.value(QStringLiteral("note")),
                                     m_args.boolValue(QStringLiteral("automatic"), false),
                                     m_args.boolValue(QStringLiteral("plugins"), true),
                                     m_args.boolValue(QStringLiteral("save-first"), true));
    }
    if (action == QLatin1String("restore")) {
        const QString name = m_args.value(QStringLiteral("backup"), m_args.positional(3));
        if (name.isEmpty())
            return Result::fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("缺少 --backup"));
        const QString scope = m_args.value(QStringLiteral("scope"), QStringLiteral("world"));
        Result result = BackupService::restore(store, id, name,
                                              m_args.boolValue(QStringLiteral("restart"), false),
                                              scope);
        if (result.isOk() && scope.compare(QLatin1String("all"), Qt::CaseInsensitive) != 0)
            result.warn(QStringLiteral("已恢复世界存档，插件与配置保持不变（如需完整回滚请加 --scope all）"));
        return result;
    }
    if (action == QLatin1String("remove")) {
        const QString name = m_args.value(QStringLiteral("backup"), m_args.positional(3));
        return BackupService::remove(record, name);
    }
    if (action == QLatin1String("schedule")) {
        QJsonObject schedule = m_args.jsonObject(QStringLiteral("schedule"));
        if (m_args.has(QStringLiteral("enabled")))
            schedule.insert(QStringLiteral("enabled"), m_args.boolValue(QStringLiteral("enabled")));
        if (m_args.has(QStringLiteral("interval")))
            schedule.insert(QStringLiteral("intervalMinutes"),
                            m_args.intValue(QStringLiteral("interval"), 180));
        if (m_args.has(QStringLiteral("keep")))
            schedule.insert(QStringLiteral("keep"), m_args.intValue(QStringLiteral("keep"), 10));
        if (m_args.has(QStringLiteral("plugins")))
            schedule.insert(QStringLiteral("includePlugins"), m_args.boolValue(QStringLiteral("plugins")));
        if (m_args.has(QStringLiteral("save-first")))
            schedule.insert(QStringLiteral("saveBeforeBackup"),
                            m_args.boolValue(QStringLiteral("save-first")));
        return BackupService::setSchedule(store, id, schedule);
    }
    return Result::fail(QStringLiteral("UNKNOWN_ACTION"),
                        QStringLiteral("未知 backup 子命令 '%1'").arg(action));
}

Result CommandRouter::cmdPlugin()
{
    ServerStore store;
    const QString action = m_args.positional(1, QStringLiteral("search"));
    const QString id = m_args.value(QStringLiteral("id"), m_args.positional(2));

    if (action == QLatin1String("sources")) {
        return Result::ok(QJsonObject {
            {QStringLiteral("sources"), QJsonArray::fromStringList(PluginManager::sources())},
        });
    }

    const ServerRecord record = store.get(id);
    if (record.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("服务器 %1 不存在").arg(id));

    if (action == QLatin1String("search")) {
        const QString source = m_args.value(QStringLiteral("source"), QStringLiteral("modrinth"));
        QString error;
        const QVector<PluginHit> hits = PluginManager::search(source,
                                                             m_args.value(QStringLiteral("query")),
                                                             record,
                                                             m_args.intValue(QStringLiteral("limit"), 20),
                                                             &error);
        if (hits.isEmpty() && !error.isEmpty())
            return Result::fail(QStringLiteral("PLUGIN_SEARCH_FAILED"),
                                QStringLiteral("插件搜索失败"), error);
        QJsonArray array;
        for (const PluginHit &hit : hits)
            array.append(hit.toJson());
        Result result = Result::ok(QJsonObject {{QStringLiteral("source"), source},
                                                {QStringLiteral("count"), array.size()},
                                                {QStringLiteral("results"), array}});
        if (!PluginManager::supportsPlugins(record))
            result.warn(QStringLiteral("%1 服务端不支持 Bukkit 插件").arg(record.type));
        return result;
    }
    if (action == QLatin1String("list")) {
        QJsonArray array;
        for (const InstalledPlugin &plugin : PluginManager::installed(record))
            array.append(plugin.toJson());
        return Result::ok(QJsonObject {{QStringLiteral("plugins"), array},
                                       {QStringLiteral("count"), array.size()},
                                       {QStringLiteral("path"), record.pluginDir()}});
    }
    if (action == QLatin1String("install")) {
        PluginHit hit;
        hit.source = m_args.value(QStringLiteral("source"), QStringLiteral("modrinth"));
        hit.slug = m_args.value(QStringLiteral("slug"));
        hit.id = m_args.value(QStringLiteral("project"), hit.slug);
        hit.title = m_args.value(QStringLiteral("title"), hit.slug);
        hit.downloadUrl = m_args.value(QStringLiteral("url"));
        hit.fileName = m_args.value(QStringLiteral("file"));
        if (hit.slug.isEmpty() && hit.downloadUrl.isEmpty())
            return Result::fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("缺少 --slug 或 --url"));
        return PluginManager::install(record, hit);
    }
    if (action == QLatin1String("toggle") || action == QLatin1String("enable")
        || action == QLatin1String("disable")) {
        const QString file = m_args.value(QStringLiteral("file"), m_args.positional(3));
        if (file.isEmpty())
            return Result::fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("缺少 --file"));
        bool enabled = action == QLatin1String("enable");
        if (m_args.has(QStringLiteral("enabled")))
            enabled = m_args.boolValue(QStringLiteral("enabled"));
        return PluginManager::setEnabled(record, file, enabled);
    }
    if (action == QLatin1String("remove")) {
        const QString file = m_args.value(QStringLiteral("file"), m_args.positional(3));
        return PluginManager::remove(record, file);
    }
    return Result::fail(QStringLiteral("UNKNOWN_ACTION"),
                        QStringLiteral("未知 plugin 子命令 '%1'").arg(action));
}

Result CommandRouter::cmdSchedule()
{
    const QString action = m_args.positional(1, QStringLiteral("tick"));
    if (action == QLatin1String("tick")) {
        const Scheduler::TickReport report = Scheduler::tick(m_args.boolValue(QStringLiteral("verbose"), false));
        return Result::ok(report.toJson());
    }
    if (action == QLatin1String("status")) {
        ServerStore store;
        QJsonArray array;
        const QDateTime now = QDateTime::currentDateTime();
        for (const ServerRecord &record : store.all()) {
            QDateTime nextDue;
            const bool due = Scheduler::isDue(record, now, &nextDue);
            array.append(QJsonObject {
                {QStringLiteral("id"), record.id},
                {QStringLiteral("name"), record.name},
                {QStringLiteral("enabled"), Json::boolean(record.backupSchedule,
                                                          QStringLiteral("enabled"), false)},
                {QStringLiteral("due"), due},
                {QStringLiteral("nextDue"), nextDue.toString(Qt::ISODate)},
                {QStringLiteral("schedule"), record.backupSchedule},
            });
        }
        return Result::ok(QJsonObject {{QStringLiteral("servers"), array},
                                       {QStringLiteral("now"), now.toString(Qt::ISODate)}});
    }
    return Result::fail(QStringLiteral("UNKNOWN_ACTION"),
                        QStringLiteral("未知 schedule 子命令 '%1'").arg(action));
}

Result CommandRouter::cmdSettings()
{
    const QString action = m_args.positional(1, QStringLiteral("get"));
    if (action == QLatin1String("get")) {
        QJsonObject object = Json::readObjectFile(AppPaths::settingsFile());
        object.insert(QStringLiteral("home"), AppPaths::root());
        object.insert(QStringLiteral("registry"), AppPaths::registryFile());
        return Result::ok(object);
    }
    if (action == QLatin1String("set")) {
        QJsonObject object = Json::readObjectFile(AppPaths::settingsFile());
        const QJsonObject values = m_args.jsonObject(QStringLiteral("values"));
        for (auto it = values.constBegin(); it != values.constEnd(); ++it)
            object.insert(it.key(), it.value());
        if (m_args.has(QStringLiteral("theme")))
            object.insert(QStringLiteral("theme"), m_args.value(QStringLiteral("theme")));
        if (m_args.has(QStringLiteral("animations")))
            object.insert(QStringLiteral("animations"), m_args.boolValue(QStringLiteral("animations")));
        QString error;
        if (!Json::writeObjectFile(AppPaths::settingsFile(), object, &error))
            return Result::fail(QStringLiteral("IO_ERROR"), QStringLiteral("保存设置失败"), error);
        return Result::ok(object);
    }
    return Result::fail(QStringLiteral("UNKNOWN_ACTION"),
                        QStringLiteral("未知 settings 子命令 '%1'").arg(action));
}

} // namespace mcsm
