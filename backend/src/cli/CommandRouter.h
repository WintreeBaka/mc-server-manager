#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "cli/ArgParser.h"
#include "core/JsonUtil.h"

namespace mcsm {

class CommandRouter
{
public:
    explicit CommandRouter(const QStringList &arguments);

    /// Executes a normal (single shot) command and returns the JSON envelope.
    Result run();

    /// True when the requested command streams NDJSON instead of one payload.
    bool isStreaming() const { return m_streaming; }
    QStringList helpLines() const;

private:
    Result dispatch(const QString &command);

    Result cmdDoctor();
    Result cmdTypes();
    Result cmdVersions();
    Result cmdJava();
    Result cmdInstall();
    Result cmdServer();
    Result cmdConfig();
    Result cmdBackup();
    Result cmdPlugin();
    Result cmdSchedule();
    Result cmdSettings();

    Args m_args;
    QString m_command;
    QString m_sub;
    bool m_streaming = false;
};

} // namespace mcsm
