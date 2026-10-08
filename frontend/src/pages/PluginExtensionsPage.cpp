#include "pages/PluginExtensionsPage.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "pages/PluginImportDialog.h"
#include "plugin/PluginHost.h"
#include "widgets/Chip.h"
#include "widgets/Common.h"

namespace mcsm {
namespace {

Chip::Tone toneForScope(const QString &scope)
{
    if (scope == QLatin1String("frontend"))
        return Chip::Pink;
    if (scope == QLatin1String("backend"))
        return Chip::Blue;
    if (scope == QLatin1String("web"))
        return Chip::Warning;
    return Chip::Gradient;
}

/// First existing candidate; used to open the bundled plug-in SDK document.
QString findDoc(const QString &relative)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(relative),
        QDir(appDir).filePath(QStringLiteral("../") + relative),
        QDir(appDir).filePath(QStringLiteral("../../") + relative),
        QDir(appDir).filePath(QStringLiteral("../../../") + relative),
        QDir::current().filePath(relative),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    }
    return QString();
}

} // namespace

PluginExtensionsPage::PluginExtensionsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    layout->addWidget(buildToolbarCard());
    layout->addWidget(buildListCard(), 1);

    PluginHost *host = AppContext::instance()->plugins();
    connect(host, &PluginHost::pluginsChanged, this, &PluginExtensionsPage::refresh);
    connect(host, &PluginHost::loadFinished, this, &PluginExtensionsPage::refresh);
    // Script level diagnostics (mcsm.log, load errors, event handler failures)
    // are surfaced in the status line so a broken plugin is visible immediately.
    connect(host, &PluginHost::pluginMessage, this, [this](const QString &pluginId, const QString &message) {
        setStatus(QStringLiteral("[%1] %2").arg(pluginId, message));
    });
}

QWidget *PluginExtensionsPage::buildToolbarCard()
{
    auto *card = new CardFrame(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);
    layout->addWidget(makeSectionHeader(
        QStringLiteral("插件扩展"),
        QStringLiteral("用 zip 或 URL 添加插件；插件可以为管理器增加前端页面、后端能力或本地 Web 面板"),
        nullptr, card));

    auto *row = new QHBoxLayout();
    row->setSpacing(10);
    auto *add = new GradientButton(QStringLiteral("添加插件"), card);
    add->setGlyph(QStringLiteral("＋"));
    add->setCompact(true);
    m_refresh = new GradientButton(QStringLiteral("刷新"), card);
    m_refresh->setStyle(GradientButton::Outline);
    m_refresh->setCompact(true);
    auto *folder = new GradientButton(QStringLiteral("打开插件目录"), card);
    folder->setStyle(GradientButton::Outline);
    folder->setCompact(true);
    auto *docs = new GradientButton(QStringLiteral("接口文档"), card);
    docs->setStyle(GradientButton::Outline);
    docs->setCompact(true);
    m_summary = makeLabel(QStringLiteral("尚未读取插件列表"), QStringLiteral("hint"), card);

    row->addWidget(add);
    row->addWidget(m_refresh);
    row->addWidget(folder);
    row->addWidget(docs);
    row->addWidget(m_summary, 1);
    layout->addLayout(row);

    m_status = makeLabel(QString(), QStringLiteral("hint"), card);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    connect(add, &QPushButton::clicked, this, &PluginExtensionsPage::openImport);
    connect(m_refresh, &QPushButton::clicked, this, [this]() {
        setStatus(QStringLiteral("正在重新读取插件…"));
        AppContext::instance()->plugins()->reload();
    });
    connect(folder, &QPushButton::clicked, this, [this]() {
        QString path = AppContext::instance()->plugins()->pluginsDir();
        if (path.isEmpty())
            path = AppContext::instance()->dataHome() + QStringLiteral("/plugins");
        openFolder(path);
    });
    connect(docs, &QPushButton::clicked, this, &PluginExtensionsPage::openDocs);
    return card;
}

QWidget *PluginExtensionsPage::buildListCard()
{
    auto *card = new CardFrame(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);
    layout->addWidget(makeSectionHeader(QStringLiteral("已安装插件"),
                                        QStringLiteral("启用 / 停用会立即生效，卸载不会删除插件数据目录"),
                                        nullptr, card));
    m_empty = makeLabel(QStringLiteral("还没有安装任何插件。点击上方「添加插件」导入 zip 或从 URL 安装。"),
                        QStringLiteral("hint"), card);
    layout->addWidget(m_empty);
    m_listLayout = new QVBoxLayout();
    m_listLayout->setSpacing(10);
    layout->addLayout(m_listLayout);
    layout->addStretch(1);
    return card;
}

void PluginExtensionsPage::refresh()
{
    if (!m_listLayout)
        return;
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    PluginHost *host = AppContext::instance()->plugins();
    const QJsonArray packages = host->packages();
    int enabled = 0;
    for (const QJsonValue &value : packages) {
        if (value.toObject().value(QStringLiteral("enabled")).toBool())
            ++enabled;
    }
    m_summary->setText(QStringLiteral("共 %1 个插件，%2 个已启用%3")
                           .arg(packages.size())
                           .arg(enabled)
                           .arg(host->loaded().isEmpty()
                                    ? QString()
                                    : QStringLiteral(" · 已加载前端：%1")
                                          .arg(host->loaded().join(QStringLiteral(", ")))));
    m_empty->setVisible(packages.isEmpty());

    for (const QJsonValue &value : packages)
        m_listLayout->addWidget(buildRow(value.toObject()));

    const QStringList errors = host->errors();
    if (!errors.isEmpty())
        m_status->setText(QStringLiteral("⚠ %1").arg(errors.join(QStringLiteral("；"))));
}

QWidget *PluginExtensionsPage::buildRow(const QJsonObject &plugin)
{
    auto *row = new QFrame(this);
    row->setProperty("role", "pluginRow");
    auto *layout = new QVBoxLayout(row);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    const QString id = plugin.value(QStringLiteral("id")).toString();
    const QString name = plugin.value(QStringLiteral("name")).toString(id);
    const bool enabled = plugin.value(QStringLiteral("enabled")).toBool();

    auto *head = new QHBoxLayout();
    head->setSpacing(8);
    auto *title = makeLabel(name, QStringLiteral("subtitle"), row);
    head->addWidget(title);
    auto *version = new Chip(QStringLiteral("v%1").arg(plugin.value(QStringLiteral("version")).toString()), row);
    version->setTone(Chip::Neutral);
    version->setCompact(true);
    head->addWidget(version);
    auto *scope = new Chip(plugin.value(QStringLiteral("scopeLabel")).toString(), row);
    scope->setTone(toneForScope(plugin.value(QStringLiteral("scope")).toString()));
    scope->setCompact(true);
    head->addWidget(scope);
    auto *state = new Chip(enabled ? QStringLiteral("已启用") : QStringLiteral("已停用"), row);
    state->setTone(enabled ? Chip::Success : Chip::Neutral);
    state->setCompact(true);
    head->addWidget(state);
    head->addStretch(1);

    auto *toggle = new ToggleSwitch(row);
    toggle->setOnText(QStringLiteral("启用"), QStringLiteral("停用"));
    toggle->setChecked(enabled);
    toggle->setToolTip(enabled ? QStringLiteral("点击停用该插件") : QStringLiteral("点击启用该插件"));
    head->addWidget(toggle);
    layout->addLayout(head);

    const QString description = plugin.value(QStringLiteral("description")).toString();
    if (!description.isEmpty()) {
        auto *desc = makeLabel(description, QStringLiteral("hint"), row);
        desc->setWordWrap(true);
        layout->addWidget(desc);
    }
    const QString author = plugin.value(QStringLiteral("author")).toString();
    auto *meta = makeLabel(QStringLiteral("%1 · %2 部分%3")
                               .arg(id, [&plugin]() {
                                   QStringList parts;
                                   for (const QJsonValue &value : plugin.value(QStringLiteral("parts")).toArray())
                                       parts << value.toString();
                                   return parts.join(QLatin1String(" + "));
                               }(),
                                    author.isEmpty() ? QString() : QStringLiteral(" · %1").arg(author)),
                           QStringLiteral("hint"), row);
    layout->addWidget(meta);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(8);
    if (plugin.value(QStringLiteral("hasFrontend")).toBool()) {
        auto *open = new GradientButton(QStringLiteral("打开页面"), row);
        open->setStyle(GradientButton::Soft);
        open->setCompact(true);
        connect(open, &QPushButton::clicked, this, [this, id]() { openPluginPage(id); });
        actions->addWidget(open);
    }
    if (plugin.value(QStringLiteral("hasWeb")).toBool()
        && !plugin.value(QStringLiteral("webEntry")).toString().isEmpty()) {
        auto *panel = new GradientButton(QStringLiteral("打开面板"), row);
        panel->setStyle(GradientButton::Soft);
        panel->setCompact(true);
        connect(panel, &QPushButton::clicked, this, [this, plugin]() { openPanel(plugin); });
        actions->addWidget(panel);
    }
    auto *folder = new GradientButton(QStringLiteral("文件夹"), row);
    folder->setStyle(GradientButton::Outline);
    folder->setCompact(true);
    connect(folder, &QPushButton::clicked, this, [this, plugin]() {
        openFolder(plugin.value(QStringLiteral("path")).toString());
    });
    actions->addWidget(folder);
    auto *remove = new GradientButton(QStringLiteral("卸载"), row);
    remove->setStyle(GradientButton::Danger);
    remove->setCompact(true);
    connect(remove, &QPushButton::clicked, this, [this, id, name]() { removePlugin(id, name); });
    actions->addWidget(remove);
    actions->addStretch(1);
    layout->addLayout(actions);

    connect(toggle, &ToggleSwitch::toggled, this, [this, id](bool checked) {
        setEnabled(id, checked);
    });
    return row;
}

void PluginExtensionsPage::openImport()
{
    PluginImportDialog dialog(this);
    connect(&dialog, &PluginImportDialog::installed, this, [this](const QString &id) {
        setStatus(QStringLiteral("插件 %1 安装完成，正在刷新列表…").arg(id));
        QTimer::singleShot(150, this, [this]() { AppContext::instance()->plugins()->reload(); });
    });
    dialog.exec();
    AppContext::instance()->plugins()->reload();
}

void PluginExtensionsPage::setEnabled(const QString &pluginId, bool enabled)
{
    setStatus(QStringLiteral("正在%1 %2…").arg(enabled ? QStringLiteral("启用") : QStringLiteral("停用"),
                                              pluginId));
    AppContext::instance()->backend()->request(
        {QStringLiteral("plugin"), QStringLiteral("package"),
         enabled ? QStringLiteral("enable") : QStringLiteral("disable"),
         QStringLiteral("--name"), pluginId},
        this, [this, pluginId, enabled](const Reply &reply) {
            if (!reply.ok) {
                setStatus(QStringLiteral("操作失败：%1").arg(reply.errorText()));
            } else {
                setStatus(QStringLiteral("✔ %1 已%2")
                              .arg(pluginId, enabled ? QStringLiteral("启用") : QStringLiteral("停用")));
                AppContext::instance()->logActivity(
                    QStringLiteral("插件 %1 已%2")
                        .arg(pluginId, enabled ? QStringLiteral("启用") : QStringLiteral("停用")),
                    QStringLiteral("info"));
            }
            AppContext::instance()->plugins()->reload();
        });
}

void PluginExtensionsPage::removePlugin(const QString &pluginId, const QString &name)
{
    const auto answer = QMessageBox::question(
        this, QStringLiteral("卸载插件"),
        QStringLiteral("确定要卸载「%1」吗？\n\n插件目录会被删除（插件数据目录保留）。").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    AppContext::instance()->backend()->request(
        {QStringLiteral("plugin"), QStringLiteral("package"), QStringLiteral("remove"),
         QStringLiteral("--name"), pluginId},
        this, [this, pluginId](const Reply &reply) {
            setStatus(reply.ok ? QStringLiteral("✔ 已卸载 %1").arg(pluginId)
                               : QStringLiteral("卸载失败：%1").arg(reply.errorText()));
            if (reply.ok)
                AppContext::instance()->logActivity(QStringLiteral("已卸载插件 %1").arg(pluginId),
                                                    QStringLiteral("warn"));
            AppContext::instance()->plugins()->reload();
        });
}

void PluginExtensionsPage::openFolder(const QString &path)
{
    QString target = path;
    if (target.isEmpty())
        target = AppContext::instance()->dataHome() + QStringLiteral("/plugins");
    if (!QFileInfo::exists(target)) {
        setStatus(QStringLiteral("目录不存在：%1").arg(target));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::cleanPath(target)));
}

void PluginExtensionsPage::openPluginPage(const QString &pluginId)
{
    for (const PluginPageInfo &page : AppContext::instance()->plugins()->pages()) {
        if (page.pluginId != pluginId)
            continue;
        AppContext::instance()->plugins()->requestPageOpen(page.id);
        setStatus(QStringLiteral("已切换到插件页面「%1」").arg(page.title));
        return;
    }
    setStatus(QStringLiteral("插件 %1 提供了前端部分，但没有注册页面（可用 mcsm.ui.registerPage 注册）")
                  .arg(pluginId));
}

void PluginExtensionsPage::openPanel(const QJsonObject &plugin)
{
    const QString id = plugin.value(QStringLiteral("id")).toString();
    const QString panelPath = QDir(plugin.value(QStringLiteral("path")).toString())
                                  .filePath(plugin.value(QStringLiteral("webEntry")).toString());
    if (plugin.value(QStringLiteral("web")).toObject().contains(QStringLiteral("service"))) {
        setStatus(QStringLiteral("正在启动 %1 的本地服务…").arg(id));
        AppContext::instance()->backend()->request(
            {QStringLiteral("plugin"), QStringLiteral("service"), QStringLiteral("start"),
             QStringLiteral("--name"), id},
            this, [this, id](const Reply &reply) {
                if (!reply.ok) {
                    setStatus(QStringLiteral("启动面板服务失败：%1").arg(reply.errorText()));
                    return;
                }
                const QString url = reply.data.value(QStringLiteral("url")).toString();
                if (!url.isEmpty()) {
                    setStatus(QStringLiteral("✔ %1 面板已启动：%2").arg(id, url));
                    QDesktopServices::openUrl(QUrl(url));
                } else {
                    setStatus(QStringLiteral("插件服务已启动，但未声明端口（web.port）"));
                }
            });
        return;
    }
    if (QFileInfo::exists(panelPath)) {
        setStatus(QStringLiteral("已在浏览器中打开 %1 的静态面板").arg(id));
        QDesktopServices::openUrl(QUrl::fromLocalFile(panelPath));
        return;
    }
    setStatus(QStringLiteral("该插件没有可打开的面板"));
}

void PluginExtensionsPage::openDocs()
{
    const QString doc = findDoc(QStringLiteral("docs/PLUGIN-SDK.md"));
    if (doc.isEmpty()) {
        setStatus(QStringLiteral("未找到 docs/PLUGIN-SDK.md；可用 `mcsm-cli plugin api` 导出接口定义"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(doc));
    setStatus(QStringLiteral("已打开接口文档：%1").arg(doc));
}

void PluginExtensionsPage::setStatus(const QString &text)
{
    if (m_status)
        m_status->setText(text);
}

} // namespace mcsm
