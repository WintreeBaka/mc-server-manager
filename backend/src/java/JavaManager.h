#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace mcsm {

/// JDK handling. On purpose the JDK is never installed on the host: every
/// runtime lives inside a docker image, so different servers can use different
/// Java majors side by side.
class JavaManager
{
public:
    struct HostJava
    {
        bool found = false;
        QString version;
        int major = 0;
    };

    /// A JDK installation discovered on this machine.
    struct HostJdk
    {
        QString home;
        QString javaPath;
        QString version;   // e.g. 21.0.12.1
        QString vendor;
        int major = 0;
        qint64 sizeBytes = 0;
        QString source;    // JAVA_HOME / Program Files / PATH …

        QJsonObject toJson() const;
    };

    static HostJava detectHostJava();
    static QStringList installedImages();
    static bool ensureImageForMajor(int major, QString *error);
    static QJsonObject overview();

    /// Scans JAVA_HOME, PATH and the usual install locations for JDKs.
    static QVector<HostJdk> scanHostJdks();
    /// Inspects a single directory; returns an entry with major <= 0 when it is
    /// not a usable JDK.
    static HostJdk describeJdk(const QString &home, const QString &source = QString());

    static QStringList knownMajors();
    static QString imageForMajor(int major);
};

} // namespace mcsm
