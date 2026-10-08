#pragma once

#include <QJsonArray>
#include <QJsonObject>

#include "plugin/PluginHost.h"
#include "widgets/Common.h"

class QLabel;
class QVBoxLayout;

namespace mcsm {

class LogView;

/// Hosts one page contributed by a plugin. The plugin describes the page as a
/// list of blocks (heading / text / keyvalue / table / buttons / log / html /
/// divider / progress) which are turned into real widgets here, so plugin UI
/// always matches the active theme.
class PluginPageView : public PageBase
{
    Q_OBJECT

public:
    explicit PluginPageView(const PluginPageInfo &info, QWidget *parent = nullptr);

    QString pageId() const { return m_info.id; }
    QString pluginId() const { return m_info.pluginId; }

    void setBlocks(const QJsonArray &blocks);

private:
    QWidget *buildBlock(const QJsonObject &block);
    void rebuild();
    void runCommand(const QString &command, const QString &toast, const QString &openUrlField);
    void appendLog(const QString &line);

    PluginPageInfo m_info;
    QJsonArray m_blocks;
    QWidget *m_container = nullptr;
    QVBoxLayout *m_layout = nullptr;
    LogView *m_log = nullptr;
    QVector<QString> m_pendingLog;
};

} // namespace mcsm
