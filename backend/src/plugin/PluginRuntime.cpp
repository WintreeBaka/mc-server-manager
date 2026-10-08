#include "plugin/PluginRuntime.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpSocket>
#include <QUrl>

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/ProcessRunner.h"
#include "core/StringUtil.h"

namespace mcsm {
namespace {

QString serviceFileName()
{
    return QStringLiteral("service.json");
}

bool processAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
#ifdef Q_OS_WIN
    const ProcessResult result =
        ProcessRunner::run(QStringLiteral("tasklist"),
                           {QStringLiteral("/FI"), QStringLiteral("PID eq %1").arg(pid),
                            QStringLiteral("/NH")},
                           20000);
    return result.stdOut.contains(QString::number(pid));
#else
    return QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid));
#endif
}

bool portOpen(quint16 port)
{
    if (port == 0)
        return false;
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), port);
    const bool connected = socket.waitForConnected(500);
    socket.abort();
    return connected;
}

QStringList matchingHooks(const PluginManifest &manifest)
{
    return manifest.hooks;
}

bool hookMatches(const QStringList &declared, const QString &hook)
{
    for (const QString &entry : declared) {
        const QString normalized = entry.trimmed().toLower();
        if (normalized == hook.toLower())
            return true;
        if (normalized.endsWith(QLatin1String(".*"))
            && hook.toLower().startsWith(normalized.left(normalized.size() - 1)))
            return true;
    }
    return false;
}

/// Enabled plugins that can actually run (have a backend entry).
QVector<PluginManifest> runnablePlugins()
{
    QVector<PluginManifest> result;
    for (const PluginManifest &manifest : PackageManager::installed(false)) {
        if (manifest.hasBackend() && !manifest.backendEntry().isEmpty())
            result.append(manifest);
    }
    return result;
}

} // namespace

QJsonObject PluginCallOutcome::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("ok"), ok);
    object.insert(QStringLiteral("plugin"), pluginId);
    object.insert(QStringLiteral("method"), method);
    object.insert(QStringLiteral("data"), data);
    object.insert(QStringLiteral("patch"), patch);
    object.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
    object.insert(QStringLiteral("log"), QJsonArray::fromStringList(log));
    object.insert(QStringLiteral("exitCode"), exitCode);
    object.insert(QStringLiteral("elapsedMs"), double(elapsedMs));
    if (!ok) {
        object.insert(QStringLiteral("error"),
                      QJsonObject {{QStringLiteral("code"), code},
                                   {QStringLiteral("message"), message},
                                   {QStringLiteral("detail"), detail}});
    }
    return object;
}

// ------------------------------------------------------------- launching -----

bool PluginRuntime::resolveLaunch(const PluginManifest &manifest,
                                  const QString &entryOverride,
                                  Launch *launch,
                                  QString *error)
{
    const QString entry = entryOverride.isEmpty() ? manifest.backendEntry() : entryOverride;
    if (entry.isEmpty()) {
        if (error)
            *error = QStringLiteral("插件 %1 没有后端入口").arg(manifest.id);
        return false;
    }
    const QString script = QDir(manifest.path).filePath(entry);
    if (!QFileInfo::exists(script)) {
        if (error)
            *error = QStringLiteral("入口文件不存在：%1").arg(script);
        return false;
    }

    QString runtime = Json::str(manifest.backend, QStringLiteral("runtime"),
                               QStringLiteral("auto")).toLower();
    const QString suffix = QFileInfo(script).suffix().toLower();
    if (runtime == QLatin1String("auto") || runtime.isEmpty()) {
        if (suffix == QLatin1String("js") || suffix == QLatin1String("mjs")
            || suffix == QLatin1String("cjs"))
            runtime = QStringLiteral("node");
        else if (suffix == QLatin1String("py"))
            runtime = QStringLiteral("python");
        else if (suffix == QLatin1String("jar"))
            runtime = QStringLiteral("java");
        else if (suffix == QLatin1String("exe") || suffix == QLatin1String("cmd")
                 || suffix == QLatin1String("bat") || suffix == QLatin1String("ps1"))
            runtime = QStringLiteral("exec");
        else
            runtime = QStringLiteral("exec");
    }

    Launch resolved;
    resolved.workingDirectory = manifest.path;
    if (runtime == QLatin1String("node")) {
        const QString node = ProcessRunner::resolveProgram({QStringLiteral("node")});
        if (node.isEmpty()) {
            if (error)
                *error = QStringLiteral("插件需要 Node.js 运行时，但未在 PATH 中找到 node");
            return false;
        }
        resolved.program = node;
        resolved.arguments << script;
        resolved.label = QStringLiteral("node");
    } else if (runtime == QLatin1String("python")) {
        const QString python = ProcessRunner::resolveProgram(
            {QStringLiteral("python"), QStringLiteral("python3"), QStringLiteral("py")});
        if (python.isEmpty()) {
            if (error)
                *error = QStringLiteral("插件需要 Python 运行时，但未在 PATH 中找到 python");
            return false;
        }
        resolved.program = python;
        resolved.arguments << script;
        resolved.label = QStringLiteral("python");
    } else if (runtime == QLatin1String("java")) {
        const QString java = ProcessRunner::resolveProgram({QStringLiteral("java")});
        if (java.isEmpty()) {
            if (error)
                *error = QStringLiteral("插件需要 Java 运行时，但未在 PATH 中找到 java");
            return false;
        }
        resolved.program = java;
        resolved.arguments << QStringLiteral("-jar") << script;
        resolved.label = QStringLiteral("java");
    } else {
        resolved.program = script;
        resolved.label = QStringLiteral("exec");
    }
    resolved.arguments << Json::toList(manifest.backend.value(QStringLiteral("args")));
    *launch = resolved;
    return true;
}

QString PluginRuntime::describeRuntime(const PluginManifest &manifest, QString *error)
{
    Launch launch;
    if (!resolveLaunch(manifest, QString(), &launch, error))
        return QString();
    return QStringLiteral("%1 %2").arg(launch.program, StringUtil::joinArgs(launch.arguments));
}

// ------------------------------------------------------------------ call -----

PluginCallOutcome PluginRuntime::call(const PluginManifest &manifest,
                                      const QString &method,
                                      const QJsonObject &params,
                                      int timeoutMs)
{
    PluginCallOutcome outcome;
    outcome.pluginId = manifest.id;
    outcome.method = method;

    Launch launch;
    QString launchError;
    if (!resolveLaunch(manifest, QString(), &launch, &launchError)) {
        outcome.code = QStringLiteral("PLUGIN_RUNTIME_UNAVAILABLE");
        outcome.message = QStringLiteral("无法启动插件 %1").arg(manifest.id);
        outcome.detail = launchError;
        return outcome;
    }

    QJsonObject request;
    request.insert(QStringLiteral("apiVersion"), PackageManager::apiVersion());
    request.insert(QStringLiteral("plugin"), manifest.id);
    request.insert(QStringLiteral("method"), method);
    request.insert(QStringLiteral("params"), params);
    request.insert(QStringLiteral("caller"), QStringLiteral("mcsm-cli"));
    request.insert(QStringLiteral("pluginsDir"), AppPaths::pluginsDir());
    request.insert(QStringLiteral("dataDir"), AppPaths::pluginDataDir(manifest.id));
    request.insert(QStringLiteral("requestedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));

    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n";
    const int effectiveTimeout =
        timeoutMs > 0 ? timeoutMs : Json::integer(manifest.backend, QStringLiteral("timeoutMs"), 30000);

    QElapsedTimer timer;
    timer.start();
    const ProcessResult result = ProcessRunner::run(launch.program, launch.arguments, effectiveTimeout,
                                                    payload, launch.workingDirectory);
    outcome.elapsedMs = timer.elapsed();
    outcome.exitCode = result.exitCode;

    const QStringList lines = StringUtil::splitLines(result.stdOut);
    QJsonObject response;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        bool parsed = false;
        const QJsonObject object = Json::parseObject(trimmed, &parsed);
        if (!parsed)
            continue;
        if (object.contains(QStringLiteral("type"))) {
            const QString type = object.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("log"))
                outcome.log << object.value(QStringLiteral("line")).toString();
            continue;
        }
        if (object.contains(QStringLiteral("ok")))
            response = object;
    }

    if (!result.started) {
        outcome.code = QStringLiteral("PLUGIN_RUNTIME_UNAVAILABLE");
        outcome.message = QStringLiteral("插件进程启动失败");
        outcome.detail = result.errorText();
        return outcome;
    }
    if (result.timedOut) {
        outcome.code = QStringLiteral("PLUGIN_TIMEOUT");
        outcome.message = QStringLiteral("插件 %1 调用超时").arg(manifest.id);
        outcome.detail = QStringLiteral("method=%1 timeout=%2ms").arg(method).arg(effectiveTimeout);
        return outcome;
    }
    if (response.isEmpty()) {
        outcome.code = QStringLiteral("PLUGIN_PROTOCOL_ERROR");
        outcome.message = QStringLiteral("插件 %1 没有返回合法的 JSON 结果").arg(manifest.id);
        outcome.detail = result.stdErr.trimmed().isEmpty() ? StringUtil::ellipsize(result.stdOut, 600)
                                                           : result.errorText();
        return outcome;
    }

    outcome.ok = response.value(QStringLiteral("ok")).toBool(false);
    outcome.data = response.value(QStringLiteral("data")).toObject();
    outcome.patch = response.value(QStringLiteral("patch")).toObject();
    outcome.warnings = Json::toList(response.value(QStringLiteral("warnings")));
    outcome.log += Json::toList(response.value(QStringLiteral("log")));
    if (!outcome.ok) {
        const QJsonObject error = response.value(QStringLiteral("error")).toObject();
        outcome.code = Json::str(error, QStringLiteral("code"), QStringLiteral("PLUGIN_ERROR"));
        outcome.message = Json::str(error, QStringLiteral("message"),
                                   QStringLiteral("插件 %1 返回错误").arg(manifest.id));
        outcome.detail = Json::str(error, QStringLiteral("detail"));
    }
    if (!result.stdErr.trimmed().isEmpty())
        outcome.log << result.stdErr.trimmed();
    return outcome;
}

QVector<PluginCallOutcome> PluginRuntime::callHook(const QString &hook,
                                                   const QJsonObject &payload,
                                                   int timeoutMs)
{
    QVector<PluginCallOutcome> outcomes;
    for (const PluginManifest &manifest : runnablePlugins()) {
        if (!hookMatches(matchingHooks(manifest), hook))
            continue;
        outcomes.append(call(manifest, hook, payload, timeoutMs));
    }
    return outcomes;
}

Result PluginRuntime::notify(const QString &hook, const QJsonObject &payload)
{
    const QVector<PluginCallOutcome> outcomes = callHook(hook, payload);
    QJsonArray array;
    QStringList warnings;
    for (const PluginCallOutcome &outcome : outcomes) {
        array.append(outcome.toJson());
        if (!outcome.ok) {
            warnings << QStringLiteral("插件 %1 的 %2 钩子执行失败：%3")
                            .arg(outcome.pluginId, hook,
                                 outcome.detail.isEmpty() ? outcome.message : outcome.detail);
            Logger::warn(QStringLiteral("plugin"),
                         QStringLiteral("hook %1 on %2 failed: %3")
                             .arg(hook, outcome.pluginId, outcome.message));
        }
        for (const QString &warning : outcome.warnings)
            warnings << QStringLiteral("[%1] %2").arg(outcome.pluginId, warning);
    }
    Result result = Result::ok(QJsonObject {{QStringLiteral("hook"), hook},
                                            {QStringLiteral("executed"), array.size()},
                                            {QStringLiteral("results"), array}});
    for (const QString &warning : warnings)
        result.warn(warning);
    return result;
}

QJsonObject PluginRuntime::serverPayload(const ServerRecord &record, const QJsonObject &extra)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("server"), record.toJson());
    payload.insert(QStringLiteral("serverDir"), record.dir);
    payload.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODate));
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        payload.insert(it.key(), it.value());
    return payload;
}

// --------------------------------------------------------- start patching ----

QJsonObject PluginRuntime::readStartPatch(const ServerRecord &record)
{
    if (record.id.isEmpty())
        return QJsonObject();
    const QString path = AppPaths::serverStartPatchFile(record.id);
    if (!QFileInfo::exists(path))
        return QJsonObject();
    return Json::readObjectFile(path);
}

Result PluginRuntime::refreshStartPatch(const ServerRecord &record)
{
    const QString path = AppPaths::serverStartPatchFile(record.id);

    QVector<PluginManifest> candidates;
    for (const PluginManifest &manifest : runnablePlugins()) {
        if (hookMatches(matchingHooks(manifest), QStringLiteral("server.beforeStart")))
            candidates.append(manifest);
    }
    if (candidates.isEmpty()) {
        QFile::remove(path);
        return Result::ok(QJsonObject {{QStringLiteral("applied"), QJsonArray()}});
    }

    QStringList jvmArgs;
    QStringList dockerArgs;
    QJsonObject env;
    QStringList notes;
    QStringList applied;
    QJsonArray results;

    const QJsonObject payload = serverPayload(record);
    for (const PluginManifest &manifest : candidates) {
        const PluginCallOutcome outcome = call(manifest, QStringLiteral("server.beforeStart"), payload);
        results.append(outcome.toJson());
        if (!outcome.ok) {
            notes << QStringLiteral("插件 %1 调优失败：%2")
                         .arg(manifest.id,
                              outcome.detail.isEmpty() ? outcome.message : outcome.detail);
            continue;
        }
        if (outcome.patch.value(QStringLiteral("cancel")).toBool(false)) {
            return Result::fail(
                QStringLiteral("PLUGIN_CANCELLED_START"),
                QStringLiteral("插件 %1 阻止了启动").arg(manifest.id),
                outcome.patch.value(QStringLiteral("reason")).toString(outcome.message));
        }
        const QStringList args = Json::toList(outcome.patch.value(QStringLiteral("jvmArgs")));
        for (const QString &arg : args) {
            if (!arg.trimmed().isEmpty() && !jvmArgs.contains(arg))
                jvmArgs << arg;
        }
        const QStringList dargs = Json::toList(outcome.patch.value(QStringLiteral("dockerArgs")));
        for (const QString &arg : dargs)
            dockerArgs << arg;
        const QJsonObject patchEnv = outcome.patch.value(QStringLiteral("env")).toObject();
        for (auto it = patchEnv.constBegin(); it != patchEnv.constEnd(); ++it)
            env.insert(it.key(), it.value());
        const QString note = outcome.patch.value(QStringLiteral("note")).toString();
        if (!note.isEmpty())
            notes << QStringLiteral("[%1] %2").arg(manifest.id, note);
        applied << manifest.id;
    }

    if (applied.isEmpty()) {
        QFile::remove(path);
        Result result = Result::ok(QJsonObject {{QStringLiteral("applied"), QJsonArray()},
                                                {QStringLiteral("results"), results}});
        for (const QString &note : notes)
            result.warn(note);
        return result;
    }

    QJsonObject patch;
    patch.insert(QStringLiteral("jvmArgs"), QJsonArray::fromStringList(jvmArgs));
    patch.insert(QStringLiteral("dockerArgs"), QJsonArray::fromStringList(dockerArgs));
    patch.insert(QStringLiteral("env"), env);
    patch.insert(QStringLiteral("notes"), QJsonArray::fromStringList(notes));
    patch.insert(QStringLiteral("appliedPlugins"), QJsonArray::fromStringList(applied));
    patch.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    patch.insert(QStringLiteral("server"), record.id);

    QString writeError;
    if (!Json::writeObjectFile(path, patch, &writeError)) {
        return Result::fail(QStringLiteral("IO_ERROR"),
                            QStringLiteral("无法写入插件启动补丁"), writeError);
    }

    Result result = Result::ok(QJsonObject {{QStringLiteral("applied"), QJsonArray::fromStringList(applied)},
                                            {QStringLiteral("jvmArgs"), QJsonArray::fromStringList(jvmArgs)},
                                            {QStringLiteral("dockerArgs"), QJsonArray::fromStringList(dockerArgs)},
                                            {QStringLiteral("env"), env},
                                            {QStringLiteral("notes"), QJsonArray::fromStringList(notes)},
                                            {QStringLiteral("path"), path},
                                            {QStringLiteral("results"), results}});
    for (const QString &note : notes)
        result.warn(note);
    return result;
}

// --------------------------------------------------------------- services ----

QJsonObject PluginRuntime::serviceFile(const QString &pluginId)
{
    return Json::readObjectFile(
        QDir(AppPaths::pluginDataDir(pluginId)).filePath(serviceFileName()));
}

bool PluginRuntime::writeServiceFile(const QString &pluginId, const QJsonObject &object)
{
    const QString path = QDir(AppPaths::pluginDataDir(pluginId)).filePath(serviceFileName());
    if (object.isEmpty())
        return QFile::remove(path);
    QString error;
    return Json::writeObjectFile(path, object, &error);
}

Result PluginRuntime::serviceStart(const QString &pluginId)
{
    const PluginManifest manifest = PackageManager::find(pluginId);
    if (manifest.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"),
                            QStringLiteral("插件 %1 未安装").arg(pluginId));
    const QString service = Json::str(manifest.web, QStringLiteral("service"));
    if (service.isEmpty())
        return Result::fail(QStringLiteral("PLUGIN_NO_SERVICE"),
                            QStringLiteral("插件 %1 没有声明可启动的服务").arg(manifest.id),
                            QStringLiteral("在 plugin.json 的 web.service 中指定入口脚本"));

    const QJsonObject existing = serviceFile(manifest.id);
    const qint64 existingPid = qint64(existing.value(QStringLiteral("pid")).toDouble());
    if (existingPid > 0 && processAlive(existingPid))
        return Result::ok(QJsonObject {{QStringLiteral("alreadyRunning"), true},
                                       {QStringLiteral("pid"), double(existingPid)},
                                       {QStringLiteral("url"),
                                        existing.value(QStringLiteral("url")).toString()}});

    Launch launch;
    QString launchError;
    if (!resolveLaunch(manifest, service, &launch, &launchError))
        return Result::fail(QStringLiteral("PLUGIN_RUNTIME_UNAVAILABLE"),
                            QStringLiteral("无法启动插件服务"), launchError);

    const int port = manifest.webPort();
    QProcess process;
    process.setProgram(launch.program);
    process.setArguments(launch.arguments);
    process.setWorkingDirectory(launch.workingDirectory);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("MCSM_PLUGIN_ID"), manifest.id);
    environment.insert(QStringLiteral("MCSM_PLUGIN_DIR"), manifest.path);
    environment.insert(QStringLiteral("MCSM_PLUGIN_DATA"), AppPaths::pluginDataDir(manifest.id));
    if (port > 0)
        environment.insert(QStringLiteral("MCSM_PLUGIN_PORT"), QString::number(port));
    process.setProcessEnvironment(environment);

    qint64 pid = 0;
    if (!process.startDetached(&pid)) {
        return Result::fail(QStringLiteral("PLUGIN_RUNTIME_UNAVAILABLE"),
                            QStringLiteral("插件服务启动失败"), process.errorString());
    }

    QJsonObject record;
    record.insert(QStringLiteral("id"), manifest.id);
    record.insert(QStringLiteral("pid"), double(pid));
    record.insert(QStringLiteral("port"), port);
    record.insert(QStringLiteral("entry"), service);
    record.insert(QStringLiteral("url"), port > 0
                                            ? QStringLiteral("http://127.0.0.1:%1/").arg(port)
                                            : QString());
    record.insert(QStringLiteral("startedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    writeServiceFile(manifest.id, record);

    QJsonObject data = record;
    data.insert(QStringLiteral("command"), QStringLiteral("%1 %2")
                                              .arg(launch.program,
                                                   StringUtil::joinArgs(launch.arguments)));
    return Result::ok(data);
}

Result PluginRuntime::serviceStop(const QString &pluginId)
{
    const PluginManifest manifest = PackageManager::find(pluginId);
    if (manifest.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"),
                            QStringLiteral("插件 %1 未安装").arg(pluginId));
    const QJsonObject record = serviceFile(manifest.id);
    const qint64 pid = qint64(record.value(QStringLiteral("pid")).toDouble());
    bool stopped = false;
    if (pid > 0 && processAlive(pid)) {
        const ProcessResult result =
            ProcessRunner::run(QStringLiteral("taskkill"),
                               {QStringLiteral("/PID"), QString::number(pid),
                                QStringLiteral("/T"), QStringLiteral("/F")},
                               30000);
        stopped = result.ok() || !processAlive(pid);
    }
    writeServiceFile(manifest.id, QJsonObject());
    return Result::ok(QJsonObject {{QStringLiteral("id"), manifest.id},
                                   {QStringLiteral("stopped"), stopped},
                                   {QStringLiteral("pid"), double(pid)}});
}

Result PluginRuntime::serviceStatus(const QString &pluginId)
{
    const PluginManifest manifest = PackageManager::find(pluginId);
    if (manifest.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"),
                            QStringLiteral("插件 %1 未安装").arg(pluginId));
    const QJsonObject record = serviceFile(manifest.id);
    const qint64 pid = qint64(record.value(QStringLiteral("pid")).toDouble());
    const int port = record.value(QStringLiteral("port")).toInt();
    const bool alive = processAlive(pid);
    const bool listening = portOpen(quint16(port));
    QJsonObject data = record;
    data.insert(QStringLiteral("id"), manifest.id);
    data.insert(QStringLiteral("name"), manifest.name);
    data.insert(QStringLiteral("running"), alive || listening);
    data.insert(QStringLiteral("listening"), listening);
    data.insert(QStringLiteral("declared"), !Json::str(manifest.web, QStringLiteral("service")).isEmpty());
    data.insert(QStringLiteral("hasPanel"), !manifest.webEntry().isEmpty());
    if (!manifest.webEntry().isEmpty())
        data.insert(QStringLiteral("panelPath"), QDir(manifest.path).filePath(manifest.webEntry()));
    return Result::ok(data);
}

} // namespace mcsm
