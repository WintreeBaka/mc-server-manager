#include "net/VersionResolver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>

#include "core/JsonUtil.h"
#include "core/Logger.h"
#include "core/StringUtil.h"
#include "net/HttpClient.h"

namespace mcsm {
namespace {

const char *kMojangManifest = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
const char *kMojangManifestFallback = "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json";
// PaperMC retired the v2 API (HTTP 410 Gone) in favour of fill.papermc.io/v3.
const char *kPaperApi = "https://fill.papermc.io/v3";
const char *kPurpurApi = "https://api.purpurmc.org/v2";
const char *kFabricApi = "https://meta.fabricmc.net/v2";

QJsonObject fetchJson(const QStringList &urls, QString *error)
{
    QStringList failures;
    for (const QString &url : urls) {
        const HttpReply reply = HttpClient::get(QUrl(url));
        if (reply.ok) {
            QJsonObject object;
            if (reply.parseJson(&object))
                return object;
            failures << QStringLiteral("%1: invalid json").arg(url);
        } else {
            failures << QStringLiteral("%1: %2").arg(url, reply.error);
        }
    }
    if (error)
        *error = failures.join(QStringLiteral("; "));
    return QJsonObject();
}

QJsonArray fetchJsonArray(const QStringList &urls, QString *error)
{
    QStringList failures;
    for (const QString &url : urls) {
        const HttpReply reply = HttpClient::get(QUrl(url));
        if (reply.ok) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply.body);
            if (doc.isArray())
                return doc.array();
            failures << QStringLiteral("%1: expected json array").arg(url);
        } else {
            failures << QStringLiteral("%1: %2").arg(url, reply.error);
        }
    }
    if (error)
        *error = failures.join(QStringLiteral("; "));
    return QJsonArray();
}

QVector<int> parseVersionTriple(const QString &version)
{
    static const QRegularExpression re(QStringLiteral("(\\d+)\\.(\\d+)(?:\\.(\\d+))?"));
    const QRegularExpressionMatch match = re.match(version);
    if (!match.hasMatch())
        return {0, 0, 0};
    return {match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt()};
}

bool versionAtLeast(const QString &version, int major, int minor, int patch, bool exclusivePatch = false)
{
    const QVector<int> parts = parseVersionTriple(version);
    if (parts.size() < 3)
        return false;
    if (parts[0] != major)
        return parts[0] > major;
    if (parts[1] != minor)
        return parts[1] > minor;
    return exclusivePatch ? parts[2] > patch : parts[2] >= patch;
}

QStringList versionArrayToList(const QJsonObject &object, const QString &key)
{
    QStringList list;
    const QJsonArray array = object.value(key).toArray();
    for (const QJsonValue &value : array) {
        if (value.isString())
            list << value.toString();
        else if (value.isObject())
            list << Json::str(value.toObject(), QStringLiteral("id"));
    }
    return list;
}

/// Numeric comparison for version ids such as "1.21.11" or "26.3".
int compareVersionIds(const QString &left, const QString &right)
{
    const QVector<int> a = parseVersionTriple(left);
    const QVector<int> b = parseVersionTriple(right);
    for (int i = 0; i < 3; ++i) {
        if (a.at(i) != b.at(i))
            return a.at(i) < b.at(i) ? -1 : 1;
    }
    return 0;
}

bool isStableChannel(const QString &channel)
{
    const QString value = channel.trimmed().toLower();
    // v2 reported "default", v3 reports "STABLE"
    return value.isEmpty() || value == QLatin1String("stable") || value == QLatin1String("default")
           || value == QLatin1String("recommended");
}

bool isPrereleaseId(const QString &id)
{
    const QString value = id.toLower();
    return value.contains(QLatin1String("-pre")) || value.contains(QLatin1String("-rc"))
           || value.contains(QLatin1String("-beta")) || value.contains(QLatin1String("snapshot"));
}

} // namespace

QJsonObject ResolvedServer::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("mcVersion"), mcVersion);
    object.insert(QStringLiteral("build"), build);
    object.insert(QStringLiteral("jarUrl"), jarUrl);
    object.insert(QStringLiteral("jarName"), jarName);
    object.insert(QStringLiteral("javaMajor"), javaMajor);
    object.insert(QStringLiteral("channel"), channel);
    object.insert(QStringLiteral("releaseTime"), releaseTime);
    object.insert(QStringLiteral("notes"), notes);
    return object;
}

QStringList VersionResolver::supportedTypes()
{
    return {QStringLiteral("vanilla"), QStringLiteral("paper"), QStringLiteral("purpur"),
            QStringLiteral("fabric")};
}

bool VersionResolver::isSupported(const QString &type)
{
    return supportedTypes().contains(type.toLower());
}

int VersionResolver::recommendedJavaMajor(const QString &mcVersion)
{
    const QString version = mcVersion.trimmed();
    if (version.isEmpty())
        return 17;
    const QVector<int> parts = parseVersionTriple(version);
    // Minecraft switched to a year based scheme (26.x) which requires Java 25.
    if (parts.at(0) >= 26)
        return 25;
    if (versionAtLeast(version, 1, 20, 5))
        return 21;
    if (versionAtLeast(version, 1, 17, 0))
        return 17;
    if (versionAtLeast(version, 1, 16, 0))
        return 16;
    if (parts.at(0) == 0)
        return 21; // unknown/snapshot style version: assume a modern JDK
    return 8;
}

QString VersionResolver::javaImage(int major)
{
    static const QSet<int> known = {8, 11, 16, 17, 21, 25};
    int resolved = major;
    if (!known.contains(resolved)) {
        if (resolved <= 8)
            resolved = 8;
        else if (resolved <= 11)
            resolved = 11;
        else if (resolved <= 17)
            resolved = 17;
        else if (resolved <= 21)
            resolved = 21;
        else
            resolved = 25;
    }
    // Temurin publishes 16 only as "16-jre" up to 16.0.2; fall back to 17 for 16.
    if (resolved == 16)
        resolved = 17;
    return QStringLiteral("eclipse-temurin:%1-jre").arg(resolved);
}

QString VersionResolver::javaLabel(int major)
{
    if (major == 16)
        return QStringLiteral("JDK 17 (兼容 1.16)");
    return QStringLiteral("JDK %1").arg(major);
}

QStringList VersionResolver::listGameVersions(const QString &type, QString *error)
{
    const QVector<ResolvedServer> metadata = listVersionMetadata(type, error);
    QStringList versions;
    versions.reserve(metadata.size());
    for (const ResolvedServer &item : metadata)
        versions << item.mcVersion;
    return versions;
}

QVector<ResolvedServer> VersionResolver::listVersionMetadata(const QString &type, QString *error)
{
    const QString flavour = type.toLower();
    QVector<ResolvedServer> result;

    if (flavour == QLatin1String("vanilla")) {
        QJsonObject manifest = fetchJson({QString::fromLatin1(kMojangManifest),
                                          QString::fromLatin1(kMojangManifestFallback)}, error);
        if (manifest.isEmpty())
            return result;
        const QJsonArray versions = manifest.value(QStringLiteral("versions")).toArray();
        for (const QJsonValue &value : versions) {
            const QJsonObject entry = value.toObject();
            if (Json::str(entry, QStringLiteral("type")) != QLatin1String("release"))
                continue;
            ResolvedServer item;
            item.type = flavour;
            item.mcVersion = Json::str(entry, QStringLiteral("id"));
            item.releaseTime = Json::str(entry, QStringLiteral("releaseTime"));
            item.jarUrl = Json::str(entry, QStringLiteral("url")); // resolved later via version json
            item.jarName = QStringLiteral("server.jar");
            item.javaMajor = recommendedJavaMajor(item.mcVersion);
            item.channel = QStringLiteral("release");
            result.append(item);
        }
        return result;
    }

    if (flavour == QLatin1String("paper")) {
        const QJsonObject project = fetchJson({QStringLiteral("%1/projects/paper").arg(QLatin1String(kPaperApi))}, error);
        if (project.isEmpty())
            return result;
        // v3 shape: {"versions": {"1.21": ["1.21.11", "1.21.11-rc3", ...], "26.3": [...]}}
        QStringList versions;
        const QJsonObject families = project.value(QStringLiteral("versions")).toObject();
        for (auto it = families.constBegin(); it != families.constEnd(); ++it) {
            const QJsonArray entries = it.value().toArray();
            for (const QJsonValue &entry : entries) {
                const QString id = entry.toString();
                if (id.isEmpty() || isPrereleaseId(id))
                    continue;
                versions << id;
            }
        }
        if (versions.isEmpty()) {
            if (error)
                *error = QStringLiteral("paper api returned no usable versions");
            return result;
        }
        std::sort(versions.begin(), versions.end(), [](const QString &left, const QString &right) {
            return compareVersionIds(left, right) > 0;
        });
        versions.removeDuplicates();
        for (const QString &version : versions) {
            ResolvedServer item;
            item.type = flavour;
            item.mcVersion = version;
            item.javaMajor = recommendedJavaMajor(version);
            item.channel = QStringLiteral("stable");
            result.append(item);
        }
        return result;
    }

    if (flavour == QLatin1String("purpur")) {
        const QJsonObject project = fetchJson({QStringLiteral("%1/purpur").arg(QLatin1String(kPurpurApi))}, error);
        if (project.isEmpty())
            return result;
        const QStringList versions = versionArrayToList(project, QStringLiteral("versions"));
        QStringList sorted = versions;
        std::sort(sorted.begin(), sorted.end(), [](const QString &left, const QString &right) {
            return compareVersionIds(left, right) > 0;
        });
        for (const QString &version : sorted) {
            ResolvedServer item;
            item.type = flavour;
            item.mcVersion = version;
            item.javaMajor = recommendedJavaMajor(version);
            item.channel = QStringLiteral("stable");
            result.append(item);
        }
        return result;
    }

    if (flavour == QLatin1String("fabric")) {
        const QJsonArray games = [] {
            const HttpReply reply = HttpClient::get(QUrl(QStringLiteral("%1/versions/game").arg(QLatin1String(kFabricApi))));
            if (!reply.ok)
                return QJsonArray();
            return QJsonDocument::fromJson(reply.body).array();
        }();
        if (games.isEmpty()) {
            if (error)
                *error = QStringLiteral("cannot query fabric meta api");
            return result;
        }
        for (const QJsonValue &value : games) {
            const QJsonObject entry = value.toObject();
            if (!Json::boolean(entry, QStringLiteral("stable"), false))
                continue;
            ResolvedServer item;
            item.type = flavour;
            item.mcVersion = Json::str(entry, QStringLiteral("version"));
            item.javaMajor = recommendedJavaMajor(item.mcVersion);
            item.channel = QStringLiteral("stable");
            result.append(item);
        }
        return result;
    }

    if (error)
        *error = QStringLiteral("unsupported server type '%1'").arg(type);
    return result;
}

bool VersionResolver::resolve(const QString &type,
                              const QString &mcVersion,
                              ResolvedServer *out,
                              QString *error)
{
    if (!out) {
        if (error)
            *error = QStringLiteral("null output");
        return false;
    }
    const QString flavour = type.toLower();
    ResolvedServer resolved;
    resolved.type = flavour;
    resolved.mcVersion = mcVersion;
    resolved.jarName = QStringLiteral("server.jar");
    resolved.javaMajor = recommendedJavaMajor(mcVersion);

    if (flavour == QLatin1String("vanilla")) {
        QJsonObject manifest = fetchJson({QString::fromLatin1(kMojangManifest),
                                          QString::fromLatin1(kMojangManifestFallback)}, error);
        if (manifest.isEmpty())
            return false;
        QString versionUrl;
        const QJsonArray versions = manifest.value(QStringLiteral("versions")).toArray();
        for (const QJsonValue &value : versions) {
            const QJsonObject entry = value.toObject();
            if (Json::str(entry, QStringLiteral("id")) == mcVersion) {
                versionUrl = Json::str(entry, QStringLiteral("url"));
                break;
            }
        }
        if (versionUrl.isEmpty()) {
            if (error)
                *error = QStringLiteral("version %1 not found in mojang manifest").arg(mcVersion);
            return false;
        }
        const HttpReply reply = HttpClient::get(QUrl(versionUrl));
        if (!reply.ok) {
            if (error)
                *error = reply.error;
            return false;
        }
        QJsonObject versionJson;
        if (!reply.parseJson(&versionJson)) {
            if (error)
                *error = QStringLiteral("invalid version metadata for %1").arg(mcVersion);
            return false;
        }
        const QJsonObject downloads = versionJson.value(QStringLiteral("downloads")).toObject();
        const QJsonObject server = downloads.value(QStringLiteral("server")).toObject();
        resolved.jarUrl = Json::str(server, QStringLiteral("url"));
        resolved.channel = QStringLiteral("release");
        // Mojang publishes the exact required JDK for every version; prefer it
        // over our heuristic.
        const int declaredJava = Json::integer(versionJson.value(QStringLiteral("javaVersion")).toObject(),
                                               QStringLiteral("majorVersion"), 0);
        if (declaredJava > 0)
            resolved.javaMajor = declaredJava;
        if (resolved.jarUrl.isEmpty()) {
            if (error)
                *error = QStringLiteral("no dedicated server download for %1").arg(mcVersion);
            return false;
        }
        *out = resolved;
        return true;
    }

    if (flavour == QLatin1String("paper")) {
        // v3 returns a plain array of builds with downloads keyed by "server:default".
        const QJsonArray list = fetchJsonArray(
            {QStringLiteral("%1/projects/paper/versions/%2/builds")
                 .arg(QLatin1String(kPaperApi), mcVersion)},
            error);
        if (list.isEmpty()) {
            if (error && error->isEmpty())
                *error = QStringLiteral("no paper build for %1").arg(mcVersion);
            return false;
        }

        QJsonObject chosen;
        int chosenId = -1;
        for (const QJsonValue &value : list) {
            const QJsonObject candidate = value.toObject();
            if (!isStableChannel(Json::str(candidate, QStringLiteral("channel"))))
                continue;
            const int id = Json::integer(candidate, QStringLiteral("id"), -1);
            if (id > chosenId) {
                chosenId = id;
                chosen = candidate;
            }
        }
        if (chosen.isEmpty()) {
            // no stable build yet: fall back to the newest build of any channel
            for (const QJsonValue &value : list) {
                const QJsonObject candidate = value.toObject();
                const int id = Json::integer(candidate, QStringLiteral("id"), -1);
                if (id > chosenId) {
                    chosenId = id;
                    chosen = candidate;
                }
            }
        }
        if (chosen.isEmpty()) {
            if (error)
                *error = QStringLiteral("no usable paper build for %1").arg(mcVersion);
            return false;
        }

        const QJsonObject downloads = chosen.value(QStringLiteral("downloads")).toObject();
        QJsonObject server = downloads.value(QStringLiteral("server:default")).toObject();
        if (server.isEmpty()) {
            for (auto it = downloads.constBegin(); it != downloads.constEnd(); ++it) {
                if (it.key().startsWith(QLatin1String("server")) && it.value().isObject()) {
                    server = it.value().toObject();
                    break;
                }
            }
        }
        resolved.jarUrl = Json::str(server, QStringLiteral("url"));
        if (resolved.jarUrl.isEmpty()) {
            if (error)
                *error = QStringLiteral("paper build %1 has no server download").arg(chosenId);
            return false;
        }
        resolved.build = QString::number(chosenId);
        resolved.channel = Json::str(chosen, QStringLiteral("channel"), QStringLiteral("stable"));
        resolved.notes = Json::str(server, QStringLiteral("name"));
        *out = resolved;
        return true;
    }

    if (flavour == QLatin1String("purpur")) {
        const QJsonObject latest = fetchJson(
            {QStringLiteral("%1/purpur/%2/latest").arg(QLatin1String(kPurpurApi), mcVersion)},
            error);
        if (latest.isEmpty())
            return false;
        resolved.build = Json::str(latest, QStringLiteral("build"));
        resolved.jarUrl = QStringLiteral("%1/purpur/%2/latest/download")
                              .arg(QLatin1String(kPurpurApi), mcVersion);
        resolved.channel = QStringLiteral("default");
        *out = resolved;
        return true;
    }

    if (flavour == QLatin1String("fabric")) {
        const QJsonArray loaders = [] {
            const HttpReply reply = HttpClient::get(
                QUrl(QStringLiteral("%1/versions/loader").arg(QLatin1String(kFabricApi))));
            if (!reply.ok)
                return QJsonArray();
            return QJsonDocument::fromJson(reply.body).array();
        }();
        const QJsonArray installers = [] {
            const HttpReply reply = HttpClient::get(
                QUrl(QStringLiteral("%1/versions/installer").arg(QLatin1String(kFabricApi))));
            if (!reply.ok)
                return QJsonArray();
            return QJsonDocument::fromJson(reply.body).array();
        }();
        if (loaders.isEmpty() || installers.isEmpty()) {
            if (error)
                *error = QStringLiteral("cannot query fabric meta api");
            return false;
        }
        QString loaderVersion;
        for (const QJsonValue &value : loaders) {
            const QJsonObject entry = value.toObject().value(QStringLiteral("loader")).toObject();
            if (Json::boolean(entry, QStringLiteral("stable"), false)) {
                loaderVersion = Json::str(entry, QStringLiteral("version"));
                break;
            }
        }
        if (loaderVersion.isEmpty())
            loaderVersion = Json::str(loaders.first().toObject().value(QStringLiteral("loader")).toObject(),
                                      QStringLiteral("version"));
        QString installerVersion;
        for (const QJsonValue &value : installers) {
            const QJsonObject entry = value.toObject();
            if (Json::boolean(entry, QStringLiteral("stable"), false)) {
                installerVersion = Json::str(entry, QStringLiteral("version"));
                break;
            }
        }
        if (installerVersion.isEmpty())
            installerVersion = Json::str(installers.first().toObject(), QStringLiteral("version"));

        resolved.build = loaderVersion;
        resolved.jarUrl = QStringLiteral("%1/versions/loader/%2/%3/%4/server/jar")
                              .arg(QLatin1String(kFabricApi), mcVersion, loaderVersion, installerVersion);
        resolved.channel = QStringLiteral("stable");
        resolved.notes = QStringLiteral("Fabric loader %1 (installer %2)").arg(loaderVersion, installerVersion);
        *out = resolved;
        return true;
    }

    if (error)
        *error = QStringLiteral("unsupported server type '%1'").arg(type);
    return false;
}

} // namespace mcsm
