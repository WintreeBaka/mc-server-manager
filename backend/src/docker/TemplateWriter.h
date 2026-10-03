#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace mcsm {

struct ServerRecord;

/// Writes every generated file for a server directory (start script, eula,
/// docker compose for reference, default server.properties).
class TemplateWriter
{
public:
    static bool writeStartScript(const ServerRecord &record, QString *error);
    static bool writeHostStartScript(const ServerRecord &record, QString *error);
    static bool writeEula(const ServerRecord &record, bool accepted, QString *error);
    static bool writeComposeFile(const ServerRecord &record, QString *error);
    static bool writeDefaultProperties(const ServerRecord &record, bool overwrite);
    static bool writeReadme(const ServerRecord &record, QString *error);
    static QString startScriptContent(const ServerRecord &record);
    static QString hostStartScriptContent(const ServerRecord &record);
    static QString hostStartScriptName();
    static QString compressTarCommand(const QStringList &entries, const QString &outputName);

    static const QStringList &protectedEntries();
};

} // namespace mcsm
