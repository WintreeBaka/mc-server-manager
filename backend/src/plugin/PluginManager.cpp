#include "plugin/PluginManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrlQuery>

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/StringUtil.h"
#include "net/HttpClient.h"

namespace mcsm {
namespace {

const char *kModrinthApi = "https://api.modrinth.com/v2";
const char *kHangarApi = "https://hangar.papermc.io/api/v1";
const char *kSpigetApi = "https://api.spiget.org/v2";

QJsonObject fetchJson(const QUrl &url, QString *error)
{
    const HttpReply reply = HttpClient::get(url, 25000);
    if (!reply.ok) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(url.toString(), reply.error);
        return QJsonObject();
    }
    QJsonObject object;
    if (!reply.parseJson(&object)) {
        if (error)
            *error = QStringLiteral("%1: invalid json response").arg(url.toString());
        return QJsonObject();
    }
    return object;
}

QJsonArray fetchArray(const QUrl &url, QString *error)
{
    const HttpReply reply = HttpClient::get(url, 25000);
    if (!reply.ok) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(url.toString(), reply.error);
        return QJsonArray();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply.body);
    if (!doc.isArray()) {
        if (error)
            *error = QStringLiteral("%1: expected json array").arg(url.toString());
        return QJsonArray();
    }
    return doc.array();
}

} // namespace

QJsonObject PluginHit::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("source"), source);
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("slug"), slug);
    object.insert(QStringLiteral("title"), title);
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("author"), author);
    object.insert(QStringLiteral("iconUrl"), iconUrl);
    object.insert(QStringLiteral("pageUrl"), pageUrl);
    object.insert(QStringLiteral("downloadUrl"), downloadUrl);
    object.insert(QStringLiteral("fileName"), fileName);
    object.insert(QStringLiteral("versionName"), versionName);
    object.insert(QStringLiteral("downloads"), double(downloads));
    object.insert(QStringLiteral("installable"), installable);
    object.insert(QStringLiteral("gameVersions"), QJsonArray::fromStringList(gameVersions));
    object.insert(QStringLiteral("loaders"), QJsonArray::fromStringList(loaders));
    object.insert(QStringLiteral("note"), note);
    return object;
}

QJsonObject InstalledPlugin::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("file"), file);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("size"), double(size));
    object.insert(QStringLiteral("sizeText"), StringUtil::humanBytes(size));
    object.insert(QStringLiteral("modifiedAt"), modifiedAt);
    object.insert(QStringLiteral("enabled"), enabled);
    return object;
}

QStringList PluginManager::sources()
{
    return {QStringLiteral("modrinth"), QStringLiteral("hangar"), QStringLiteral("spiget")};
}

bool PluginManager::supportsPlugins(const ServerRecord &record)
{
    const QString type = record.type.toLower();
    return type == QLatin1String("paper") || type == QLatin1String("purpur")
           || type == QLatin1String("spigot") || type == QLatin1String("bukkit");
}

QStringList PluginManager::loaderTokens(const ServerRecord &record)
{
    const QString type = record.type.toLower();
    if (type == QLatin1String("fabric"))
        return {QStringLiteral("fabric")};
    if (type == QLatin1String("vanilla"))
        return {};
    return {QStringLiteral("paper"), QStringLiteral("bukkit"), QStringLiteral("spigot")};
}

QVector<PluginHit> PluginManager::search(const QString &source,
                                         const QString &query,
                                         const ServerRecord &record,
                                         int limit,
                                         QString *error)
{
    const QString normalized = source.toLower();
    if (normalized == QLatin1String("modrinth"))
        return searchModrinth(query, record, limit, error);
    if (normalized == QLatin1String("hangar"))
        return searchHangar(query, record, limit, error);
    if (normalized == QLatin1String("spiget"))
        return searchSpiget(query, limit, error);
    if (error)
        *error = QStringLiteral("未知插件源 '%1'").arg(source);
    return {};
}

QVector<PluginHit> PluginManager::searchModrinth(const QString &query,
                                                 const ServerRecord &record,
                                                 int limit,
                                                 QString *error)
{
    QVector<PluginHit> hits;
    QUrl url(QStringLiteral("%1/search").arg(QLatin1String(kModrinthApi)));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("query"), query);
    params.addQueryItem(QStringLiteral("limit"), QString::number(qBound(1, limit, 50)));
    params.addQueryItem(QStringLiteral("index"), QStringLiteral("relevance"));

    const QStringList loaders = loaderTokens(record);
    // Modrinth expects an array of arrays: facets=[[...],[...]] where entries of
    // one inner array are OR'ed and separate arrays are AND'ed.
    const bool fabricServer = record.type.compare(QLatin1String("fabric"), Qt::CaseInsensitive) == 0;
    QStringList facetGroups;
    facetGroups << (fabricServer ? QStringLiteral("[\"project_type:mod\"]")
                                 : QStringLiteral("[\"project_type:plugin\"]"));
    if (!loaders.isEmpty()) {
        QStringList loaderFacet;
        for (const QString &loader : loaders)
            loaderFacet << QStringLiteral("\"categories:%1\"").arg(loader);
        facetGroups << QStringLiteral("[%1]").arg(loaderFacet.join(QLatin1Char(',')));
    }
    params.addQueryItem(QStringLiteral("facets"),
                        QStringLiteral("[%1]").arg(facetGroups.join(QLatin1Char(','))));
    url.setQuery(params);

    const QJsonObject object = fetchJson(url, error);
    const QJsonArray results = object.value(QStringLiteral("hits")).toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject entry = value.toObject();
        PluginHit hit;
        hit.source = QStringLiteral("modrinth");
        hit.id = Json::str(entry, QStringLiteral("project_id"));
        hit.slug = Json::str(entry, QStringLiteral("slug"));
        hit.title = Json::str(entry, QStringLiteral("title"));
        hit.description = Json::str(entry, QStringLiteral("description"));
        hit.author = Json::str(entry, QStringLiteral("author"));
        hit.iconUrl = Json::str(entry, QStringLiteral("icon_url"));
        hit.pageUrl = QStringLiteral("https://modrinth.com/plugin/%1").arg(hit.slug);
        hit.downloads = Json::bigint(entry, QStringLiteral("downloads"));
        hit.gameVersions = Json::toList(entry.value(QStringLiteral("versions")));
        hit.loaders = Json::toList(entry.value(QStringLiteral("categories")));
        hit.installable = !record.mcVersion.isEmpty();
        hits.append(hit);
    }
    return hits;
}

QVector<PluginHit> PluginManager::searchHangar(const QString &query,
                                               const ServerRecord &record,
                                               int limit,
                                               QString *error)
{
    QVector<PluginHit> hits;
    QUrl url(QStringLiteral("%1/projects").arg(QLatin1String(kHangarApi)));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("q"), query);
    params.addQueryItem(QStringLiteral("limit"), QString::number(qBound(1, limit, 25)));
    params.addQueryItem(QStringLiteral("sort"), QStringLiteral("-downloads"));
    url.setQuery(params);

    const QJsonObject object = fetchJson(url, error);
    const QJsonArray results = object.value(QStringLiteral("result")).toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject entry = value.toObject();
        const QJsonObject namespaceObject = entry.value(QStringLiteral("namespace")).toObject();
        const QJsonObject stats = entry.value(QStringLiteral("stats")).toObject();
        PluginHit hit;
        hit.source = QStringLiteral("hangar");
        hit.slug = Json::str(namespaceObject, QStringLiteral("slug"));
        hit.id = QStringLiteral("%1/%2").arg(Json::str(namespaceObject, QStringLiteral("owner")), hit.slug);
        hit.title = Json::str(entry, QStringLiteral("name"), hit.slug);
        hit.description = Json::str(entry, QStringLiteral("description"));
        hit.iconUrl = Json::str(entry, QStringLiteral("avatarUrl"));
        hit.pageUrl = QStringLiteral("https://hangar.papermc.io/%1").arg(hit.id);
        hit.downloads = Json::bigint(stats, QStringLiteral("downloads"));
        hit.installable = !record.mcVersion.isEmpty();
        hits.append(hit);
    }
    return hits;
}

QVector<PluginHit> PluginManager::searchSpiget(const QString &query, int limit, QString *error)
{
    QVector<PluginHit> hits;
    QUrl url(QStringLiteral("%1/search/resources/%2").arg(QLatin1String(kSpigetApi),
                                                         QString::fromLatin1(QUrl::toPercentEncoding(query))));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("field"), QStringLiteral("name"));
    params.addQueryItem(QStringLiteral("size"), QString::number(qBound(1, limit, 25)));
    params.addQueryItem(QStringLiteral("sort"), QStringLiteral("-downloads"));
    url.setQuery(params);

    const QJsonArray results = fetchArray(url, error);
    for (const QJsonValue &value : results) {
        const QJsonObject entry = value.toObject();
        const QJsonObject author = entry.value(QStringLiteral("author")).toObject();
        PluginHit hit;
        hit.source = QStringLiteral("spiget");
        hit.id = QString::number(Json::bigint(entry, QStringLiteral("id")));
        hit.slug = hit.id;
        hit.title = Json::str(entry, QStringLiteral("name"));
        hit.description = Json::str(entry, QStringLiteral("tag"));
        hit.author = Json::str(author, QStringLiteral("name"));
        hit.iconUrl = QStringLiteral("https://www.spigotmc.org/%1")
                          .arg(Json::str(entry, QStringLiteral("icon")).remove(QRegularExpression(QStringLiteral("^\\./"))));
        hit.pageUrl = QStringLiteral("https://www.spigotmc.org/resources/%1/").arg(hit.id);
        hit.downloads = Json::bigint(entry, QStringLiteral("downloads"));
        hit.installable = false;
        hit.note = QStringLiteral("SpigotMC 下载需要通过浏览器完成，安装请使用 Modrinth 或 Hangar");
        hits.append(hit);
    }
    return hits;
}

bool PluginManager::resolveDownload(PluginHit *hit, const ServerRecord &record, QString *error)
{
    if (!hit)
        return false;
    if (hit->source == QLatin1String("modrinth")) {
        const QStringList loaders = loaderTokens(record);
        QUrl url(QStringLiteral("%1/project/%2/version").arg(QLatin1String(kModrinthApi),
                                                            hit->slug.isEmpty() ? hit->id : hit->slug));
        QUrlQuery params;
        if (!record.mcVersion.isEmpty())
            params.addQueryItem(QStringLiteral("game_versions"),
                                QStringLiteral("[\"%1\"]").arg(record.mcVersion));
        if (!loaders.isEmpty()) {
            QStringList quoted;
            for (const QString &loader : loaders)
                quoted << QStringLiteral("\"%1\"").arg(loader);
            params.addQueryItem(QStringLiteral("loaders"),
                                QStringLiteral("[%1]").arg(quoted.join(QLatin1Char(','))));
        }
        url.setQuery(params);

        const QJsonArray versions = fetchArray(url, error);
        if (versions.isEmpty()) {
            if (error && error->isEmpty())
                *error = QStringLiteral("没有与 %1 %2 兼容的版本").arg(record.type, record.mcVersion);
            return false;
        }
        const QJsonObject version = versions.first().toObject();
        const QJsonArray files = version.value(QStringLiteral("files")).toArray();
        if (files.isEmpty()) {
            if (error)
                *error = QStringLiteral("该版本没有可下载文件");
            return false;
        }
        const QJsonObject file = files.first().toObject();
        hit->downloadUrl = Json::str(file, QStringLiteral("url"));
        hit->fileName = Json::str(file, QStringLiteral("filename"));
        hit->versionName = Json::str(version, QStringLiteral("version_number"));
        return !hit->downloadUrl.isEmpty();
    }

    if (hit->source == QLatin1String("hangar")) {
        const QString platform = record.type.compare(QLatin1String("fabric")) == 0
                                     ? QStringLiteral("FABRIC")
                                     : QStringLiteral("PAPER");
        QUrl url(QStringLiteral("%1/projects/%2/versions").arg(QLatin1String(kHangarApi), hit->slug));
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("limit"), QStringLiteral("5"));
        params.addQueryItem(QStringLiteral("platform"), platform);
        params.addQueryItem(QStringLiteral("channel"), QStringLiteral("Release"));
        url.setQuery(params);

        const QJsonObject object = fetchJson(url, error);
        const QJsonArray versions = object.value(QStringLiteral("result")).toArray();
        if (versions.isEmpty()) {
            if (error)
                *error = QStringLiteral("Hangar 上没有可用于 %1 的版本").arg(platform);
            return false;
        }
        const QJsonObject version = versions.first().toObject();
        hit->versionName = Json::str(version, QStringLiteral("name"));
        hit->gameVersions = Json::toList(version.value(QStringLiteral("platformDependencies"))
                                             .toObject()
                                             .value(platform));
        hit->downloadUrl = QStringLiteral("%1/projects/%2/versions/%3/%4/download")
                               .arg(QLatin1String(kHangarApi), hit->slug, hit->versionName, platform);
        hit->fileName = QStringLiteral("%1-%2.jar").arg(hit->slug, hit->versionName);
        return true;
    }

    if (error)
        *error = QStringLiteral("插件源 %1 不支持直接安装").arg(hit->source);
    return false;
}

Result PluginManager::install(const ServerRecord &record, const PluginHit &hit)
{
    if (!supportsPlugins(record)) {
        return Result::fail(QStringLiteral("UNSUPPORTED"),
                            QStringLiteral("%1 服务端不支持 Bukkit 系插件").arg(record.type),
                            QStringLiteral("Fabric 请使用模组 (mods) 目录，原版服务端没有插件 API"));
    }
    PluginHit resolved = hit;
    QString resolveError;
    if (!resolveDownload(&resolved, record, &resolveError))
        return Result::fail(QStringLiteral("PLUGIN_RESOLVE_FAILED"),
                            QStringLiteral("无法解析插件下载地址"), resolveError);

    const QString fileName = resolved.fileName.isEmpty()
                                 ? StringUtil::slugify(resolved.title) + QStringLiteral(".jar")
                                 : resolved.fileName;
    const QString target = QDir(record.pluginDir()).filePath(fileName);
    QDir().mkpath(record.pluginDir());

    QString downloadError;
    if (!HttpClient::download(QUrl(resolved.downloadUrl), target, &downloadError)) {
        return Result::fail(QStringLiteral("PLUGIN_DOWNLOAD_FAILED"),
                            QStringLiteral("插件下载失败"), downloadError);
    }
    Logger::info(QStringLiteral("plugin"), QStringLiteral("installed %1").arg(fileName));
    QJsonObject data = resolved.toJson();
    data.insert(QStringLiteral("installedFile"), fileName);
    data.insert(QStringLiteral("path"), target);
    Result result = Result::ok(data);
    result.warn(QStringLiteral("插件已写入 plugins 目录，重启%s后生效").arg(record.name));
    return result;
}

QVector<InstalledPlugin> PluginManager::installed(const ServerRecord &record)
{
    QVector<InstalledPlugin> plugins;
    QDir dir(record.pluginDir());
    if (!dir.exists())
        return plugins;
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.jar"), QStringLiteral("*.jar.disabled")},
                                                  QDir::Files, QDir::Name);
    for (const QFileInfo &info : files) {
        InstalledPlugin plugin;
        plugin.file = info.fileName();
        plugin.name = info.fileName();
        plugin.name.remove(QRegularExpression(QStringLiteral("\\.jar(\\.disabled)?$")));
        plugin.size = info.size();
        plugin.modifiedAt = info.lastModified().toString(Qt::ISODate);
        plugin.enabled = !info.fileName().endsWith(QLatin1String(".disabled"));
        plugins.append(plugin);
    }
    return plugins;
}

Result PluginManager::setEnabled(const ServerRecord &record, const QString &file, bool enabled)
{
    const QString source = QDir(record.pluginDir()).filePath(file);
    if (!QFileInfo::exists(source))
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("插件 %1 不存在").arg(file));
    QString target;
    if (enabled) {
        target = source.endsWith(QLatin1String(".disabled"))
                     ? source.left(source.size() - QStringLiteral(".disabled").size())
                     : source;
    } else {
        target = source.endsWith(QLatin1String(".disabled")) ? source : source + QStringLiteral(".disabled");
    }
    if (target == source)
        return Result::ok(QJsonObject {{QStringLiteral("file"), file}, {QStringLiteral("enabled"), enabled}});
    if (!QFile::rename(source, target))
        return Result::fail(QStringLiteral("IO_ERROR"), QStringLiteral("无法重命名 %1").arg(file));
    return Result::ok(QJsonObject {{QStringLiteral("file"), QFileInfo(target).fileName()},
                                   {QStringLiteral("enabled"), enabled}});
}

Result PluginManager::remove(const ServerRecord &record, const QString &file)
{
    const QString path = QDir(record.pluginDir()).filePath(file);
    if (!QFileInfo::exists(path))
        return Result::fail(QStringLiteral("NOT_FOUND"), QStringLiteral("插件 %1 不存在").arg(file));
    if (!QFile::remove(path))
        return Result::fail(QStringLiteral("IO_ERROR"), QStringLiteral("无法删除 %1").arg(file));
    return Result::ok(QJsonObject {{QStringLiteral("removed"), file}});
}

} // namespace mcsm
