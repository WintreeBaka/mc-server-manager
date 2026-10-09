#include "plugin/PluginHost.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "app/BackendClient.h"

namespace mcsm {
namespace {

/// Time budgets for plugin scripts. Loading does more work (registering pages,
/// reading storage), event handlers must stay well under one frame budget.
constexpr int kScriptLoadBudgetMs = 2500;
constexpr int kScriptEventBudgetMs = 1000;

/// Watches one synchronous script evaluation.
///
/// A plugin with `while (true) {}` would otherwise freeze the whole application,
/// because evaluate() never returns to the event loop (a QTimer in this thread
/// could not fire). The deadline therefore lives in a helper thread that only
/// touches QJSEngine::setInterrupted(). This is best effort: V4 checks the flag
/// in loops, so a script that never yields is stopped within a few ms.
class ScriptWatchdog
{
public:
    ScriptWatchdog(QJSEngine *engine, int budgetMs)
        : engine_(engine)
    {
        worker_ = std::thread([this, budgetMs]() {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!finished_.wait_for(lock, std::chrono::milliseconds(budgetMs),
                                    [this]() { return done_; })) {
                fired_ = true;
                engine_->setInterrupted(true);
            }
        });
    }

    ~ScriptWatchdog()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        finished_.notify_all();
        if (worker_.joinable())
            worker_.join();
        engine_->setInterrupted(false);
    }

    bool fired()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return fired_;
    }

private:
    QJSEngine *engine_ = nullptr;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable finished_;
    bool done_ = false;
    bool fired_ = false;
};

/// Local copy of the backend helper: the GUI must not depend on backend headers.
QString humanBytes(qint64 bytes)
{
    static const QStringList units = {QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"),
                                      QStringLiteral("GB"), QStringLiteral("TB")};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0 ? QStringLiteral("%1 %2").arg(bytes).arg(units.at(unit))
                     : QStringLiteral("%1 %2").arg(QString::number(value, 'f', 1), units.at(unit));
}

/// Splits a command line such as `server get --id demo` into argv, honouring
/// double quotes so plugins can pass values with spaces.
QStringList tokenize(const QString &command)
{
    QStringList tokens;
    QString current;
    bool inQuotes = false;
    for (const QChar &character : command) {
        if (character == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            continue;
        }
        if (character.isSpace() && !inQuotes) {
            if (!current.isEmpty()) {
                tokens << current;
                current.clear();
            }
            continue;
        }
        current.append(character);
    }
    if (!current.isEmpty())
        tokens << current;
    return tokens;
}

QStringList argumentsFrom(const QJSValue &command)
{
    if (command.isArray()) {
        QStringList arguments;
        const quint32 length = command.property(QStringLiteral("length")).toUInt();
        for (quint32 i = 0; i < length; ++i)
            arguments << command.property(i).toString();
        return arguments;
    }
    if (command.isString())
        return tokenize(command.toString());
    return QStringList();
}

QJSValue toJs(QJSEngine *engine, const QJsonValue &value)
{
    return engine->toScriptValue(value.toVariant());
}

} // namespace

// -------------------------------------------------------------- PluginApi ----

PluginApi::PluginApi(PluginHost *host, QJSEngine *engine, const QJsonObject &plugin)
    : QObject(engine)
    , m_host(host)
    , m_engine(engine)
    , m_plugin(plugin)
{
}

QString PluginApi::pluginId() const
{
    return m_plugin.value(QStringLiteral("id")).toString();
}

void PluginApi::log(const QString &message) const
{
    m_host->reportLog(pluginId(), message);
}

void PluginApi::toast(const QString &message, const QString &level) const
{
    m_host->pushToast(message, level);
}

QString PluginApi::registerPage(const QJSValue &descriptor)
{
    QJsonObject object = QJsonObject::fromVariantMap(descriptor.toVariant().toMap());
    if (!descriptor.isObject() || object.isEmpty()) {
        log(QStringLiteral("registerPage 需要传入 {id,title,icon,subtitle} 对象"));
        return QString();
    }
    m_host->registerPage(object, pluginId(), m_plugin);
    QString pageId = object.value(QStringLiteral("id")).toString();
    if (pageId.isEmpty())
        pageId = object.value(QStringLiteral("title")).toString();
    return pageId;
}

void PluginApi::setPageContent(const QString &pageId, const QJSValue &blocks) const
{
    QJsonArray array = QJsonArray::fromVariantList(blocks.toVariant().toList());
    m_host->setPageContent(pageId, array);
}

void PluginApi::openPage(const QString &pageId) const
{
    m_host->requestPageOpen(pageId);
}

QJSValue PluginApi::storageGet(const QString &key) const
{
    return m_engine->toScriptValue(m_host->storageValue(pluginId(), key));
}

void PluginApi::storageSet(const QString &key, const QJSValue &value) const
{
    m_host->setStorageValue(pluginId(), key, value.toVariant());
}

void PluginApi::storageRemove(const QString &key) const
{
    m_host->removeStorageValue(pluginId(), key);
}

QJSValue PluginApi::storageKeys() const
{
    return m_engine->toScriptValue(m_host->storageKeys(pluginId()));
}

QJSValue PluginApi::settingsGet(const QString &key) const
{
    return m_engine->toScriptValue(m_host->settingsValue(key));
}

void PluginApi::settingsSet(const QString &key, const QJSValue &value) const
{
    m_host->setSettingsValue(key, value.toVariant());
}

QJSValue PluginApi::serversList() const
{
    return m_engine->toScriptValue(m_host->serverListJson().toVariantList());
}

QString PluginApi::selectedServer() const
{
    return m_host->selectedServerId();
}

void PluginApi::refreshServers() const
{
    m_host->invokeBackend({QStringLiteral("server"), QStringLiteral("list")}, QJSValue(), m_engine);
}

void PluginApi::invoke(const QJSValue &command, const QJSValue &callback) const
{
    const QStringList arguments = argumentsFrom(command);
    if (arguments.isEmpty()) {
        log(QStringLiteral("invoke 需要命令字符串或参数数组"));
        return;
    }
    m_host->invokeBackend(arguments, callback, m_engine);
}

void PluginApi::on(const QString &event, const QJSValue &handler)
{
    if (!handler.isCallable())
        return;
    m_handlers[event.trimmed().toLower()].append(handler);
}

QJSValue PluginApi::include(const QString &relativePath)
{
    const QString base = m_plugin.value(QStringLiteral("path")).toString();
    // keep includes inside the plugin directory: no absolute paths, no ".."
    QString normalized = relativePath;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (normalized.startsWith(QLatin1Char('/')) || normalized.contains(QLatin1String("../"))
        || normalized.contains(QLatin1Char(':'))) {
        log(QStringLiteral("include 只允许插件目录内的相对路径：%1").arg(relativePath));
        return QJSValue(QJSValue::UndefinedValue);
    }
    const QString path = QDir(base).filePath(relativePath);
    const QString canonicalBase = QFileInfo(base).canonicalFilePath();
    const QString canonicalPath = QFileInfo(path).canonicalFilePath();
    if (canonicalBase.isEmpty() || canonicalPath.isEmpty()
        || !canonicalPath.startsWith(canonicalBase + QLatin1Char('/'), Qt::CaseInsensitive)) {
        log(QStringLiteral("include 路径越界：%1").arg(relativePath));
        return QJSValue(QJSValue::UndefinedValue);
    }
    if (!QFileInfo::exists(path)) {
        log(QStringLiteral("include 找不到文件：%1").arg(relativePath));
        return QJSValue(QJSValue::UndefinedValue);
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        log(QStringLiteral("include 无法读取：%1").arg(relativePath));
        return QJSValue(QJSValue::UndefinedValue);
    }
    const QString source = QString::fromUtf8(file.readAll());
    file.close();
    const QJSValue value = m_engine->evaluate(source, path);
    if (value.isError())
        log(QStringLiteral("include 执行出错：%1").arg(value.toString()));
    return value;
}

void PluginApi::openUrl(const QString &url) const
{
    QDesktopServices::openUrl(QUrl(url));
}

QString PluginApi::humanBytes(double bytes) const
{
    return mcsm::humanBytes(qint64(bytes));
}

QString PluginApi::managerVersion() const
{
    return QCoreApplication::applicationVersion();
}

QString PluginApi::apiVersion() const
{
    return QStringLiteral("1");
}

void PluginApi::dispatch(const QString &event, const QJsonObject &payload)
{
    const auto it = m_handlers.constFind(event.trimmed().toLower());
    if (it == m_handlers.constEnd())
        return;
    const QJSValue argument = m_engine->toScriptValue(payload.toVariantMap());
    for (const QJSValue &handler : it.value()) {
        // Each handler runs behind a watchdog and a misbehaving plugin is taken
        // out of the loop instead of stalling every later event.
        if (!m_host->dispatchOne(pluginId(), m_engine, handler, argument))
            break;
    }
}

// ------------------------------------------------------------- PluginHost ----

PluginHost::PluginHost(QObject *parent)
    : QObject(parent)
{
}

void PluginHost::teardown()
{
    m_apis.clear();
    const QVector<QJSEngine *> engines = m_engines;
    m_engines.clear();
    for (QJSEngine *engine : engines)
        engine->deleteLater();
    m_loaded.clear();
}

void PluginHost::reload()
{
    if (!m_backend || !m_backend->isConfigured()) {
        m_errors = {QStringLiteral("未找到 mcsm-cli，无法加载插件")};
        emit pluginsChanged();
        return;
    }
    m_busy = true;
    m_backend->request({QStringLiteral("plugin"), QStringLiteral("packages")}, this,
                       [this](const Reply &reply) {
                           m_busy = false;
                           if (!reply.ok) {
                               m_errors = {QStringLiteral("读取插件列表失败：%1").arg(reply.errorText())};
                               emit pluginsChanged();
                               return;
                           }
                           m_packages = reply.data.value(QStringLiteral("plugins")).toArray();
                           m_pluginsDir = reply.data.value(QStringLiteral("pluginsDir")).toString();
                           loadFrontendPlugins();
                           emit pluginsChanged();
                           emit loadFinished();
                       });
}

void PluginHost::loadFrontendPlugins()
{
    m_pages.clear();
    m_pageContent.clear();
    m_errors.clear();
    m_quarantined.clear();
    teardown();
    emit pagesReset();

    for (const QJsonValue &value : m_packages) {
        const QJsonObject plugin = value.toObject();
        if (!plugin.value(QStringLiteral("enabled")).toBool(true))
            continue;
        if (!plugin.value(QStringLiteral("hasFrontend")).toBool())
            continue;

        const QString pluginId = plugin.value(QStringLiteral("id")).toString();
        const QString entry = plugin.value(QStringLiteral("frontendEntry")).toString();
        const QString path = QDir(plugin.value(QStringLiteral("path")).toString()).filePath(entry);
        if (entry.isEmpty() || !QFileInfo::exists(path)) {
            m_errors << QStringLiteral("%1：找不到前端入口 %2").arg(pluginId, entry);
            continue;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            m_errors << QStringLiteral("%1：无法读取 %2").arg(pluginId, path);
            continue;
        }
        const QString source = QString::fromUtf8(file.readAll());
        file.close();

        auto *engine = new QJSEngine(this);
        engine->installExtensions(QJSEngine::ConsoleExtension);
        auto *api = new PluginApi(this, engine, plugin);
        m_engines.append(engine);
        m_apis.append(api);

        // Build the `mcsm` namespace: plain JS object so plugins can read
        // mcsm.plugin.id and add their own helpers without clobbering Qt state.
        QJSValue global = engine->globalObject();
        QJSValue mcsm = engine->newObject();
        QJSValue wrapped = engine->newQObject(api);
        mcsm.setProperty(QStringLiteral("plugin"), engine->toScriptValue(
                                                    QVariantMap {
                                                        {QStringLiteral("id"), pluginId},
                                                        {QStringLiteral("name"), plugin.value(QStringLiteral("name")).toString()},
                                                        {QStringLiteral("version"), plugin.value(QStringLiteral("version")).toString()},
                                                        {QStringLiteral("scope"), plugin.value(QStringLiteral("scope")).toString()},
                                                        {QStringLiteral("path"), plugin.value(QStringLiteral("path")).toString()},
                                                    }));
        mcsm.setProperty(QStringLiteral("version"), QCoreApplication::applicationVersion());
        mcsm.setProperty(QStringLiteral("apiVersion"), QStringLiteral("1"));
        mcsm.setProperty(QStringLiteral("log"), wrapped.property(QStringLiteral("log")));
        mcsm.setProperty(QStringLiteral("toast"), wrapped.property(QStringLiteral("toast")));
        mcsm.setProperty(QStringLiteral("openUrl"), wrapped.property(QStringLiteral("openUrl")));
        mcsm.setProperty(QStringLiteral("humanBytes"), wrapped.property(QStringLiteral("humanBytes")));
        mcsm.setProperty(QStringLiteral("include"), wrapped.property(QStringLiteral("include")));
        mcsm.setProperty(QStringLiteral("refreshServers"), wrapped.property(QStringLiteral("refreshServers")));

        QJSValue ui = engine->newObject();
        ui.setProperty(QStringLiteral("registerPage"), wrapped.property(QStringLiteral("registerPage")));
        ui.setProperty(QStringLiteral("setPageContent"), wrapped.property(QStringLiteral("setPageContent")));
        ui.setProperty(QStringLiteral("openPage"), wrapped.property(QStringLiteral("openPage")));
        mcsm.setProperty(QStringLiteral("ui"), ui);

        QJSValue storage = engine->newObject();
        storage.setProperty(QStringLiteral("get"), wrapped.property(QStringLiteral("storageGet")));
        storage.setProperty(QStringLiteral("set"), wrapped.property(QStringLiteral("storageSet")));
        storage.setProperty(QStringLiteral("remove"), wrapped.property(QStringLiteral("storageRemove")));
        storage.setProperty(QStringLiteral("keys"), wrapped.property(QStringLiteral("storageKeys")));
        mcsm.setProperty(QStringLiteral("storage"), storage);

        QJSValue settings = engine->newObject();
        settings.setProperty(QStringLiteral("get"), wrapped.property(QStringLiteral("settingsGet")));
        settings.setProperty(QStringLiteral("set"), wrapped.property(QStringLiteral("settingsSet")));
        mcsm.setProperty(QStringLiteral("settings"), settings);

        QJSValue servers = engine->newObject();
        servers.setProperty(QStringLiteral("list"), wrapped.property(QStringLiteral("serversList")));
        servers.setProperty(QStringLiteral("selected"), wrapped.property(QStringLiteral("selectedServer")));
        mcsm.setProperty(QStringLiteral("servers"), servers);

        QJSValue backend = engine->newObject();
        backend.setProperty(QStringLiteral("invoke"), wrapped.property(QStringLiteral("invoke")));
        mcsm.setProperty(QStringLiteral("backend"), backend);

        QJSValue events = engine->newObject();
        events.setProperty(QStringLiteral("on"), wrapped.property(QStringLiteral("on")));
        mcsm.setProperty(QStringLiteral("events"), events);

        QJSValue utils = engine->newObject();
        utils.setProperty(QStringLiteral("humanBytes"), wrapped.property(QStringLiteral("humanBytes")));
        mcsm.setProperty(QStringLiteral("utils"), utils);

        global.setProperty(QStringLiteral("mcsm"), mcsm);

        QString scriptError;
        if (!runScript(engine, source, path, pluginId, kScriptLoadBudgetMs, &scriptError)) {
            m_errors << QStringLiteral("%1：%2").arg(pluginId, scriptError);
            reportLog(pluginId, QStringLiteral("脚本执行失败：%1").arg(scriptError));
            continue;
        }
        m_loaded << pluginId;
        reportLog(pluginId, QStringLiteral("前端插件已加载"));
    }

    dispatchEvent(QStringLiteral("app.ready"),
                  QJsonObject {{QStringLiteral("pluginCount"), m_loaded.size()}});
}

bool PluginHost::runScript(QJSEngine *engine, const QString &source, const QString &fileName,
                           const QString &pluginId, int budgetMs, QString *error)
{
    if (!engine)
        return false;
    QElapsedTimer timer;
    timer.start();
    QJSValue value;
    {
        ScriptWatchdog watchdog(engine, budgetMs);
        value = engine->evaluate(source, fileName);
        if (watchdog.fired()) {
            if (error)
                *error = QStringLiteral("脚本超过 %1ms 预算，已被强制中断").arg(budgetMs);
            return false;
        }
    }
    noteCallCost(pluginId, timer.elapsed(), budgetMs);
    if (value.isError()) {
        if (error)
            *error = value.toString();
        return false;
    }
    return true;
}

void PluginHost::noteCallCost(const QString &pluginId, qint64 elapsedMs, int budgetMs)
{
    if (elapsedMs <= budgetMs || pluginId.isEmpty())
        return;
    if (m_quarantined.contains(pluginId))
        return;
    m_quarantined << pluginId;
    m_errors << QStringLiteral("%1：单次执行耗时 %2ms（预算 %3ms），已在本会话中停用该插件")
                    .arg(pluginId)
                    .arg(elapsedMs)
                    .arg(budgetMs);
    reportLog(pluginId, QStringLiteral("执行过慢，已临时停用（可在插件扩展页重新启用）"));
}

bool PluginHost::dispatchOne(const QString &pluginId, QJSEngine *engine, const QJSValue &handler,
                             const QJSValue &argument)
{
    if (!engine || !handler.isCallable() || pluginIsQuarantined(pluginId))
        return false;
    QElapsedTimer timer;
    timer.start();
    QJSValue result;
    {
        ScriptWatchdog watchdog(engine, kScriptEventBudgetMs);
        result = handler.call({argument});
        if (watchdog.fired()) {
            noteCallCost(pluginId, kScriptEventBudgetMs + 1, kScriptEventBudgetMs);
            return false;
        }
    }
    noteCallCost(pluginId, timer.elapsed(), kScriptEventBudgetMs);
    if (result.isError()) {
        reportLog(pluginId, QStringLiteral("事件处理失败：%1").arg(result.toString()));
        return false;
    }
    return true;
}

PluginPageInfo PluginHost::pageInfo(const QString &pageId) const
{
    for (const PluginPageInfo &page : m_pages)
        if (page.id == pageId)
            return page;
    return PluginPageInfo {};
}

QString PluginHost::pagePluginId(const QString &pageId) const
{
    return pageInfo(pageId).pluginId;
}

QJsonArray PluginHost::pageContent(const QString &pageId) const
{
    return m_pageContent.value(pageId);
}

void PluginHost::registerPage(const QJsonObject &descriptor, const QString &pluginId,
                              const QJsonObject &plugin)
{
    PluginPageInfo page;
    page.pluginId = pluginId;
    page.pluginName = plugin.value(QStringLiteral("name")).toString(pluginId);
    page.title = descriptor.value(QStringLiteral("title")).toString(
        plugin.value(QStringLiteral("name")).toString(pluginId));
    page.icon = descriptor.value(QStringLiteral("icon")).toString(QStringLiteral("✦"));
    page.subtitle = descriptor.value(QStringLiteral("subtitle")).toString();
    page.id = descriptor.value(QStringLiteral("id")).toString();
    if (page.id.isEmpty())
        page.id = QStringLiteral("%1-page-%2").arg(pluginId).arg(++m_generatedPageCounter);
    if (pageInfo(page.id).id == page.id)
        return;
    m_pages.append(page);
    emit pageRegistered(page.id);
}

void PluginHost::setPageContent(const QString &pageId, const QJsonArray &blocks)
{
    if (pageId.isEmpty())
        return;
    m_pageContent.insert(pageId, blocks);
    emit pageContentChanged(pageId);
}

void PluginHost::pushToast(const QString &message, const QString &level)
{
    emit toastRequested(message, level.isEmpty() ? QStringLiteral("info") : level);
}

void PluginHost::reportLog(const QString &pluginId, const QString &message)
{
    emit pluginMessage(pluginId, message);
}

void PluginHost::invokeBackend(const QStringList &arguments, const QJSValue &callback,
                               QJSEngine *engine)
{
    if (!m_backend || arguments.isEmpty())
        return;
    m_backend->request(arguments, this, [callback, engine](const Reply &reply) {
        if (!callback.isCallable())
            return;
        if (!engine)
            return;
        QJSValue argument = engine->newObject();
        argument.setProperty(QStringLiteral("ok"), reply.ok);
        argument.setProperty(QStringLiteral("data"), engine->toScriptValue(reply.data.toVariantMap()));
        argument.setProperty(QStringLiteral("warnings"), engine->toScriptValue(reply.warnings));
        argument.setProperty(QStringLiteral("error"), reply.ok ? QString() : reply.errorText());
        argument.setProperty(QStringLiteral("code"), reply.code);
        callback.call({argument});
    });
}

QVariant PluginHost::storageValue(const QString &pluginId, const QString &key) const
{
    if (!m_settings || pluginId.isEmpty())
        return QVariant();
    return m_settings->value(QStringLiteral("pluginData/%1/%2").arg(pluginId, key));
}

void PluginHost::setStorageValue(const QString &pluginId, const QString &key, const QVariant &value)
{
    if (!m_settings || pluginId.isEmpty())
        return;
    m_settings->setValue(QStringLiteral("pluginData/%1/%2").arg(pluginId, key), value);
    m_settings->sync();
}

void PluginHost::removeStorageValue(const QString &pluginId, const QString &key)
{
    if (!m_settings || pluginId.isEmpty())
        return;
    m_settings->remove(QStringLiteral("pluginData/%1/%2").arg(pluginId, key));
    m_settings->sync();
}

QStringList PluginHost::storageKeys(const QString &pluginId) const
{
    QStringList keys;
    if (!m_settings || pluginId.isEmpty())
        return keys;
    m_settings->beginGroup(QStringLiteral("pluginData/%1").arg(pluginId));
    keys = m_settings->childKeys();
    m_settings->endGroup();
    return keys;
}

QVariant PluginHost::settingsValue(const QString &key) const
{
    if (!m_settings)
        return QVariant();
    return m_settings->value(key);
}

void PluginHost::setSettingsValue(const QString &key, const QVariant &value)
{
    if (!m_settings)
        return;
    m_settings->setValue(key, value);
    m_settings->sync();
}

QJsonArray PluginHost::serverListJson() const
{
    // Cached snapshot only: spawning mcsm-cli here would block the UI thread for
    // as long as the command takes (this used to freeze the window whenever a
    // plugin called mcsm.servers.list()).
    if (m_serverListProvider)
        return m_serverListProvider();
    return QJsonArray();
}

QString PluginHost::selectedServerId() const
{
    return m_selectedId;
}

void PluginHost::dispatchEvent(const QString &event, const QJsonObject &payload)
{
    for (PluginApi *api : m_apis)
        api->dispatch(event, payload);
}

} // namespace mcsm
