#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace mcsm {

class GradientButton;

/// "设置 → 插件扩展" section: lists every installed plugin package, exposes the
/// 添加插件 entry point (zip / URL import) and lets the user enable, disable,
/// uninstall plugins or open a plugin provided web panel.
class PluginExtensionsPage : public QWidget
{
    Q_OBJECT

public:
    explicit PluginExtensionsPage(QWidget *parent = nullptr);

    void refresh();

private:
    QWidget *buildToolbarCard();
    QWidget *buildListCard();
    QWidget *buildRow(const QJsonObject &plugin);

    void openImport();
    void setEnabled(const QString &pluginId, bool enabled);
    void removePlugin(const QString &pluginId, const QString &name);
    void openFolder(const QString &path);
    void openPluginPage(const QString &pluginId);
    void openPanel(const QJsonObject &plugin);
    void openDocs();
    void setStatus(const QString &text);

    QVBoxLayout *m_listLayout = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_empty = nullptr;
    GradientButton *m_refresh = nullptr;
};

} // namespace mcsm
