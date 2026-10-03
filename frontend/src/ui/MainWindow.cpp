#include "ui/MainWindow.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QShortcut>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "pages/BackupPage.h"
#include "pages/ConfigPage.h"
#include "pages/ConsolePage.h"
#include "pages/CreateServerDialog.h"
#include "pages/DashboardPage.h"
#include "pages/PluginPage.h"
#include "pages/ServersPage.h"
#include "pages/SettingsPage.h"
#include "ui/FramelessHelper.h"
#include "ui/Sidebar.h"
#include "ui/TitleBar.h"
#include "ui/ToastHost.h"
#include "widgets/AnimatedStack.h"
#include "widgets/Common.h"

namespace mcsm {
namespace {

enum PageIndex { Dashboard = 0, Servers, Console, Config, Backup, Plugin, Settings, PageCount };

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("我的世界服务器管理器"));
    resize(1360, 860);
    setMinimumSize(1080, 700);
    FramelessHelper::attach(this, 6);
    buildUi();

    AppContext *context = AppContext::instance();
    connect(context, &AppContext::notify, this, [this](const QString &message, const QString &level) {
        m_toasts->push(message, level);
    });
    connect(context, &AppContext::backendAvailabilityChanged, this, [this](bool available) {
        m_titleBar->setBackendState(available, available ? QStringLiteral("mcsm-cli 已就绪")
                                                        : QStringLiteral("未找到 mcsm-cli"));
        updateStatusStrip();
    });
    connect(context, &AppContext::selectionChanged, this, [this](const QString &) {
        updateStatusStrip();
    });
    connect(context->servers(), &ServerModel::changed, this, &MainWindow::updateStatusStrip);
    connect(context, &AppContext::environmentChanged, this, [this](const QJsonObject &doctor) {
        const QJsonObject docker = doctor.value(QStringLiteral("docker")).toObject();
        const bool running = docker.value(QStringLiteral("daemonRunning")).toBool();
        m_titleBar->setDockerState(running ? QStringLiteral("running") : QStringLiteral("stopped"),
                                   docker.value(QStringLiteral("error")).toString());
    });
    connect(context, &AppContext::themeChanged, this, [this]() {
        m_titleBar->setThemeIsDark(ThemeManager::instance()->isDark());
        update();
    });

    m_titleBar->setBackendState(context->backend()->isConfigured(), context->backend()->executable());
    m_titleBar->setThemeIsDark(ThemeManager::instance()->isDark());
    updateStatusStrip();
    refreshEnvironment();
    m_dashboard->onActivated();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_titleBar = new TitleBar(this);
    rootLayout->addWidget(m_titleBar);

    auto *content = new QWidget(central);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(12, 10, 16, 8);
    contentLayout->setSpacing(14);

    // main menu stays on the left; the settings page is reached from the gear
    // button in the title bar, so it is not part of this list
    m_sidebar = new Sidebar(content);
    m_sidebar->addItem(QStringLiteral("▤"), QStringLiteral("总览"), QStringLiteral("服务器与环境概览"));
    m_sidebar->addItem(QStringLiteral("▣"), QStringLiteral("服务器"), QStringLiteral("服务器列表与详情"));
    m_sidebar->addItem(QStringLiteral("▶"), QStringLiteral("控制台"), QStringLiteral("实时日志与指令"));
    m_sidebar->addItem(QStringLiteral("✎"), QStringLiteral("配置文件"), QStringLiteral("快捷 / 专家配置"));
    m_sidebar->addItem(QStringLiteral("▥"), QStringLiteral("备份"), QStringLiteral("定时备份与回滚"));
    m_sidebar->addItem(QStringLiteral("✦"), QStringLiteral("插件"), QStringLiteral("插件市场"));

    m_stack = new AnimatedStack(content);
    m_dashboard = new DashboardPage(m_stack);
    m_servers = new ServersPage(m_stack);
    m_console = new ConsolePage(m_stack);
    m_config = new ConfigPage(m_stack);
    m_backup = new BackupPage(m_stack);
    m_plugin = new PluginPage(m_stack);
    m_settings = new SettingsPage(m_stack);
    m_stack->addPage(m_dashboard);
    m_stack->addPage(m_servers);
    m_stack->addPage(m_console);
    m_stack->addPage(m_config);
    m_stack->addPage(m_backup);
    m_stack->addPage(m_plugin);
    m_stack->addPage(m_settings);
    m_stack->setDuration(300);

    contentLayout->addWidget(m_sidebar);
    contentLayout->addWidget(m_stack, 1);
    rootLayout->addWidget(content, 1);

    auto *statusStrip = new QWidget(central);
    statusStrip->setFixedHeight(32);
    auto *statusLayout = new QHBoxLayout(statusStrip);
    statusLayout->setContentsMargins(22, 0, 22, 4);
    statusLayout->setSpacing(16);
    m_statusLabel = makeLabel(QStringLiteral("就绪"), QStringLiteral("hint"), statusStrip);
    m_selectionLabel = makeLabel(QStringLiteral("未选择服务器"), QStringLiteral("hint"), statusStrip);
    statusLayout->addWidget(m_statusLabel, 1);
    statusLayout->addWidget(m_selectionLabel);
    rootLayout->addWidget(statusStrip);

    setCentralWidget(central);

    m_toasts = new ToastHost(central);
    m_toasts->raise();

    connect(m_sidebar, &Sidebar::itemSelected, this, &MainWindow::navigateTo);

    // Ctrl+1 … Ctrl+7 jump between pages (also used by the automated UI test).
    for (int i = 0; i < PageCount; ++i) {
        auto *shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)), this);
        connect(shortcut, &QShortcut::activated, this, [this, i]() { navigateTo(i); });
    }
    connect(m_titleBar, &TitleBar::themeToggleRequested, this, []() {
        ThemeManager::instance()->toggle();
    });
    connect(m_titleBar, &TitleBar::settingsRequested, this, [this]() { navigateTo(Settings); });

    connect(m_dashboard, &DashboardPage::createServerRequested, this, &MainWindow::openCreateDialog);
    connect(m_settings, &SettingsPage::backRequested, this, [this]() { navigateTo(Dashboard); });
    connect(m_servers, &ServersPage::createServerRequested, this, &MainWindow::openCreateDialog);
    connect(m_servers, &ServersPage::openConsoleRequested, this, [this](const QString &id) {
        AppContext::instance()->setSelectedServerId(id);
        navigateTo(Console);
    });
    connect(m_servers, &ServersPage::openConfigRequested, this, [this](const QString &id) {
        AppContext::instance()->setSelectedServerId(id);
        navigateTo(Config);
    });
    connect(m_servers, &ServersPage::openBackupRequested, this, [this](const QString &id) {
        AppContext::instance()->setSelectedServerId(id);
        navigateTo(Backup);
    });
    connect(m_servers, &ServersPage::openPluginRequested, this, [this](const QString &id) {
        AppContext::instance()->setSelectedServerId(id);
        navigateTo(Plugin);
    });

    m_sidebar->setCurrentIndex(Dashboard);
}

void MainWindow::navigateTo(int index)
{
    if (index < 0 || index >= PageCount)
        return;
    // the settings page has no sidebar entry: clear the highlight instead
    m_sidebar->setCurrentIndex(index < m_sidebar->itemCount() ? index : -1);
    // the settings page brings its own left menu, so the main one steps aside
    m_sidebar->setVisible(index != Settings);
    m_stack->setCurrentIndex(index, true, index > m_stack->currentIndex() ? 1 : -1);
    if (auto *page = qobject_cast<PageBase *>(m_stack->page(index)))
        page->onActivated();
    updateStatusStrip();
}

void MainWindow::showPage(const QString &name)
{
    static const QHash<QString, int> pages = {
        {QStringLiteral("dashboard"), Dashboard}, {QStringLiteral("overview"), Dashboard},
        {QStringLiteral("servers"), Servers},     {QStringLiteral("console"), Console},
        {QStringLiteral("logs"), Console},        {QStringLiteral("config"), Config},
        {QStringLiteral("backup"), Backup},       {QStringLiteral("backups"), Backup},
        {QStringLiteral("plugin"), Plugin},       {QStringLiteral("plugins"), Plugin},
        {QStringLiteral("settings"), Settings},
    };
    const int index = pages.value(name.trimmed().toLower(), -1);
    if (index >= 0)
        navigateTo(index);
}

void MainWindow::openCreateDialog()
{
    CreateServerDialog dialog(this);
    connect(&dialog, &CreateServerDialog::serverCreated, this, [this](const QString &id) {
        AppContext::instance()->setSelectedServerId(id);
        AppContext::instance()->refreshServers([this](bool ok, const QString &) {
            if (ok)
                navigateTo(Servers);
        });
    });
    dialog.exec();
}

void MainWindow::updateStatusStrip()
{
    ServerModel *model = AppContext::instance()->servers();
    const int total = model->rowCount();
    const int running = model->runningCount();
    const int errors = model->errorCount();
    m_statusLabel->setText(QStringLiteral("共 %1 台服务器 · %2 台运行中%3")
                               .arg(total)
                               .arg(running)
                               .arg(errors > 0 ? QStringLiteral(" · %1 台需要处理").arg(errors)
                                               : QString()));

    const QString id = AppContext::instance()->selectedServerId();
    if (id.isEmpty()) {
        m_selectionLabel->setText(QStringLiteral("未选择服务器"));
        return;
    }
    const ServerInfo info = model->serverById(id);
    m_selectionLabel->setText(QStringLiteral("当前：%1（%2）").arg(info.name, info.statusText()));
}

void MainWindow::refreshEnvironment()
{
    AppContext::instance()->refreshEnvironment();
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Palette &palette = ThemeManager::instance()->palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), ThemeManager::windowGradient(palette, QRectF(0, 0, width(), height())));

    // Decorative glows only in the light gradient theme: dark mode stays neutral
    // and the flat style stays plain.
    if (ThemeManager::instance()->accentStyle() == ThemeManager::AccentStyle::Flat || palette.dark)
        return;

    QColor softPink = palette.pink;
    softPink.setAlphaF(palette.dark ? 0.10 : 0.16);
    painter.setPen(Qt::NoPen);
    painter.setBrush(softPink);
    painter.drawEllipse(QPointF(width() * 0.16, height() * 0.06), width() * 0.26, height() * 0.24);

    QColor softBlue = palette.blue;
    softBlue.setAlphaF(palette.dark ? 0.09 : 0.14);
    painter.setBrush(softBlue);
    painter.drawEllipse(QPointF(width() * 0.92, height() * 0.94), width() * 0.30, height() * 0.26);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    AppContext::instance()->stopScheduler();
    QMainWindow::closeEvent(event);
}

} // namespace mcsm
