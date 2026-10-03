#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QTimer>

#include <functional>

#include "app/BackendClient.h"
#include "app/ThemeManager.h"
#include "model/ServerModel.h"

namespace mcsm {

/// Application wide services: backend bridge, server list, theme, settings and
/// the background poller that keeps container states fresh.
class AppContext : public QObject
{
    Q_OBJECT

public:
    static AppContext *instance();

    BackendClient *backend() const { return m_backend; }
    ThemeManager *theme() const { return ThemeManager::instance(); }
    ServerModel *servers() const { return m_servers; }
    QSettings *settings() const { return m_settings; }

    void bootstrap();

    QString selectedServerId() const { return m_selectedId; }
    void setSelectedServerId(const QString &id);

    bool animationsEnabled() const;
    void setAnimationsEnabled(bool enabled);
    double animationScale() const;
    void setAnimationScale(double scale);

    ThemeManager::AccentStyle accentStyle() const;
    void setAccentStyle(ThemeManager::AccentStyle style);
    QString fontFamily() const;
    void setFontFamily(const QString &family);

    /// Experimental features (currently: running a server on a JDK cloned from
    /// this machine). Enabled from the settings menu.
    bool experimentalEnabled() const;
    void setExperimentalEnabled(bool enabled);

    QString dataHome() const;
    void setDataHome(const QString &home);

    /// Refreshes the server list from the backend.
    void refreshServers(std::function<void(bool ok, const QString &message)> done = {});

    void selectedServer(std::function<void(const ServerInfo &, const QString &error)> done);

    void logActivity(const QString &message, const QString &level = QStringLiteral("info"));
    QVector<QPair<QString, QString>> activity() const { return m_activity; }

    /// Re-runs `mcsm-cli doctor` and broadcasts the result. Called periodically
    /// and by every "refresh" action, so the title bar follows Docker being
    /// started or stopped while the app is already running.
    void refreshEnvironment();
    QJsonObject environment() const { return m_environment; }

    /// Starts `mcsm-cli daemon` so scheduled backups keep running while the app
    /// is open. Safe to call repeatedly.
    void startScheduler();
    void stopScheduler();
    bool schedulerRunning() const;

signals:
    void serversRefreshed(bool ok, const QString &message);
    void selectionChanged(const QString &serverId);
    void activityAdded(const QString &message, const QString &level);
    void notify(const QString &message, const QString &level);
    void themeChanged();
    void experimentalChanged(bool enabled);
    void environmentChanged(const QJsonObject &doctor);
    void backendAvailabilityChanged(bool available);

private slots:
    void onPoll();
    void onThemeChanged();

private:
    explicit AppContext(QObject *parent = nullptr);
    void applyTheme();
    static void applyComboPopupStyle(QApplication *application);
    void loadSettings();
    void saveSettings();

    BackendClient *m_backend = nullptr;
    ServerModel *m_servers = nullptr;
    QSettings *m_settings = nullptr;
    QTimer m_pollTimer;
    QPointer<QProcess> m_daemon;
    QString m_selectedId;
    QVector<QPair<QString, QString>> m_activity;
    QJsonObject m_environment;
    int m_pollTicks = 0;
    bool m_experimental = false;
    bool m_bootstrapped = false;
    bool m_suppressSave = false;
};

} // namespace mcsm
