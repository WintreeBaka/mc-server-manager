#include "core/Archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "core/Logger.h"
#include "core/ProcessRunner.h"
#include "core/StringUtil.h"

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

    // Security: a package must never be able to write outside the plugin
    // directory (zip-slip / absolute paths / drive letters). The entries are
    // checked *before* extraction, and the result is verified afterwards.
    const QStringList entries = listEntries(archivePath);
    for (const QString &entry : entries) {
        if (!isSafeRelativePath(entry)) {
            if (error)
                *error = QStringLiteral("压缩包包含不安全路径，已拒绝解压：%1").arg(entry);
            Logger::warn(QStringLiteral("archive"),
                         QStringLiteral("refused unsafe entry '%1' in %2").arg(entry, archivePath));
            return false;
        }
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

    // Post-extraction check: nothing may have landed outside the destination
    // (covers symlinks and junctions that the listing cannot see through).
    QDirIterator iterator(destination,
                          QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden
                              | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (!isInside(destination, path)) {
            if (error)
                *error = QStringLiteral("压缩包试图写入目录之外的文件：%1").arg(path);
            Logger::warn(QStringLiteral("archive"),
                         QStringLiteral("extracted path escapes destination: %1").arg(path));
            return false;
        }
        if (QFileInfo(path).isSymLink()) {
            if (error)
                *error = QStringLiteral("压缩包包含符号链接，已拒绝：%1").arg(path);
            return false;
        }
    }
    return true;
}

QStringList Archive::listEntries(const QString &archivePath, QString *error)
{
    QStringList entries;
    const QString tool = toolPath();
    if (tool.isEmpty()) {
        if (error)
            *error = QStringLiteral("未找到可用的解压工具");
        return entries;
    }
    // -slt switches the listing to "technical" key = value lines
    const ProcessResult result = ProcessRunner::run(
        tool, {QStringLiteral("l"), QStringLiteral("-slt"),
               QDir::toNativeSeparators(archivePath)},
        60000);
    if (!result.ok()) {
        if (error)
            *error = result.errorText();
        return entries;
    }
    const QString archiveName = QFileInfo(archivePath).fileName();
    const QStringList lines = StringUtil::splitLines(result.stdOut);
    bool first = true;
    for (const QString &line : lines) {
        if (!line.startsWith(QLatin1String("Path = ")))
            continue;
        const QString value = line.mid(7).trimmed();
        if (value.isEmpty())
            continue;
        if (first) {
            // the first Path is the archive itself
            first = false;
            if (QFileInfo(value).fileName().compare(archiveName, Qt::CaseInsensitive) == 0)
                continue;
        }
        entries << value;
    }
    return entries;
}

bool Archive::isSafeRelativePath(const QString &path)
{
    if (path.isEmpty())
        return false;
    QString normalized = path;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (normalized.startsWith(QLatin1Char('/')))
        return false;
    // "C:" / UNC "//server/share"
    if (normalized.size() > 1 && normalized.at(1) == QLatin1Char(':'))
        return false;
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String(".."))
            return false;
        if (part.contains(QLatin1Char(':')))
            return false;
    }
    return !parts.isEmpty();
}

bool Archive::isInside(const QString &root, const QString &path)
{
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (canonicalRoot.isEmpty())
        return false;
    QString canonicalPath = QFileInfo(path).canonicalFilePath();
    if (canonicalPath.isEmpty())
        canonicalPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (canonicalPath.compare(canonicalRoot, Qt::CaseInsensitive) == 0)
        return true;
#ifdef Q_OS_WIN
    const QString prefix = canonicalRoot + QLatin1Char('/');
    return canonicalPath.startsWith(prefix, Qt::CaseInsensitive);
#else
    return canonicalPath.startsWith(canonicalRoot + QLatin1Char('/'));
#endif
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
