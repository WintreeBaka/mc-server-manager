#include "app/AppContext.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QStandardPaths>
#include <QComboBox>
#include <QAbstractItemView>

#include "app/Easing.h"
#include "plugin/PluginHost.h"

namespace mcsm {
namespace {

QString settingsPath()
{
    // GenericConfigLocation + our own folder keeps gui.ini next to the backend
    // data root instead of nesting a second McServerManager directory.
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.mcservermanager");
    base = base + QStringLiteral("/McServerManager");
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("gui.ini"));
}

constexpr int kPollIntervalMs = 5000;

} // namespace

AppContext *AppContext::instance()
{
    // Intentionally leaked: the context outlives QApplication during shutdown,
    // which keeps child QProcess/QSettings teardown deterministic.
    static AppContext *context = new AppContext();
    return context;
}

AppContext::AppContext(QObject *parent)
    : QObject(parent)
{
    m_backend = new BackendClient(this);
    m_servers = new ServerModel(this);
    m_settings = new QSettings(settingsPath(), QSettings::IniFormat, this);
    m_plugins = new PluginHost(this);
    m_plugins->setBackend(m_backend);
    m_plugins->setSettings(m_settings);

    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &AppContext::onPoll);
    connect(m_backend, &BackendClient::availabilityChanged, this, [this](bool available) {
        emit backendAvailabilityChanged(available);
        if (available)
            m_plugins->reload();
    });
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &AppContext::onThemeChanged);

    // plugin frontend scripts receive the same notifications the pages do
    connect(m_plugins, &PluginHost::toastRequested, this, [this](const QString &message, const QString &level) {
        emit notify(message, level.isEmpty() ? QStringLiteral("info") : level);
    });
}

void AppContext::bootstrap()
{
    if (m_bootstrapped)
        return;
    m_bootstrapped = true;
    loadSettings();
    applyTheme();
    m_pollTimer.start();
    refreshServers();
    m_plugins->reload();
    if (m_settings->value(QStringLiteral("scheduler/enabled"), true).toBool())
        startScheduler();
}

void AppContext::loadSettings()
{
    // Setters persist immediately, so without this guard the first setter would
    // write the not-yet-read defaults back over the user's file.
    m_suppressSave = true;
    const QString theme = m_settings->value(QStringLiteral("ui/theme"), QStringLiteral("light")).toString();
    ThemeManager::instance()->setDark(theme.compare(QLatin1String("light")) != 0, false);
    setAnimationsEnabled(m_settings->value(QStringLiteral("ui/animations"), true).toBool());
    setAnimationScale(m_settings->value(QStringLiteral("ui/animationScale"), 1.0).toDouble());
    const QString accent = m_settings->value(QStringLiteral("ui/accentStyle"),
                                             QStringLiteral("gradient")).toString();
    ThemeManager::instance()->setAccentStyle(accent.compare(QLatin1String("flat")) == 0
                                                ? ThemeManager::AccentStyle::Flat
                                                : ThemeManager::AccentStyle::Gradient);
    ThemeManager::instance()->setFontFamily(m_settings->value(QStringLiteral("ui/fontFamily")).toString());
    m_experimental = m_settings->value(QStringLiteral("ui/experimental"), false).toBool();

    const QString storedHome = m_settings->value(QStringLiteral("backend/home")).toString();
    if (!storedHome.isEmpty())
        m_backend->setDataHome(storedHome);

    // A portable copy should always use the backend that sits next to it; the
    // stored path is only a fallback for custom setups.
    const QString sibling = BackendClient::siblingBackend();
    const QString backendPath = m_settings->value(QStringLiteral("backend/executable")).toString();
    if (!sibling.isEmpty()) {
        m_backend->setExecutable(sibling);
    } else if (!backendPath.isEmpty() && QFileInfo::exists(backendPath)) {
        m_backend->setExecutable(backendPath);
    } else {
        // stored path is stale (app was moved): fall back to the auto-detected one
        const QString discovered = BackendClient::locateBackend();
        if (!discovered.isEmpty())
            m_backend->setExecutable(discovered);
    }
    m_suppressSave = false;
    saveSettings();
}

void AppContext::saveSettings()
{
    if (m_suppressSave)
        return;
    m_settings->setValue(QStringLiteral("ui/theme"), ThemeManager::instance()->name());
    m_settings->setValue(QStringLiteral("ui/animations"), animationsEnabled());
    m_settings->setValue(QStringLiteral("ui/animationScale"), animationScale());
    m_settings->setValue(QStringLiteral("ui/accentStyle"), ThemeManager::instance()->accentStyleName());
    m_settings->setValue(QStringLiteral("ui/fontFamily"), ThemeManager::instance()->fontFamily());
    m_settings->setValue(QStringLiteral("ui/experimental"), m_experimental);
    m_settings->setValue(QStringLiteral("backend/executable"), m_backend->executable());
    m_settings->setValue(QStringLiteral("backend/home"), m_backend->dataHome());
    m_settings->sync();
}

void AppContext::applyTheme()
{
    if (auto *application = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        QFont font = application->font();
        font.setFamilies(ThemeManager::instance()->fontFamilies());
        application->setFont(font);
        application->setPalette(ThemeManager::instance()->qtPalette());
        application->setStyleSheet(ThemeManager::instance()->styleSheet());
        applyComboPopupStyle(application);
    }
    emit themeChanged();
}

void AppContext::applyComboPopupStyle(QApplication *application)
{
    // Combo box popups are separate top level windows; on Windows they pick up
    // the *system* theme for their text and selection colours, which turned a
    // light themed app into "black background with dark text" on a dark Windows.
    // Styling the popup view explicitly keeps it in sync with the app theme.
    const Palette &p = ThemeManager::instance()->palette();
    const QString popupStyle =
        QStringLiteral("QAbstractItemView { background: %1; color: %2; border: 1px solid %3;"
                       " border-radius: 10px; padding: 4px; outline: none; }"
                       "QAbstractItemView::item { padding: 6px 10px; border-radius: 8px; min-height: 22px; }"
                       "QAbstractItemView::item:selected { background: %4; color: %2; }"
                       "QAbstractItemView::item:hover { background: %4; color: %2; }")
            .arg(p.surface.name(), p.text.name(), p.border.name(), p.surfaceHover.name());
    const QString containerStyle =
        QStringLiteral("background: %1; border: 1px solid %2; border-radius: 12px;")
            .arg(p.surface.name(), p.border.name());

    const QList<QWidget *> widgets = application->allWidgets();
    for (QWidget *widget : widgets) {
        auto *combo = qobject_cast<QComboBox *>(widget);
        if (!combo)
            continue;
        QAbstractItemView *view = combo->view();
        if (!view)
            continue;
        view->setStyleSheet(popupStyle);
        if (QWidget *container = view->parentWidget())
            container->setStyleSheet(containerStyle);
    }
}

ThemeManager::AccentStyle AppContext::accentStyle() const
{
    return ThemeManager::instance()->accentStyle();
}

void AppContext::setAccentStyle(ThemeManager::AccentStyle style)
{
    ThemeManager::instance()->setAccentStyle(style);
    saveSettings();
}

QString AppContext::fontFamily() const
{
    return ThemeManager::instance()->fontFamily();
}

void AppContext::setFontFamily(const QString &family)
{
    ThemeManager::instance()->setFontFamily(family);
    saveSettings();
}

bool AppContext::experimentalEnabled() const
{
    return m_experimental;
}

void AppContext::setExperimentalEnabled(bool enabled)
{
    if (m_experimental == enabled)
        return;
    m_experimental = enabled;
    saveSettings();
    emit experimentalChanged(enabled);
}

void AppContext::onThemeChanged()
{
    applyTheme();
    saveSettings();
    if (m_plugins)
        m_plugins->dispatchEvent(
            QStringLiteral("theme.changed"),
            QJsonObject {{QStringLiteral("dark"), ThemeManager::instance()->isDark()},
                         {QStringLiteral("style"), ThemeManager::instance()->accentStyleName()}});
}

bool AppContext::animationsEnabled() const
{
    return ThemeManager::instance()->animationsEnabled();
}

void AppContext::setAnimationsEnabled(bool enabled)
{
    ThemeManager::instance()->setAnimationsEnabled(enabled);
    saveSettings();
}

double AppContext::animationScale() const
{
    return ThemeManager::instance()->animationScale();
}

void AppContext::setAnimationScale(double scale)
{
    ThemeManager::instance()->setAnimationScale(scale);
    saveSettings();
}

QString AppContext::dataHome() const
{
    if (!m_backend->dataHome().isEmpty())
        return m_backend->dataHome();
    const Reply reply = m_backend->requestSync({QStringLiteral("version")}, 20000);
    if (reply.ok) {
        const QString home = reply.data.value(QStringLiteral("home")).toString();
        if (!home.isEmpty())
            return home;
    }
    return QString();
}

void AppContext::setDataHome(const QString &home)
{
    m_backend->setDataHome(home);
    saveSettings();
    refreshServers();
}

void AppContext::setSelectedServerId(const QString &id)
{
    if (m_selectedId == id)
        return;
    m_selectedId = id;
    if (m_plugins) {
        m_plugins->setSelectedServerId(id);
        m_plugins->dispatchEvent(QStringLiteral("server.selected"),
                                 QJsonObject {{QStringLiteral("serverId"), id}});
    }
    emit selectionChanged(id);
}

void AppContext::refreshServers(std::function<void(bool, const QString &)> done)
{
    m_backend->request({QStringLiteral("server"), QStringLiteral("list")}, this,
                       [this, done](const Reply &reply) {
                           if (!reply.ok) {
                               emit serversRefreshed(false, reply.errorText());
                               if (done)
                                   done(false, reply.errorText());
                               return;
                           }
                           QVector<ServerInfo> list;
                           const QJsonArray array = reply.data.value(QStringLiteral("servers")).toArray();
                           for (const QJsonValue &value : array)
                               list.append(ServerInfo::fromJson(value.toObject()));
                           m_servers->setServers(list);

                           if (m_selectedId.isEmpty() && !list.isEmpty())
                               setSelectedServerId(list.first().id);
                           else if (!m_selectedId.isEmpty() && m_servers->rowOf(m_selectedId) < 0)
                               setSelectedServerId(list.isEmpty() ? QString() : list.first().id);

                           emit serversRefreshed(true, QString());
                           if (m_plugins)
                               m_plugins->dispatchEvent(
                                   QStringLiteral("servers.refreshed"),
                                   QJsonObject {{QStringLiteral("count"), list.size()}});
                           if (done)
                               done(true, QString());
                       });
}

void AppContext::refreshEnvironment()
{
    if (!m_backend->isConfigured())
        return;
    m_backend->request({QStringLiteral("doctor")}, this, [this](const Reply &reply) {
        if (!reply.ok)
            return;
        m_environment = reply.data;
        emit environmentChanged(m_environment);
        if (m_plugins)
            m_plugins->dispatchEvent(QStringLiteral("environment.changed"), m_environment);
    });
}

void AppContext::selectedServer(std::function<void(const ServerInfo &, const QString &)> done)
{
    const QString id = m_selectedId;
    if (id.isEmpty()) {
        if (done)
            done(ServerInfo {}, QStringLiteral("尚未选择服务器"));
        return;
    }
    m_backend->request({QStringLiteral("server"), QStringLiteral("get"), QStringLiteral("--id"), id}, this,
                       [done](const Reply &reply) {
                           if (!reply.ok) {
                               if (done)
                                   done(ServerInfo {}, reply.errorText());
                               return;
                           }
                           if (done)
                               done(ServerInfo::fromJson(reply.data), QString());
                       });
}

void AppContext::logActivity(const QString &message, const QString &level)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString entry = QStringLiteral("%1  %2").arg(stamp, message);
    m_activity.prepend(qMakePair(entry, level));
    while (m_activity.size() > 60)
        m_activity.removeLast();
    emit activityAdded(entry, level);
    emit notify(message, level);
}

void AppContext::startScheduler()
{
    if (schedulerRunning() || !m_backend->isConfigured())
        return;
    auto *process = new QProcess(this);
    QStringList arguments = m_backend->dataHome().isEmpty()
                                ? QStringList()
                                : QStringList {QStringLiteral("--home"), m_backend->dataHome()};
    arguments << QStringLiteral("daemon") << QStringLiteral("--interval") << QStringLiteral("60");
    process->setProgram(m_backend->executable());
    process->setArguments(arguments);
    // The daemon writes one JSON line per tick; forwarding avoids ever blocking
    // it on an unread pipe.
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    process->start();
    if (!process->waitForStarted(8000)) {
        logActivity(QStringLiteral("定时备份守护进程启动失败"), QStringLiteral("error"));
        delete process;
        return;
    }
    m_daemon = process;
}

void AppContext::stopScheduler()
{
    if (!m_daemon)
        return;
    m_daemon->kill();
    m_daemon->waitForFinished(3000);
    m_daemon.clear();
}

bool AppContext::schedulerRunning() const
{
    return m_daemon && m_daemon->state() == QProcess::Running;
}

void AppContext::onPoll()
{
    if (!m_backend->isConfigured())
        return;
    // the environment (Docker, JDK images) is re-checked every ~20 seconds so a
    // Docker that was started after the app keeps showing correctly
    if (++m_pollTicks % 4 == 1)
        refreshEnvironment();
    m_backend->request({QStringLiteral("server"), QStringLiteral("list")}, this, [this](const Reply &reply) {
        if (!reply.ok)
            return;
        const QJsonArray array = reply.data.value(QStringLiteral("servers")).toArray();
        QVector<ServerInfo> list;
        for (const QJsonValue &value : array)
            list.append(ServerInfo::fromJson(value.toObject()));
        m_servers->setServers(list);
    });
}

} // namespace mcsm
