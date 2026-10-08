#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJSValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QJSEngine;
class QSettings;

namespace mcsm {

class BackendClient;
struct Reply;
class PluginHost;

/// One page contributed by a plugin's frontend script.
struct PluginPageInfo
{
    QString id;
    QString pluginId;
    QString pluginName;
    QString title;
    QString icon = QStringLiteral("✦");
    QString subtitle;
};

/// The `mcsm` object a plugin script sees. Every method is a controlled entry
/// point into the manager - plugins never touch Qt directly.
class PluginApi : public QObject
{
    Q_OBJECT

public:
    PluginApi(PluginHost *host, QJSEngine *engine, const QJsonObject &plugin);

    Q_INVOKABLE void log(const QString &message) const;
    Q_INVOKABLE void toast(const QString &message, const QString &level = QStringLiteral("info")) const;

    Q_INVOKABLE QString registerPage(const QJSValue &descriptor);
    Q_INVOKABLE void setPageContent(const QString &pageId, const QJSValue &blocks) const;
    Q_INVOKABLE void openPage(const QString &pageId) const;

    Q_INVOKABLE QJSValue storageGet(const QString &key) const;
    Q_INVOKABLE void storageSet(const QString &key, const QJSValue &value) const;
    Q_INVOKABLE void storageRemove(const QString &key) const;
    Q_INVOKABLE QJSValue storageKeys() const;

    Q_INVOKABLE QJSValue settingsGet(const QString &key) const;
    Q_INVOKABLE void settingsSet(const QString &key, const QJSValue &value) const;

    Q_INVOKABLE QJSValue serversList() const;
    Q_INVOKABLE QString selectedServer() const;
    Q_INVOKABLE void refreshServers() const;

    Q_INVOKABLE void invoke(const QJSValue &command, const QJSValue &callback) const;
    Q_INVOKABLE void on(const QString &event, const QJSValue &handler);
    Q_INVOKABLE QJSValue include(const QString &relativePath);

    Q_INVOKABLE void openUrl(const QString &url) const;
    Q_INVOKABLE QString humanBytes(double bytes) const;
    Q_INVOKABLE QString managerVersion() const;
    Q_INVOKABLE QString apiVersion() const;

    QJSEngine *engine() const { return m_engine; }
    QString pluginId() const;
    QJsonObject plugin() const { return m_plugin; }

    /// Calls every handler registered for `event` with `payload`.
    void dispatch(const QString &event, const QJsonObject &payload);

private:
    PluginHost *m_host = nullptr;
    QJSEngine *m_engine = nullptr;
    QJsonObject m_plugin;
    QHash<QString, QVector<QJSValue>> m_handlers;
};

/// Loads plugin frontend scripts, exposes the API and keeps the contributed
/// pages / events in sync with the backend.
class PluginHost : public QObject
{
    Q_OBJECT

public:
    explicit PluginHost(QObject *parent = nullptr);

    void setBackend(BackendClient *backend) { m_backend = backend; }
    void setSettings(QSettings *settings) { m_settings = settings; }

    /// Re-reads `plugin packages` and reloads every enabled frontend plugin.
    void reload();

    QVector<PluginPageInfo> pages() const { return m_pages; }
    PluginPageInfo pageInfo(const QString &pageId) const;
    QJsonArray pageContent(const QString &pageId) const;
    QStringList errors() const { return m_errors; }
    QStringList loaded() const { return m_loaded; }
    QJsonArray packages() const { return m_packages; }
    QString pluginsDir() const { return m_pluginsDir; }
    bool busy() const { return m_busy; }
    /// Scope label of the plugin that contributed a page.
    QString pagePluginId(const QString &pageId) const;

    void dispatchEvent(const QString &event, const QJsonObject &payload = QJsonObject());

    // --- called by PluginApi -------------------------------------------------
    void registerPage(const QJsonObject &descriptor, const QString &pluginId,
                      const QJsonObject &plugin);
    void setPageContent(const QString &pageId, const QJsonArray &blocks);
    void pushToast(const QString &message, const QString &level);
    void reportLog(const QString &pluginId, const QString &message);
    void invokeBackend(const QStringList &arguments, const QJSValue &callback,
                       QJSEngine *engine = nullptr);
    QVariant storageValue(const QString &pluginId, const QString &key) const;
    void setStorageValue(const QString &pluginId, const QString &key, const QVariant &value);
    void removeStorageValue(const QString &pluginId, const QString &key);
    QStringList storageKeys(const QString &pluginId) const;
    QVariant settingsValue(const QString &key) const;
    void setSettingsValue(const QString &key, const QVariant &value);
    QJsonArray serverListJson() const;
    QString selectedServerId() const;
    void setSelectedServerId(const QString &id) { m_selectedId = id; }
    void requestPageOpen(const QString &pageId) { emit pageOpenRequested(pageId); }

signals:
    /// Every contributed page is gone: the window should drop plugin pages.
    void pagesReset();
    void pageRegistered(const QString &pageId);
    void pageContentChanged(const QString &pageId);
    void pageOpenRequested(const QString &pageId);
    void toastRequested(const QString &message, const QString &level);
    void pluginMessage(const QString &pluginId, const QString &message);
    void pluginsChanged();
    void loadFinished();

private:
    void loadFrontendPlugins();
    void teardown();

    BackendClient *m_backend = nullptr;
    QSettings *m_settings = nullptr;

    QJsonArray m_packages;
    QString m_pluginsDir;
    QVector<PluginPageInfo> m_pages;
    QHash<QString, QJsonArray> m_pageContent;
    QVector<QJSEngine *> m_engines;
    QVector<PluginApi *> m_apis;
    QStringList m_loaded;
    QStringList m_errors;
    QString m_selectedId;
    bool m_busy = false;
    int m_generatedPageCounter = 0;
};

} // namespace mcsm
