#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>

#include "core/JsonUtil.h"
#include "server/ServerRecord.h"
#include "server/ServerStore.h"

namespace mcsm {

struct InstallRequest
{
    QString name;
    QString type = QStringLiteral("paper");
    QString mcVersion;
    QString preferredId;
    int port = 0;              // 0 = auto allocate
    int rconPort = 0;          // 0 = auto allocate
    QString memory = QStringLiteral("4G");
    QString memoryLimit;
    QString levelName = QStringLiteral("world");
    QString motd;
    QString gamemode;
    QString difficulty;
    int maxPlayers = 20;
    int javaMajor = 0;         // 0 = auto detect from minecraft version
    QString image;             // optional explicit docker image
    QString javaOptions;
    bool acceptEula = true;
    bool manual = false;       // user supplied jar
    bool skipDocker = false;
    bool autoRestart = true;
    bool autoStart = false;
    bool reuseExistingJar = true;
    QString manualJarPath;
    /// "docker" (default) or "host" (experimental: clone a JDK from this machine)
    QString runtime = QStringLiteral("docker");
    QString jdkHome;           // JDK to clone when runtime == host
    QJsonObject overrides;     // additional server.properties keys
};

using InstallProgressFn = std::function<void(const QString &stage, int percent, const QString &detail)>;

/// One click auto setup: resolves the version, downloads the jar, prepares the
/// JDK container image and writes every generated file.
class ServerInstaller
{
public:
    static Result install(ServerStore &store,
                          const InstallRequest &request,
                          const InstallProgressFn &progress = InstallProgressFn());

    static bool downloadJar(ServerRecord *record, QString *error, const InstallProgressFn &progress);
    static bool importJar(const ServerRecord &record, const QString &sourceJar, QString *error);
    /// Copies a JDK directory into the server folder (experimental host runtime).
    static bool cloneJdk(const QString &sourceHome,
                         const QString &targetHome,
                         QString *error,
                         const InstallProgressFn &progress = InstallProgressFn());
    static bool prepareDirectory(const ServerRecord &record,
                                 const QMap<QString, QString> &propertyOverrides,
                                 QString *error);
    static Result deleteServer(ServerStore &store, const QString &id, bool purgeFiles);
};

} // namespace mcsm
