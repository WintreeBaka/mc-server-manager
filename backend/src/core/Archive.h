#pragma once

#include <QString>
#include <QStringList>

namespace mcsm {

/// Zip packing / unpacking on top of the 7-Zip binary that ships with the app.
///
/// The manager deliberately does not depend on the machine having an archiver
/// installed: `third_party/7zip/7za.exe` is bundled with every build and is
/// looked up next to the executable first. A system 7-Zip / bsdtar is only used
/// as a fallback so a stripped down development checkout still works.
class Archive
{
public:
    /// Path of the archiver that will be used (empty when nothing was found).
    static QString toolPath();
    /// Human readable description of the resolved tool, e.g. "bundled 7za".
    static QString toolDescription();
    /// Directories that were searched, for `doctor` style diagnostics.
    static QStringList toolCandidates();

    /// True when the file starts with the zip magic bytes ("PK").
    static bool isZipArchive(const QString &path);

    static bool extractZip(const QString &archivePath,
                           const QString &destination,
                           QString *error,
                           QString *log = nullptr);

    /// Creates a zip from `entries` (file system paths) inside `workingDir`.
    static bool createZip(const QStringList &entries,
                          const QString &outputPath,
                          const QString &workingDirectory,
                          QString *error);

    /// Recursively deletes a directory, ignoring failures caused by it not
    /// existing. Used to clean up staging folders.
    static bool removeTree(const QString &path, QString *error = nullptr);
};

} // namespace mcsm
