#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace mcsm {

struct ResolvedServer
{
    QString type;         // vanilla | paper | purpur | fabric
    QString mcVersion;    // 1.20.4
    QString build;        // 496 (paper build / purpur build / fabric loader)
    QString jarUrl;       // direct download url
    QString jarName;      // server.jar
    int javaMajor = 17;
    QString channel;      // default / experimental
    QString releaseTime;  // ISO timestamp (vanilla manifest)
    QString notes;

    QJsonObject toJson() const;
};

class VersionResolver
{
public:
    static QStringList supportedTypes();
    static bool isSupported(const QString &type);

    /// Lists installable game versions for a server flavour (newest first).
    static QStringList listGameVersions(const QString &type, QString *error);

    /// Full metadata for the GUI list: id / releaseTime / javaMajor / type.
    static QVector<ResolvedServer> listVersionMetadata(const QString &type, QString *error);

    /// Resolves the concrete jar url + recommended JDK for auto setup.
    static bool resolve(const QString &type,
                        const QString &mcVersion,
                        ResolvedServer *out,
                        QString *error);

    /// Minecraft -> required JDK major version.
    static int recommendedJavaMajor(const QString &mcVersion);

    /// Friendly label + docker image for a JDK major version.
    static QString javaImage(int major);
    static QString javaLabel(int major);
};

} // namespace mcsm
