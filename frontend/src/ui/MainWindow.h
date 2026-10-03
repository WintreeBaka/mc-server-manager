#pragma once

#include <QMainWindow>

class QLabel;

namespace mcsm {

class AnimatedStack;
class BackupPage;
class ConfigPage;
class ConsolePage;
class DashboardPage;
class PluginPage;
class ServersPage;
class SettingsPage;
class Sidebar;
class TitleBar;
class ToastHost;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    /// Deep link used by `McServerManager --page servers` and by shortcuts.
    void showPage(const QString &name);

protected:
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void navigateTo(int index);
    void openCreateDialog();
    void updateStatusStrip();
    void refreshEnvironment();

    TitleBar *m_titleBar = nullptr;
    Sidebar *m_sidebar = nullptr;
    AnimatedStack *m_stack = nullptr;
    ToastHost *m_toasts = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_selectionLabel = nullptr;

    DashboardPage *m_dashboard = nullptr;
    ServersPage *m_servers = nullptr;
    ConsolePage *m_console = nullptr;
    ConfigPage *m_config = nullptr;
    BackupPage *m_backup = nullptr;
    PluginPage *m_plugin = nullptr;
    SettingsPage *m_settings = nullptr;
};

} // namespace mcsm
