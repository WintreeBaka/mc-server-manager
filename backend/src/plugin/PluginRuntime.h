#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/JsonUtil.h"
#include "plugin/PackageManager.h"
#include "server/ServerRecord.h"

namespace mcsm {

/// Outcome of a single `plugin call` / hook invocation.
struct PluginCallOutcome
{
    bool ok = false;
    QString pluginId;
    QString method;
    QString code;
    QString message;
    QString detail;
    QJsonObject data;
    QJsonObject patch;
    QStringList warnings;
    QStringList log;
    int exitCode = -1;
    qint64 elapsedMs = 0;

    QJsonObject toJson() const;
};

/// Executes plugin packages: backend JSON-line processes, lifecycle hooks, the
/// generated start patch and local web services.
class PluginRuntime
{
public:
    /// Invokes one method on one plugin (`invoke` is the generic RPC entry).
    static PluginCallOutcome call(const PluginManifest &manifest,
                                  const QString &method,
                                  const QJsonObject &params,
                                  int timeoutMs = 30000);

    /// Runs `hook` on every enabled plugin that declared it.
    static QVector<PluginCallOutcome> callHook(const QString &hook,
                                               const QJsonObject &payload,
                                               int timeoutMs = 30000);

    /// Runs `hook` and folds the outcomes into a Result (failures become
    /// warnings: a broken plugin must never block the manager).
    static Result notify(const QString &hook, const QJsonObject &payload);

    /// Convenience wrapper for the hook payload of one server.
    static QJsonObject serverPayload(const ServerRecord &record, const QJsonObject &extra = QJsonObject());

    /// Asks every enabled backend plugin for a start patch and stores the merged
    /// result in <server>/.mcsm/start-patch.json (removed when nothing applies).
    static Result refreshStartPatch(const ServerRecord &record);
    /// Reads the merged start patch written by refreshStartPatch().
    static QJsonObject readStartPatch(const ServerRecord &record);

    static Result serviceStart(const QString &pluginId);
    static Result serviceStop(const QString &pluginId);
    static Result serviceStatus(const QString &pluginId);

    /// Human readable description of how a plugin entry point will be launched.
    static QString describeRuntime(const PluginManifest &manifest, QString *error);

private:
    struct Launch
    {
        QString program;
        QStringList arguments;
        QString workingDirectory;
        QString label;
    };

    static bool resolveLaunch(const PluginManifest &manifest,
                              const QString &entryOverride,
                              Launch *launch,
                              QString *error);
    static QJsonObject serviceFile(const QString &pluginId);
    static bool writeServiceFile(const QString &pluginId, const QJsonObject &object);
};

} // namespace mcsm
