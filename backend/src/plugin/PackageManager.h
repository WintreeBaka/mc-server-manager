#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/JsonUtil.h"

namespace mcsm {

/// Where a plugin package plugs in.
enum class PluginScope { Frontend, Backend, Web, Global, Unknown };

/// One entry of `plugin.json`, merged with the local install metadata.
struct PluginManifest
{
    QString id;
    QString name;
    QString version;
    QString apiVersion = QStringLiteral("1");
    QString description;
    QString author;
    QString homepage;
    QString license;
    QStringList permissions;
    QStringList hooks;
    PluginScope scope = PluginScope::Unknown;
    QString scopeToken;                 // frontend | backend | web | global | auto
    QStringList parts;                  // any of: frontend, backend, web

    QJsonObject frontend;               // { entry, styles[], assets[] }
    QJsonObject backend;                // { entry, runtime, args[], timeoutMs }
    QJsonObject web;                    // { entry, service, port, title }
    QJsonObject contributes;            // { pages[], menus[], serverActions[] }
    QJsonObject raw;                    // plugin.json as written

    QString path;                       // install directory
    bool enabled = true;

    QString installedAt;
    QString source;                     // "zip" | "url" | "manual"
    QString sourceUrl;

    bool hasFrontend() const { return parts.contains(QStringLiteral("frontend")); }
    bool hasBackend() const { return parts.contains(QStringLiteral("backend")); }
    bool hasWeb() const { return parts.contains(QStringLiteral("web")); }

    QString frontendEntry() const;
    QString backendEntry() const;
    QString webEntry() const;
    int webPort() const;

    QJsonObject toJson() const;

    static QString scopeName(PluginScope scope);
    static QString scopeLabel(PluginScope scope);
};

struct PluginInstallRequest
{
    QString zipPath;             // local archive
    QString url;                 // remote archive (mutually exclusive with zip)
    QString expectId;            // optional: refuse to install a different id
    bool force = false;          // overwrite an existing install
    bool enable = true;
};

struct PluginValidation
{
    bool ok = false;
    QStringList errors;
    QStringList warnings;
    PluginManifest manifest;
};

/// Install / enable / remove plugin **packages** (frontend + backend + web
/// extension bundles). Not to be confused with `PluginManager`, which manages
/// the Minecraft server plug-ins inside a server's `plugins` folder.
class PackageManager
{
public:
    static QString apiVersion();

    static QVector<PluginManifest> installed(bool includeDisabled = true);
    static PluginManifest find(const QString &id);
    static bool exists(const QString &id);

    /// Reads a plugin directory (a zip that was already extracted).
    static PluginValidation inspectDirectory(const QString &directory, QString *error);
    /// Reads a plugin directory that contains plugins/*/ dirs, e.g. a `.mcpack`
    /// bundle or a zip with one top level folder. Returns the real root.
    static QString locatePluginRoot(const QString &directory);

    static Result install(const PluginInstallRequest &request);
    static Result remove(const QString &id, bool purgeData = false);
    static Result setEnabled(const QString &id, bool enabled);

    /// Machine readable description of the whole plugin API. Backs
    /// `mcsm-cli plugin api` so tooling and the docs stay in sync.
    static QJsonObject apiManifest();

    /// Enabled plugins that declared a frontend part (the GUI executes these).
    static QVector<PluginManifest> frontendPlugins();

private:
    static QJsonObject readRegistry();
    static bool writeRegistry(const QJsonObject &registry, QString *error);
    static bool writeInstallMetadata(const PluginManifest &manifest, QString *error);
    static bool copyTree(const QString &from, const QString &to, QStringList *copied, QString *error);
    static QString normalizeId(const QString &raw);
    static PluginScope parseScope(const QString &token, const QStringList &parts);
};

} // namespace mcsm
