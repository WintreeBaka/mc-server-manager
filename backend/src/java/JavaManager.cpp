#include "java/JavaManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

#include "core/JsonUtil.h"
#include "core/Logger.h"
#include "core/ProcessRunner.h"
#include "core/StringUtil.h"
#include "docker/DockerManager.h"
#include "net/VersionResolver.h"

namespace mcsm {
namespace {

QString javaExecutableName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("java.exe");
#else
    return QStringLiteral("java");
#endif
}

int majorFromVersion(const QString &version)
{
    static const QRegularExpression re(QStringLiteral("^(\\d+)(?:\\.(\\d+))?"));
    const QRegularExpressionMatch match = re.match(version.trimmed());
    if (!match.hasMatch())
        return 0;
    const int first = match.captured(1).toInt();
    return first == 1 ? match.captured(2).toInt() : first;
}

/// <home>/release is written by every official JDK build; reading it avoids
/// spawning a JVM per candidate.
bool readReleaseFile(const QString &home, QString *version, QString *vendor)
{
    QFile file(QDir(home).filePath(QStringLiteral("release")));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    static const QRegularExpression versionRe(QStringLiteral("JAVA_VERSION=\"([^\"]+)\""));
    const QRegularExpressionMatch versionMatch = versionRe.match(text);
    if (!versionMatch.hasMatch())
        return false;
    if (version)
        *version = versionMatch.captured(1);
    if (vendor) {
        static const QRegularExpression vendorRe(QStringLiteral("IMPLEMENTOR=\"([^\"]+)\""));
        const QRegularExpressionMatch vendorMatch = vendorRe.match(text);
        *vendor = vendorMatch.hasMatch() ? vendorMatch.captured(1) : QString();
    }
    return true;
}

qint64 directorySize(const QString &path)
{
    qint64 total = 0;
    QDirIterator iterator(path, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        total += iterator.fileInfo().size();
    }
    return total;
}

QString vendorFromOutput(const QString &text)
{
    if (text.contains(QLatin1String("Temurin")))
        return QStringLiteral("Eclipse Temurin");
    if (text.contains(QLatin1String("Oracle")))
        return QStringLiteral("Oracle");
    if (text.contains(QLatin1String("Microsoft")))
        return QStringLiteral("Microsoft");
    if (text.contains(QLatin1String("Zulu")))
        return QStringLiteral("Azul Zulu");
    if (text.contains(QLatin1String("Corretto")))
        return QStringLiteral("Amazon Corretto");
    if (text.contains(QLatin1String("OpenJDK")))
        return QStringLiteral("OpenJDK");
    return QString();
}

} // namespace

QJsonObject JavaManager::HostJdk::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("home"), home);
    object.insert(QStringLiteral("javaPath"), javaPath);
    object.insert(QStringLiteral("version"), version);
    object.insert(QStringLiteral("vendor"), vendor);
    object.insert(QStringLiteral("major"), major);
    object.insert(QStringLiteral("sizeBytes"), double(sizeBytes));
    object.insert(QStringLiteral("sizeText"), StringUtil::humanBytes(sizeBytes));
    object.insert(QStringLiteral("source"), source);
    return object;
}

JavaManager::HostJava JavaManager::detectHostJava()
{
    HostJava result;
    const QString java = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (java.isEmpty())
        return result;
    const ProcessResult probe = ProcessRunner::run(java, {QStringLiteral("-version")}, 20000);
    const QString text = probe.combined();
    if (text.trimmed().isEmpty())
        return result;
    static const QRegularExpression re(QStringLiteral("\"([0-9]+)(?:\\.([0-9]+))?[^\"]*\""));
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return result;
    result.found = true;
    result.version = match.captured(0);
    result.major = match.captured(1).toInt();
    if (result.major == 1)
        result.major = match.captured(2).toInt();
    return result;
}

JavaManager::HostJdk JavaManager::describeJdk(const QString &home, const QString &source)
{
    HostJdk jdk;
    const QDir dir(home);
    if (!dir.exists())
        return jdk;
    const QString javaPath = dir.filePath(QStringLiteral("bin/") + javaExecutableName());
    if (!QFileInfo::exists(javaPath))
        return jdk;

    jdk.home = QDir::cleanPath(dir.absolutePath());
    jdk.javaPath = javaPath;
    jdk.source = source;

    QString version;
    QString vendor;
    if (!readReleaseFile(jdk.home, &version, &vendor)) {
        const ProcessResult probe = ProcessRunner::run(javaPath, {QStringLiteral("-version")}, 15000);
        const QString text = probe.combined();
        static const QRegularExpression re(QStringLiteral("\"([0-9][^\"]*)\""));
        const QRegularExpressionMatch match = re.match(text);
        if (match.hasMatch())
            version = match.captured(1);
        vendor = vendorFromOutput(text);
    }
    jdk.version = version;
    jdk.vendor = vendor;
    jdk.major = majorFromVersion(version);
    if (jdk.major <= 0)
        return HostJdk {};
    jdk.sizeBytes = directorySize(jdk.home);
    return jdk;
}

QVector<JavaManager::HostJdk> JavaManager::scanHostJdks()
{
    QStringList homes;    // directories that are JDK homes themselves
    QStringList parents;  // directories whose children are usually JDK homes

    const QString javaHome = qEnvironmentVariable("JAVA_HOME");
    if (!javaHome.isEmpty())
        homes << javaHome;

    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    const QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QStringList vendors = {QStringLiteral("Java"), QStringLiteral("Eclipse Adoptium"),
                                 QStringLiteral("Eclipse Foundation"), QStringLiteral("Microsoft"),
                                 QStringLiteral("Zulu"), QStringLiteral("Amazon Corretto"),
                                 QStringLiteral("BellSoft"), QStringLiteral("Semeru"),
                                 QStringLiteral("AdoptOpenJDK"), QStringLiteral("ojdkbuild"),
                                 QStringLiteral("RedHat"), QStringLiteral("JetBrains")};
    for (const QString &root : {programFiles, programFilesX86}) {
        if (root.isEmpty())
            continue;
        for (const QString &vendor : vendors)
            parents << root + QLatin1Char('/') + vendor;
    }
    if (!localAppData.isEmpty()) {
        const QStringList localVendors = {QStringLiteral("Eclipse Adoptium"), QStringLiteral("Microsoft"),
                                          QStringLiteral("Zulu"), QStringLiteral("Java"),
                                          QStringLiteral("Amazon Corretto")};
        for (const QString &vendor : localVendors)
            parents << localAppData + QStringLiteral("/Programs/") + vendor;
    }
    for (const QString &drive : {QStringLiteral("C:"), QStringLiteral("D:"), QStringLiteral("E:")}) {
        parents << drive + QStringLiteral("/Java") << drive + QStringLiteral("/jdk")
                << drive + QStringLiteral("/Program Files/Java") << drive + QStringLiteral("/tools");
    }

    const QString pathJava = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (!pathJava.isEmpty() && !pathJava.contains(QLatin1String("javapath"), Qt::CaseInsensitive)) {
        const QString guessed = QFileInfo(QFileInfo(pathJava).absolutePath()).absolutePath();
        homes << guessed;
        parents << QFileInfo(guessed).absolutePath();
    }

    QStringList candidates = homes;
    for (const QString &parent : parents) {
        const QDir dir(parent);
        if (!dir.exists())
            continue;
        candidates << QDir::cleanPath(dir.absolutePath());
        const QFileInfoList children = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &child : children) {
            candidates << child.absoluteFilePath();
            const QFileInfoList grandChildren = QDir(child.absoluteFilePath())
                                                    .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &grandChild : grandChildren)
                candidates << grandChild.absoluteFilePath();
        }
    }

    QVector<HostJdk> found;
    QStringList seen;
    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);
        if (cleaned.isEmpty())
            continue;
        bool alreadyChecked = false;
        for (const QString &item : seen) {
            if (item.compare(cleaned, Qt::CaseInsensitive) == 0)
                alreadyChecked = true;
        }
        if (alreadyChecked)
            continue;
        seen << cleaned;

        const QString source = cleaned.compare(javaHome, Qt::CaseInsensitive) == 0
                                   ? QStringLiteral("JAVA_HOME")
                                   : (cleaned.contains(QLatin1String("Program"), Qt::CaseInsensitive)
                                          ? QStringLiteral("Program Files")
                                          : QString());
        const HostJdk jdk = describeJdk(cleaned, source);
        if (jdk.major <= 0)
            continue;
        bool duplicate = false;
        for (const HostJdk &existing : found) {
            if (existing.home.compare(jdk.home, Qt::CaseInsensitive) == 0)
                duplicate = true;
        }
        if (!duplicate)
            found.append(jdk);
    }

    std::sort(found.begin(), found.end(), [](const HostJdk &left, const HostJdk &right) {
        if (left.major != right.major)
            return left.major > right.major;
        return left.home < right.home;
    });
    Logger::info(QStringLiteral("java"), QStringLiteral("host jdk scan found %1").arg(found.size()));
    return found;
}

QStringList JavaManager::installedImages()
{
    return DockerManager::localImages(QStringLiteral("eclipse-temurin"));
}

QString JavaManager::imageForMajor(int major)
{
    return VersionResolver::javaImage(major);
}

QStringList JavaManager::knownMajors()
{
    return {QStringLiteral("8"), QStringLiteral("11"), QStringLiteral("17"), QStringLiteral("21")};
}

bool JavaManager::ensureImageForMajor(int major, QString *error)
{
    const QString image = imageForMajor(major);
    return DockerManager::ensureImage(image, error);
}

QJsonObject JavaManager::overview()
{
    QJsonObject object;
    const HostJava host = detectHostJava();
    QJsonObject hostObject;
    hostObject.insert(QStringLiteral("found"), host.found);
    hostObject.insert(QStringLiteral("version"), host.version);
    hostObject.insert(QStringLiteral("major"), host.major);
    object.insert(QStringLiteral("hostJava"), hostObject);

    QJsonArray images;
    for (const QString &image : installedImages())
        images.append(image);
    object.insert(QStringLiteral("dockerImages"), images);
    object.insert(QStringLiteral("recommendedImages"), Json::toStringArray({
        VersionResolver::javaImage(8),
        VersionResolver::javaImage(17),
        VersionResolver::javaImage(21),
    }));
    return object;
}

} // namespace mcsm
