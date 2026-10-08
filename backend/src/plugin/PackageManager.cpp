#include "plugin/PackageManager.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#include "core/AppPaths.h"
#include "core/Archive.h"
#include "core/Logger.h"
#include "core/StringUtil.h"
#include "net/HttpClient.h"

namespace mcsm {
namespace {

const char *kPluginApiVersion = "1";

QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QStringList readStringList(const QJsonObject &object, const QString &key)
{
    return Json::toList(object.value(key));
}

/// True when the directory contains anything at all (used to detect empty
/// archives instead of reporting a confusing "missing plugin.json").
bool directoryHasEntries(const QString &path)
{
    QDir dir(path);
    return dir.exists() && !dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty();
}

} // namespace

// ------------------------------------------------------------- manifest ------

QString PluginManifest::frontendEntry() const
{
    return Json::str(frontend, QStringLiteral("entry"));
}

QString PluginManifest::backendEntry() const
{
    return Json::str(backend, QStringLiteral("entry"));
}

QString PluginManifest::webEntry() const
{
    return Json::str(web, QStringLiteral("entry"));
}

int PluginManifest::webPort() const
{
    return Json::integer(web, QStringLiteral("port"), 0);
}

QString PluginManifest::scopeName(PluginScope scope)
{
    switch (scope) {
    case PluginScope::Frontend:
        return QStringLiteral("frontend");
    case PluginScope::Backend:
        return QStringLiteral("backend");
    case PluginScope::Web:
        return QStringLiteral("web");
    case PluginScope::Global:
        return QStringLiteral("global");
    case PluginScope::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QString PluginManifest::scopeLabel(PluginScope scope)
{
    switch (scope) {
    case PluginScope::Frontend:
        return QStringLiteral("前端扩展");
    case PluginScope::Backend:
        return QStringLiteral("后端服务");
    case PluginScope::Web:
        return QStringLiteral("Web 支持库");
    case PluginScope::Global:
        return QStringLiteral("全局（前端 + 后端）");
    case PluginScope::Unknown:
        break;
    }
    return QStringLiteral("未知");
}

QJsonObject PluginManifest::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("version"), version);
    object.insert(QStringLiteral("apiVersion"), apiVersion);
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("author"), author);
    object.insert(QStringLiteral("homepage"), homepage);
    object.insert(QStringLiteral("license"), license);
    object.insert(QStringLiteral("scope"), scopeName(scope));
    object.insert(QStringLiteral("scopeLabel"), scopeLabel(scope));
    object.insert(QStringLiteral("scopeToken"), scopeToken);
    object.insert(QStringLiteral("parts"), QJsonArray::fromStringList(parts));
    object.insert(QStringLiteral("permissions"), QJsonArray::fromStringList(permissions));
    object.insert(QStringLiteral("hooks"), QJsonArray::fromStringList(hooks));
    object.insert(QStringLiteral("hasFrontend"), hasFrontend());
    object.insert(QStringLiteral("hasBackend"), hasBackend());
    object.insert(QStringLiteral("hasWeb"), hasWeb());
    object.insert(QStringLiteral("frontend"), frontend);
    object.insert(QStringLiteral("backend"), backend);
    object.insert(QStringLiteral("web"), web);
    object.insert(QStringLiteral("contributes"), contributes);
    object.insert(QStringLiteral("frontendEntry"), frontendEntry());
    object.insert(QStringLiteral("backendEntry"), backendEntry());
    object.insert(QStringLiteral("webEntry"), webEntry());
    object.insert(QStringLiteral("webPort"), webPort());
    object.insert(QStringLiteral("enabled"), enabled);
    object.insert(QStringLiteral("path"), path);
    object.insert(QStringLiteral("installedAt"), installedAt);
    object.insert(QStringLiteral("source"), source);
    object.insert(QStringLiteral("sourceUrl"), sourceUrl);
    object.insert(QStringLiteral("manifest"), raw);
    return object;
}

// ------------------------------------------------------------- helpers -------

QString PackageManager::apiVersion()
{
    return QString::fromLatin1(kPluginApiVersion);
}

QString PackageManager::normalizeId(const QString &raw)
{
    QString id = raw.trimmed();
    id.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (id.contains(QLatin1Char('/')))
        id = id.section(QLatin1Char('/'), -1);
    id.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")));
    while (id.startsWith(QLatin1Char('.')))
        id.remove(0, 1);
    return id;
}

PluginScope PackageManager::parseScope(const QString &token, const QStringList &parts)
{
    const QString normalized = token.trimmed().toLower();
    const auto infer = [&parts]() {
        const bool front = parts.contains(QStringLiteral("frontend"));
        const bool back = parts.contains(QStringLiteral("backend"));
        const bool web = parts.contains(QStringLiteral("web"));
        if (front && back)
            return PluginScope::Global;
        if (web && (front || back))
            return PluginScope::Global;
        if (front)
            return PluginScope::Frontend;
        if (back)
            return PluginScope::Backend;
        if (web)
            return PluginScope::Web;
        return PluginScope::Unknown;
    };
    if (normalized.isEmpty() || normalized == QLatin1String("auto"))
        return infer();
    if (normalized == QLatin1String("frontend") || normalized == QLatin1String("ui"))
        return PluginScope::Frontend;
    if (normalized == QLatin1String("backend") || normalized == QLatin1String("cli"))
        return PluginScope::Backend;
    if (normalized == QLatin1String("web") || normalized == QLatin1String("support"))
        return PluginScope::Web;
    if (normalized == QLatin1String("global") || normalized == QLatin1String("full"))
        return PluginScope::Global;
    return infer();
}

QString PackageManager::locatePluginRoot(const QString &directory)
{
    const QDir dir(directory);
    if (!dir.exists())
        return directory;
    if (QFileInfo::exists(dir.filePath(QStringLiteral("plugin.json"))))
        return QDir::cleanPath(directory);
    // archives created by "compress folder" tools usually wrap everything in a
    // single top level directory
    const QFileInfoList children =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &child : children) {
        if (QFileInfo::exists(QDir(child.absoluteFilePath()).filePath(QStringLiteral("plugin.json"))))
            return QDir::cleanPath(child.absoluteFilePath());
    }
    return QDir::cleanPath(directory);
}

// ------------------------------------------------------------ inspection -----

PluginValidation PackageManager::inspectDirectory(const QString &directory, QString *error)
{
    PluginValidation validation;
    const QString root = locatePluginRoot(directory);
    const QString manifestPath = QDir(root).filePath(QStringLiteral("plugin.json"));
    const auto finish = [&](const QString &message) {
        validation.ok = false;
        validation.errors << message;
        if (error)
            *error = message;
        return validation;
    };

    if (!QFileInfo::exists(manifestPath)) {
        if (!directoryHasEntries(root))
            return finish(QStringLiteral("压缩包是空的"));
        return finish(QStringLiteral("插件包缺少 plugin.json"));
    }

    bool parsed = false;
    const QJsonObject raw = Json::readObjectFile(manifestPath, &parsed);
    if (!parsed)
        return finish(QStringLiteral("plugin.json 不是合法的 JSON"));

    PluginManifest manifest;
    manifest.raw = raw;
    manifest.path = root;
    manifest.source = QStringLiteral("zip");

    manifest.id = normalizeId(Json::str(raw, QStringLiteral("id")));
    if (manifest.id.isEmpty())
        return finish(QStringLiteral("plugin.json 缺少合法的 id 字段"));
    manifest.name = Json::str(raw, QStringLiteral("name"), manifest.id);
    manifest.version = Json::str(raw, QStringLiteral("version"), QStringLiteral("1.0.0"));
    manifest.apiVersion = Json::str(raw, QStringLiteral("apiVersion"), apiVersion());
    manifest.description = Json::str(raw, QStringLiteral("description"));
    manifest.author = Json::str(raw, QStringLiteral("author"));
    manifest.homepage = Json::str(raw, QStringLiteral("homepage"));
    manifest.license = Json::str(raw, QStringLiteral("license"));
    manifest.permissions = readStringList(raw, QStringLiteral("permissions"));
    manifest.hooks = readStringList(raw, QStringLiteral("hooks"));
    manifest.frontend = raw.value(QStringLiteral("frontend")).toObject();
    manifest.backend = raw.value(QStringLiteral("backend")).toObject();
    manifest.web = raw.value(QStringLiteral("web")).toObject();
    manifest.contributes = raw.value(QStringLiteral("contributes")).toObject();

    const QString major = manifest.apiVersion.section(QLatin1Char('.'), 0, 0).trimmed();
    if (major != apiVersion())
        return finish(QStringLiteral("插件要求 apiVersion %1，当前管理器只支持 %2")
                          .arg(manifest.apiVersion, apiVersion()));

    const QDir dir(root);
    const bool frontendDir = dir.exists(QStringLiteral("frontend"));
    const bool backendDir = dir.exists(QStringLiteral("backend"));
    const bool webDir = dir.exists(QStringLiteral("web"));

    QString frontendEntry = Json::str(manifest.frontend, QStringLiteral("entry"));
    if (frontendEntry.isEmpty()) {
        const QStringList guesses = {QStringLiteral("frontend/index.js"),
                                     QStringLiteral("frontend/main.js")};
        for (const QString &guess : guesses) {
            if (QFileInfo::exists(dir.filePath(guess))) {
                frontendEntry = guess;
                break;
            }
        }
    }
    if (frontendEntry.isEmpty() && frontendDir)
        validation.warnings << QStringLiteral("frontend/ 目录存在但没有入口脚本（frontend/index.js）");
    if (!frontendEntry.isEmpty()) {
        if (!QFileInfo::exists(dir.filePath(frontendEntry)))
            return finish(QStringLiteral("前端入口文件不存在：%1").arg(frontendEntry));
        manifest.frontend.insert(QStringLiteral("entry"), frontendEntry);
    }

    QString backendEntry = Json::str(manifest.backend, QStringLiteral("entry"));
    if (backendEntry.isEmpty() && backendDir) {
        const QStringList guesses = {QStringLiteral("backend/index.js"),
                                     QStringLiteral("backend/main.py"),
                                     QStringLiteral("backend/index.py"),
                                     QStringLiteral("backend/main.exe"),
                                     QStringLiteral("backend/run.cmd")};
        for (const QString &guess : guesses) {
            if (QFileInfo::exists(dir.filePath(guess))) {
                backendEntry = guess;
                break;
            }
        }
    }
    if (backendEntry.isEmpty() && backendDir)
        validation.warnings << QStringLiteral("backend/ 目录存在但没有入口脚本（backend/index.js）");
    if (!backendEntry.isEmpty()) {
        if (!QFileInfo::exists(dir.filePath(backendEntry)))
            return finish(QStringLiteral("后端入口文件不存在：%1").arg(backendEntry));
        manifest.backend.insert(QStringLiteral("entry"), backendEntry);
    }

    QString webEntry = Json::str(manifest.web, QStringLiteral("entry"));
    if (webEntry.isEmpty() && webDir && QFileInfo::exists(dir.filePath(QStringLiteral("web/index.html"))))
        webEntry = QStringLiteral("web/index.html");
    if (!webEntry.isEmpty()) {
        if (!QFileInfo::exists(dir.filePath(webEntry)))
            return finish(QStringLiteral("Web 入口文件不存在：%1").arg(webEntry));
        manifest.web.insert(QStringLiteral("entry"), webEntry);
    }

    const int port = manifest.webPort();
    if (port != 0 && (port < 1024 || port > 65535)) {
        validation.warnings << QStringLiteral("web.port %1 超出范围，已忽略").arg(port);
        manifest.web.remove(QStringLiteral("port"));
    }

    QStringList parts;
    if (!frontendEntry.isEmpty())
        parts << QStringLiteral("frontend");
    if (!backendEntry.isEmpty())
        parts << QStringLiteral("backend");
    if (!webEntry.isEmpty() || !Json::str(manifest.web, QStringLiteral("service")).isEmpty())
        parts << QStringLiteral("web");
    manifest.parts = parts;

    manifest.scopeToken = Json::str(raw, QStringLiteral("scope"), QStringLiteral("auto"));
    manifest.scope = parseScope(manifest.scopeToken, parts);
    if (manifest.scope == PluginScope::Unknown)
        return finish(QStringLiteral("无法识别插件类型：请提供 frontend/ backend/ web/ 目录，或显式设置 scope"));

    const QString declared = manifest.scopeToken.trimmed().toLower();
    if (declared == QLatin1String("frontend") && !manifest.hasFrontend())
        return finish(QStringLiteral("scope 声明为 frontend，但缺少前端入口"));
    if (declared == QLatin1String("backend") && !manifest.hasBackend())
        return finish(QStringLiteral("scope 声明为 backend，但缺少后端入口"));
    if (declared == QLatin1String("global")
        && !(manifest.hasFrontend() && manifest.hasBackend()))
        validation.warnings << QStringLiteral("scope 声明为 global，但只检测到部分运行环境");

    if (manifest.hooks.isEmpty() && manifest.hasBackend())
        validation.warnings << QStringLiteral("后端插件没有声明 hooks，只有通过 API 调用的方法可用");
    if (manifest.license.isEmpty())
        validation.warnings << QStringLiteral("plugin.json 未声明 license");
    if (!QFileInfo::exists(dir.filePath(QStringLiteral("README.md"))))
        validation.warnings << QStringLiteral("建议随包提供 README.md");

    validation.manifest = manifest;
    validation.ok = true;
    if (error)
        error->clear();
    return validation;
}

// -------------------------------------------------------------- registry -----

QJsonObject PackageManager::readRegistry()
{
    QJsonObject registry = Json::readObjectFile(AppPaths::pluginRegistryFile());
    if (!registry.contains(QStringLiteral("plugins")))
        registry.insert(QStringLiteral("plugins"), QJsonArray());
    registry.insert(QStringLiteral("apiVersion"), apiVersion());
    return registry;
}

bool PackageManager::writeRegistry(const QJsonObject &registry, QString *error)
{
    QJsonObject object = registry;
    object.insert(QStringLiteral("apiVersion"), apiVersion());
    object.insert(QStringLiteral("updatedAt"), nowIso());
    object.insert(QStringLiteral("pluginsDir"), AppPaths::pluginsDir());
    return Json::writeObjectFile(AppPaths::pluginRegistryFile(), object, error);
}

bool PackageManager::writeInstallMetadata(const PluginManifest &manifest, QString *error)
{
    QJsonObject object = manifest.toJson();
    object.insert(QStringLiteral("installedBy"), QStringLiteral("McServerManager"));
    return Json::writeObjectFile(QDir(manifest.path).filePath(QStringLiteral("install.json")),
                                 object, error);
}

QVector<PluginManifest> PackageManager::installed(bool includeDisabled)
{
    QVector<PluginManifest> result;
    const QJsonArray entries = readRegistry().value(QStringLiteral("plugins")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        const QString path = QDir::cleanPath(
            entry.value(QStringLiteral("path")).toString(QDir(AppPaths::pluginDir(id)).absolutePath()));
        PluginValidation validation = inspectDirectory(path, nullptr);
        if (!validation.ok) {
            Logger::warn(QStringLiteral("plugin"),
                         QStringLiteral("ignoring broken plugin %1: %2")
                             .arg(id, validation.errors.join(QStringLiteral("; "))));
            continue;
        }
        PluginManifest manifest = validation.manifest;
        manifest.path = path;
        manifest.enabled = entry.value(QStringLiteral("enabled")).toBool(true);
        manifest.installedAt = entry.value(QStringLiteral("installedAt")).toString();
        manifest.source = entry.value(QStringLiteral("source")).toString(QStringLiteral("zip"));
        manifest.sourceUrl = entry.value(QStringLiteral("sourceUrl")).toString();
        if (!includeDisabled && !manifest.enabled)
            continue;
        result.append(manifest);
    }
    return result;
}

PluginManifest PackageManager::find(const QString &id)
{
    const QString normalized = normalizeId(id);
    for (const PluginManifest &manifest : installed())
        if (manifest.id.compare(normalized, Qt::CaseInsensitive) == 0)
            return manifest;
    return PluginManifest {};
}

bool PackageManager::exists(const QString &id)
{
    return !find(id).id.isEmpty();
}

QVector<PluginManifest> PackageManager::frontendPlugins()
{
    QVector<PluginManifest> result;
    for (const PluginManifest &manifest : installed(false)) {
        if (manifest.hasFrontend())
            result.append(manifest);
    }
    return result;
}

// ------------------------------------------------------------- filesystem ----

bool PackageManager::copyTree(const QString &from, const QString &to, QStringList *copied, QString *error)
{
    // Compare absolute paths: `from` may arrive relative (e.g. from --home) while
    // the iterator yields a differently normalised prefix.
    const QString absoluteFrom = QDir::cleanPath(QFileInfo(from).absoluteFilePath());
    const QDir source(absoluteFrom);
    if (!source.exists()) {
        if (error)
            *error = QStringLiteral("源目录不存在：%1").arg(from);
        return false;
    }
    if (!QDir().mkpath(to)) {
        if (error)
            *error = QStringLiteral("无法创建目录：%1").arg(to);
        return false;
    }
    QDirIterator iterator(absoluteFrom, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString relative = source.relativeFilePath(QDir::cleanPath(info.absoluteFilePath()));
        if (relative.startsWith(QLatin1String(".."))) {
            // never escape the destination directory
            continue;
        }
        const QString destination = QDir(to).filePath(relative);
        if (info.isDir()) {
            if (!QDir().mkpath(destination)) {
                if (error)
                    *error = QStringLiteral("无法创建目录：%1").arg(destination);
                return false;
            }
            continue;
        }
        QDir().mkpath(QFileInfo(destination).absolutePath());
        if (QFile::exists(destination))
            QFile::remove(destination);
        if (!QFile::copy(path, destination)) {
            if (error)
                *error = QStringLiteral("复制失败：%1").arg(relative);
            return false;
        }
        if (copied)
            *copied << relative;
    }
    return true;
}

// ---------------------------------------------------------------- install ----

Result PackageManager::install(const PluginInstallRequest &request)
{
    if (request.zipPath.trimmed().isEmpty() && request.url.trimmed().isEmpty())
        return Result::fail(QStringLiteral("INVALID_ARGUMENT"),
                            QStringLiteral("请提供 --zip 本地压缩包或 --url 下载地址"));

    const QString staging = QDir(AppPaths::tmpDir())
                                .filePath(QStringLiteral("plugin-%1")
                                              .arg(QUuid::createUuid().toString(QUuid::Id128)));
    if (!QDir().mkpath(staging))
        return Result::fail(QStringLiteral("IO_ERROR"),
                            QStringLiteral("无法创建临时目录"), staging);

    const auto cleanup = [&staging]() {
        QString ignored;
        Archive::removeTree(staging, &ignored);
    };

    QString archivePath = request.zipPath.trimmed();
    QString source = QStringLiteral("zip");
    if (!request.url.trimmed().isEmpty()) {
        const QString downloadPath = QDir(staging).filePath(QStringLiteral("source.zip"));
        QString downloadError;
        Logger::info(QStringLiteral("plugin"),
                     QStringLiteral("downloading %1").arg(request.url));
        if (!HttpClient::download(QUrl(request.url.trimmed()), downloadPath, &downloadError, nullptr,
                                  600000)) {
            cleanup();
            return Result::fail(QStringLiteral("PLUGIN_DOWNLOAD_FAILED"),
                                QStringLiteral("插件包下载失败"), downloadError);
        }
        archivePath = downloadPath;
        source = QStringLiteral("url");
    }

    if (!QFileInfo::exists(archivePath)) {
        cleanup();
        return Result::fail(QStringLiteral("NOT_FOUND"),
                            QStringLiteral("找不到插件包"), archivePath);
    }
    if (!Archive::isZipArchive(archivePath)) {
        cleanup();
        return Result::fail(QStringLiteral("INVALID_PLUGIN_PACKAGE"),
                            QStringLiteral("插件包必须是 zip 压缩包"),
                            QStringLiteral("也可以直接把解压后的目录复制到 %1")
                                .arg(AppPaths::pluginsDir()));
    }

    const QString extractDir = QDir(staging).filePath(QStringLiteral("extract"));
    QString archiveError;
    QString archiveLog;
    if (!Archive::extractZip(archivePath, extractDir, &archiveError, &archiveLog)) {
        cleanup();
        return Result::fail(QStringLiteral("PLUGIN_EXTRACT_FAILED"),
                            QStringLiteral("解压插件包失败"), archiveError);
    }

    QString inspectError;
    const PluginValidation validation = inspectDirectory(extractDir, &inspectError);
    if (!validation.ok) {
        const QString detail = validation.errors.isEmpty() ? inspectError
                                                           : validation.errors.join(QStringLiteral("\n"));
        cleanup();
        return Result::fail(QStringLiteral("INVALID_PLUGIN_PACKAGE"),
                            QStringLiteral("插件包不合法"), detail);
    }

    PluginManifest manifest = validation.manifest;
    if (!request.expectId.isEmpty()
        && normalizeId(request.expectId).compare(manifest.id, Qt::CaseInsensitive) != 0) {
        cleanup();
        return Result::fail(QStringLiteral("PLUGIN_ID_MISMATCH"),
                            QStringLiteral("插件包 id 与预期不符"),
                            QStringLiteral("期望 %1，实际 %2").arg(request.expectId, manifest.id));
    }

    const QString target = AppPaths::pluginDir(manifest.id);
    const bool replacing = QFileInfo::exists(target);
    if (replacing && !request.force) {
        cleanup();
        return Result::fail(QStringLiteral("PLUGIN_EXISTS"),
                            QStringLiteral("插件 %1 已安装").arg(manifest.id),
                            QStringLiteral("如需覆盖请使用 --force"));
    }
    if (replacing) {
        QString removeError;
        if (!Archive::removeTree(target, &removeError)) {
            cleanup();
            return Result::fail(QStringLiteral("IO_ERROR"),
                                QStringLiteral("无法移除已安装的插件"), removeError);
        }
    }

    QStringList copiedFiles;
    QString copyError;
    if (!copyTree(manifest.path, target, &copiedFiles, &copyError)) {
        cleanup();
        return Result::fail(QStringLiteral("IO_ERROR"),
                            QStringLiteral("写入插件目录失败"), copyError);
    }
    cleanup();

    manifest.path = target;
    manifest.enabled = request.enable;
    manifest.installedAt = nowIso();
    manifest.source = source;
    manifest.sourceUrl = request.url.trimmed();

    QString metadataError;
    writeInstallMetadata(manifest, &metadataError);

    QJsonObject registry = readRegistry();
    QJsonArray entries = registry.value(QStringLiteral("plugins")).toArray();
    QJsonArray next;
    for (const QJsonValue &value : entries) {
        if (value.toObject().value(QStringLiteral("id")).toString().compare(manifest.id) != 0)
            next.append(value);
    }
    next.append(QJsonObject {
        {QStringLiteral("id"), manifest.id},
        {QStringLiteral("name"), manifest.name},
        {QStringLiteral("version"), manifest.version},
        {QStringLiteral("scope"), manifest.scopeName(manifest.scope)},
        {QStringLiteral("enabled"), manifest.enabled},
        {QStringLiteral("path"), manifest.path},
        {QStringLiteral("installedAt"), manifest.installedAt},
        {QStringLiteral("source"), manifest.source},
        {QStringLiteral("sourceUrl"), manifest.sourceUrl},
    });
    registry.insert(QStringLiteral("plugins"), next);
    QString registryError;
    if (!writeRegistry(registry, &registryError)) {
        return Result::fail(QStringLiteral("IO_ERROR"),
                            QStringLiteral("插件已解压但注册表写入失败"), registryError);
    }

    Logger::info(QStringLiteral("plugin"),
                 QStringLiteral("installed package %1 (%2) scope=%3")
                     .arg(manifest.id, manifest.version, manifest.scopeName(manifest.scope)));

    QJsonObject data = manifest.toJson();
    data.insert(QStringLiteral("installedPath"), manifest.path);
    data.insert(QStringLiteral("replaced"), replacing);
    data.insert(QStringLiteral("fileCount"), copiedFiles.size());
    data.insert(QStringLiteral("scopeLabel"), PluginManifest::scopeLabel(manifest.scope));
    Result result = Result::ok(data);
    for (const QString &warning : validation.warnings)
        result.warn(warning);
    result.warn(QStringLiteral("插件已安装到 %1").arg(manifest.path));
    return result;
}

Result PackageManager::remove(const QString &id, bool purgeData)
{
    const PluginManifest manifest = find(id);
    if (manifest.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"),
                            QStringLiteral("插件 %1 未安装").arg(id));

    QString removeError;
    if (!Archive::removeTree(manifest.path, &removeError))
        return Result::fail(QStringLiteral("IO_ERROR"),
                            QStringLiteral("无法删除插件目录"), removeError);

    bool dataRemoved = false;
    if (purgeData) {
        QString dataError;
        dataRemoved = Archive::removeTree(AppPaths::pluginDataDir(manifest.id), &dataError);
    }

    QJsonObject registry = readRegistry();
    QJsonArray next;
    const QJsonArray entries = registry.value(QStringLiteral("plugins")).toArray();
    for (const QJsonValue &value : entries) {
        if (value.toObject().value(QStringLiteral("id")).toString().compare(manifest.id) != 0)
            next.append(value);
    }
    registry.insert(QStringLiteral("plugins"), next);
    QString registryError;
    writeRegistry(registry, &registryError);

    Logger::info(QStringLiteral("plugin"), QStringLiteral("removed package %1").arg(manifest.id));
    return Result::ok(QJsonObject {
        {QStringLiteral("removed"), manifest.id},
        {QStringLiteral("name"), manifest.name},
        {QStringLiteral("path"), manifest.path},
        {QStringLiteral("dataRemoved"), dataRemoved},
    });
}

Result PackageManager::setEnabled(const QString &id, bool enabled)
{
    const PluginManifest manifest = find(id);
    if (manifest.id.isEmpty())
        return Result::fail(QStringLiteral("NOT_FOUND"),
                            QStringLiteral("插件 %1 未安装").arg(id));

    if (enabled && manifest.scope == PluginScope::Unknown) {
        return Result::fail(QStringLiteral("INVALID_PLUGIN_PACKAGE"),
                            QStringLiteral("插件类型未知，无法启用"));
    }

    QJsonObject registry = readRegistry();
    QJsonArray next;
    bool found = false;
    const QJsonArray entries = registry.value(QStringLiteral("plugins")).toArray();
    for (const QJsonValue &value : entries) {
        QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("id")).toString().compare(manifest.id) == 0) {
            entry.insert(QStringLiteral("enabled"), enabled);
            found = true;
        }
        next.append(entry);
    }
    if (!found) {
        next.append(QJsonObject {
            {QStringLiteral("id"), manifest.id},
            {QStringLiteral("name"), manifest.name},
            {QStringLiteral("version"), manifest.version},
            {QStringLiteral("scope"), manifest.scopeName(manifest.scope)},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("path"), manifest.path},
            {QStringLiteral("installedAt"), manifest.installedAt},
        });
    }
    registry.insert(QStringLiteral("plugins"), next);
    QString registryError;
    if (!writeRegistry(registry, &registryError))
        return Result::fail(QStringLiteral("IO_ERROR"),
                            QStringLiteral("保存插件状态失败"), registryError);

    Logger::info(QStringLiteral("plugin"),
                 QStringLiteral("plugin %1 %2").arg(manifest.id,
                                                    enabled ? QStringLiteral("enabled")
                                                            : QStringLiteral("disabled")));
    return Result::ok(QJsonObject {
        {QStringLiteral("id"), manifest.id},
        {QStringLiteral("name"), manifest.name},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("scope"), manifest.scopeName(manifest.scope)},
    });
}

// -------------------------------------------------------------- api doc ------

QJsonObject PackageManager::apiManifest()
{
    const auto strArray = [](const QStringList &values) {
        return QJsonArray::fromStringList(values);
    };

    QJsonObject packageSection;
    packageSection.insert(QStringLiteral("layout"), strArray({
        QStringLiteral("plugin.json"),
        QStringLiteral("frontend/index.js"),
        QStringLiteral("backend/index.js"),
        QStringLiteral("web/index.html"),
        QStringLiteral("README.md"),
    }));
    packageSection.insert(QStringLiteral("manifestVersion"), apiVersion());
    packageSection.insert(QStringLiteral("requiredFields"), strArray({
        QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("version"),
    }));
    packageSection.insert(QStringLiteral("optionalFields"), strArray({
        QStringLiteral("apiVersion"), QStringLiteral("scope"), QStringLiteral("description"),
        QStringLiteral("author"), QStringLiteral("homepage"), QStringLiteral("license"),
        QStringLiteral("permissions"), QStringLiteral("hooks"), QStringLiteral("frontend"),
        QStringLiteral("backend"), QStringLiteral("web"), QStringLiteral("contributes"),
    }));

    QJsonObject scopes;
    scopes.insert(QStringLiteral("frontend"),
                  QStringLiteral("只扩展桌面界面：注册页面、按钮、通知"));
    scopes.insert(QStringLiteral("backend"),
                  QStringLiteral("只扩展后端：生命周期钩子、性能调优、API 方法"));
    scopes.insert(QStringLiteral("web"),
                  QStringLiteral("支持库 / Web 运维面板：提供静态资源与本地服务"));
    scopes.insert(QStringLiteral("global"),
                  QStringLiteral("同时包含前端与后端内容（frontend/ 与 backend/ 目录同时存在）"));
    scopes.insert(QStringLiteral("auto"),
                  QStringLiteral("默认值：按目录结构自动识别"));

    QJsonObject frontendApi;
    frontendApi.insert(QStringLiteral("runtime"), QStringLiteral("QJSEngine (ES2020, 无 DOM)"));
    frontendApi.insert(QStringLiteral("globalObject"), QStringLiteral("mcsm"));
    frontendApi.insert(QStringLiteral("namespaces"), strArray({
        QStringLiteral("mcsm.plugin"), QStringLiteral("mcsm.log"), QStringLiteral("mcsm.toast"),
        QStringLiteral("mcsm.ui"), QStringLiteral("mcsm.storage"), QStringLiteral("mcsm.settings"),
        QStringLiteral("mcsm.servers"), QStringLiteral("mcsm.backend"), QStringLiteral("mcsm.events"),
        QStringLiteral("mcsm.utils"), QStringLiteral("mcsm.openUrl"),
    }));
    frontendApi.insert(QStringLiteral("pageBlocks"), strArray({
        QStringLiteral("heading"), QStringLiteral("text"), QStringLiteral("keyvalue"),
        QStringLiteral("table"), QStringLiteral("buttons"), QStringLiteral("log"),
        QStringLiteral("html"), QStringLiteral("divider"), QStringLiteral("progress"),
    }));

    QJsonObject backendApi;
    backendApi.insert(QStringLiteral("protocol"), QStringLiteral("JSON lines over stdin/stdout"));
    backendApi.insert(QStringLiteral("runtimes"), strArray({
        QStringLiteral("node"), QStringLiteral("python"), QStringLiteral("exec"), QStringLiteral("auto"),
    }));
    backendApi.insert(QStringLiteral("hooks"), strArray({
        QStringLiteral("plugin.install"), QStringLiteral("plugin.enable"),
        QStringLiteral("plugin.disable"), QStringLiteral("plugin.uninstall"),
        QStringLiteral("server.beforeStart"), QStringLiteral("server.afterStart"),
        QStringLiteral("server.beforeStop"), QStringLiteral("server.afterStop"),
        QStringLiteral("server.beforeBackup"), QStringLiteral("server.afterBackup"),
        QStringLiteral("server.configApplied"), QStringLiteral("scheduler.tick"),
    }));
    backendApi.insert(QStringLiteral("startPatchKeys"), strArray({
        QStringLiteral("jvmArgs"), QStringLiteral("env"), QStringLiteral("dockerArgs"),
        QStringLiteral("note"), QStringLiteral("cancel"), QStringLiteral("reason"),
    }));

    QJsonObject cli;
    cli.insert(QStringLiteral("install"), QStringLiteral("mcsm-cli plugin package install --zip <file>|--url <url> [--force]"));
    cli.insert(QStringLiteral("list"), QStringLiteral("mcsm-cli plugin packages [--enabled]"));
    cli.insert(QStringLiteral("info"), QStringLiteral("mcsm-cli plugin package info --name <id>"));
    cli.insert(QStringLiteral("enable"), QStringLiteral("mcsm-cli plugin package enable|disable --name <id>"));
    cli.insert(QStringLiteral("remove"), QStringLiteral("mcsm-cli plugin package remove --name <id> [--purge-data]"));
    cli.insert(QStringLiteral("inspect"), QStringLiteral("mcsm-cli plugin package inspect --zip <file>"));
    cli.insert(QStringLiteral("call"), QStringLiteral("mcsm-cli plugin call --name <id> --method <name> [--params '{...}']"));
    cli.insert(QStringLiteral("hook"), QStringLiteral("mcsm-cli plugin hook --name <id> --hook <name> [--payload '{...}']"));
    cli.insert(QStringLiteral("service"), QStringLiteral("mcsm-cli plugin service start|stop|status --name <id>"));

    QJsonObject object;
    object.insert(QStringLiteral("apiVersion"), apiVersion());
    object.insert(QStringLiteral("manager"), QStringLiteral("McServerManager plugin package API"));
    object.insert(QStringLiteral("package"), packageSection);
    object.insert(QStringLiteral("scopes"), scopes);
    object.insert(QStringLiteral("frontend"), frontendApi);
    object.insert(QStringLiteral("backend"), backendApi);
    object.insert(QStringLiteral("cli"), cli);
    object.insert(QStringLiteral("pluginsDir"), AppPaths::pluginsDir());
    object.insert(QStringLiteral("dataDir"), AppPaths::pluginsDir() + QStringLiteral("/.data"));
    object.insert(QStringLiteral("docs"), QStringLiteral("docs/PLUGIN-SDK.md"));
    return object;
}

} // namespace mcsm
