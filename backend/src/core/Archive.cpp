#include "core/Archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "core/Logger.h"
#include "core/ProcessRunner.h"

namespace mcsm {
namespace {

QStringList toolRoots()
{
    QStringList roots;
    const QString appDir = QCoreApplication::applicationDirPath();
    roots << appDir;
    if (!appDir.isEmpty())
        roots << QDir::cleanPath(appDir + QStringLiteral("/.."))
              << QDir::cleanPath(appDir + QStringLiteral("/../.."))
              << QDir::cleanPath(appDir + QStringLiteral("/../../.."));
    return roots;
}

/// First existing candidate out of the bundled locations.
QString bundledTool()
{
    const QStringList relative = {
        QStringLiteral("7zip/7za.exe"),
        QStringLiteral("third_party/7zip/7za.exe"),
        QStringLiteral("tools/7zip/7za.exe"),
    };
    const QStringList roots = toolRoots();
    for (const QString &root : roots) {
        for (const QString &rel : relative) {
            const QString path = QDir(root).filePath(rel);
            if (QFileInfo::exists(path))
                return QDir::cleanPath(path);
        }
    }
    return QString();
}

} // namespace

QStringList Archive::toolCandidates()
{
    QStringList candidates;
    const QString env = QString::fromLocal8Bit(qgetenv("MCSM_7ZA")).trimmed();
    if (!env.isEmpty())
        candidates << env;
    const QStringList relative = {
        QStringLiteral("7zip/7za.exe"),
        QStringLiteral("third_party/7zip/7za.exe"),
        QStringLiteral("tools/7zip/7za.exe"),
    };
    for (const QString &root : toolRoots()) {
        for (const QString &rel : relative)
            candidates << QDir::cleanPath(QDir(root).filePath(rel));
    }
    candidates << QStringLiteral("C:/Program Files/7-Zip/7z.exe")
               << QStringLiteral("C:/Program Files/7-Zip/7za.exe")
               << QStringLiteral("7z") << QStringLiteral("7za");
    return candidates;
}

QString Archive::toolPath()
{
    const QString env = QString::fromLocal8Bit(qgetenv("MCSM_7ZA")).trimmed();
    if (!env.isEmpty() && QFileInfo::exists(env))
        return QDir::cleanPath(env);
    const QString bundled = bundledTool();
    if (!bundled.isEmpty())
        return bundled;
    return ProcessRunner::resolveProgram({QStringLiteral("7z"),
                                          QStringLiteral("7za"),
                                          QStringLiteral("C:/Program Files/7-Zip/7z.exe")});
}

QString Archive::toolDescription()
{
    const QString tool = toolPath();
    if (tool.isEmpty())
        return QStringLiteral("未找到解压工具");
    const QString bundled = bundledTool();
    if (!tool.isEmpty() && tool == bundled)
        return QStringLiteral("内置 7-Zip (%1)").arg(tool);
    if (tool.contains(QLatin1String("7-Zip"), Qt::CaseInsensitive))
        return QStringLiteral("系统 7-Zip (%1)").arg(tool);
    return QStringLiteral("系统 tar/7z (%1)").arg(tool);
}

bool Archive::isZipArchive(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray magic = file.read(4);
    file.close();
    return magic.startsWith("PK");
}

bool Archive::extractZip(const QString &archivePath, const QString &destination,
                         QString *error, QString *log)
{
    if (!QFileInfo::exists(archivePath)) {
        if (error)
            *error = QStringLiteral("压缩包不存在：%1").arg(archivePath);
        return false;
    }
    if (!QDir().mkpath(destination)) {
        if (error)
            *error = QStringLiteral("无法创建解压目录：%1").arg(destination);
        return false;
    }

    const QString tool = toolPath();
    if (tool.isEmpty()) {
        if (error)
            *error = QStringLiteral("未找到可用的解压工具（内置 7zip 缺失）");
        return false;
    }

    // `-o<dir>` must be a single token: 7-Zip rejects a separated value.
    const ProcessResult result = ProcessRunner::run(
        tool,
        {QStringLiteral("x"), QDir::toNativeSeparators(archivePath),
         QStringLiteral("-o%1").arg(QDir::toNativeSeparators(destination)),
         QStringLiteral("-y"), QStringLiteral("-bso0"), QStringLiteral("-bsp0")},
        180000);

    if (log)
        *log = result.combined();
    if (!result.ok()) {
        if (error)
            *error = result.errorText();
        Logger::warn(QStringLiteral("archive"),
                     QStringLiteral("extract failed: %1").arg(result.errorText()));
        return false;
    }
    return true;
}

bool Archive::createZip(const QStringList &entries, const QString &outputPath,
                        const QString &workingDirectory, QString *error)
{
    if (entries.isEmpty()) {
        if (error)
            *error = QStringLiteral("没有需要打包的文件");
        return false;
    }
    const QString tool = toolPath();
    if (tool.isEmpty()) {
        if (error)
            *error = QStringLiteral("未找到可用的压缩工具（内置 7zip 缺失）");
        return false;
    }
    QStringList args {QStringLiteral("a"), QStringLiteral("-tzip"),
                      QDir::toNativeSeparators(outputPath)};
    args << entries;
    args << QStringLiteral("-y") << QStringLiteral("-bso0") << QStringLiteral("-bsp0");
    const ProcessResult result = ProcessRunner::run(tool, args, 180000, QByteArray(), workingDirectory);
    if (!result.ok()) {
        if (error)
            *error = result.errorText();
        return false;
    }
    return true;
}

bool Archive::removeTree(const QString &path, QString *error)
{
    QDir dir(path);
    if (!dir.exists())
        return true;
    if (!dir.removeRecursively()) {
        if (error)
            *error = QStringLiteral("无法删除目录：%1").arg(path);
        return false;
    }
    return true;
}

} // namespace mcsm
