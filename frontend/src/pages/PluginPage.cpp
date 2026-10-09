#include "pages/PluginPage.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "widgets/Chip.h"

namespace mcsm {

PluginPage::PluginPage(QWidget *parent)
    : PageBase(QStringLiteral("服务器插件"),
               QStringLiteral("为选中的服务器安装 Bukkit / Paper 插件；管理器自身的扩展在「插件扩展」"), parent)
{
    m_selector = new ServerSelector(this);
    setHeaderTrailing(m_selector);

    body()->addWidget(buildSearchCard());

    auto *columns = new QHBoxLayout();
    columns->setSpacing(16);
    columns->addWidget(buildResultsCard(), 3);
    columns->addWidget(buildInstalledCard(), 2);
    body()->addLayout(columns, 1);

    connect(m_selector, &ServerSelector::serverChanged, this, [this](const QString &id) {
        if (id.isEmpty())
            return;
        m_serverId = id;
        AppContext::instance()->setSelectedServerId(id);
        loadInstalled(id);
        m_statusLabel->setText(QStringLiteral("已选择 %1，可搜索并安装插件").arg(id));
    });
    connect(AppContext::instance(), &AppContext::selectionChanged, this,
            &PluginPage::onServerSelectionChanged);
}

QWidget *PluginPage::buildSearchCard()
{
    auto *card = new CardFrame(this);
    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);

    m_source = new QComboBox(card);
    m_source->addItem(QStringLiteral("Modrinth（推荐）"), QStringLiteral("modrinth"));
    m_source->addItem(QStringLiteral("Hangar (PaperMC)"), QStringLiteral("hangar"));
    m_source->addItem(QStringLiteral("SpigotMC（仅搜索）"), QStringLiteral("spiget"));
    m_source->setMinimumWidth(180);

    m_query = new QLineEdit(card);
    m_query->setPlaceholderText(QStringLiteral("搜索插件，例如 EssentialsX、Vault、LuckPerms"));
    auto *searchButton = new GradientButton(QStringLiteral("搜索"), card);
    searchButton->setGlyph(QStringLiteral("⌕"));
    searchButton->setCompact(true);

    m_statusLabel = makeLabel(QStringLiteral("请先选择服务器"), QStringLiteral("hint"), card);

    layout->addWidget(m_source);
    layout->addWidget(m_query, 1);
    layout->addWidget(searchButton);
    layout->addWidget(m_statusLabel, 1);

    connect(searchButton, &QPushButton::clicked, this, &PluginPage::search);
    connect(m_query, &QLineEdit::returnPressed, this, &PluginPage::search);
    return card;
}

QWidget *PluginPage::buildResultsCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 12);
    layout->addWidget(makeSectionHeader(QStringLiteral("搜索结果"),
                                        QStringLiteral("点击安装即写入服务器的 plugins 目录"), nullptr, card));
    m_results = new QListWidget(card);
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setSpacing(8);
    m_results->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; }"));
    layout->addWidget(m_results, 1);
    return card;
}

QWidget *PluginPage::buildInstalledCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 12);
    layout->addWidget(makeSectionHeader(QStringLiteral("已安装插件"),
                                        QStringLiteral("禁用会重命名为 .jar.disabled"), nullptr, card));
    m_installed = new QListWidget(card);
    m_installed->setFrameShape(QFrame::NoFrame);
    m_installed->setSpacing(6);
    m_installed->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; }"));
    layout->addWidget(m_installed, 1);
    m_installedHint = makeLabel(QStringLiteral("还没有安装任何插件。"), QStringLiteral("hint"), card);
    layout->addWidget(m_installedHint);
    return card;
}

void PluginPage::search()
{
    if (m_serverId.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("请先选择服务器"));
        return;
    }
    const QString source = m_source->currentData().toString();
    const QString query = m_query->text().trimmed();
    m_statusLabel->setText(QStringLiteral("正在搜索…"));
    m_results->clear();

    AppContext::instance()->backend()->request(
        {QStringLiteral("plugin"), QStringLiteral("search"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--source"), source, QStringLiteral("--query"), query,
         QStringLiteral("--limit"), QStringLiteral("25")},
        this, [this, source](const Reply &reply) {
            if (!reply.ok) {
                m_statusLabel->setText(QStringLiteral("搜索失败：%1").arg(reply.errorText()));
                return;
            }
            const QJsonArray results = reply.data.value(QStringLiteral("results")).toArray();
            m_statusLabel->setText(QStringLiteral("找到 %1 个插件").arg(results.size()));
            for (const QJsonValue &value : results) {
                const QJsonObject hit = value.toObject();
                auto *item = new QListWidgetItem(m_results);
                auto *widget = new QWidget(m_results);
                auto *layout = new QHBoxLayout(widget);
                layout->setContentsMargins(10, 8, 10, 8);
                layout->setSpacing(12);

                auto *column = new QVBoxLayout();
                column->setSpacing(3);
                auto *titleRow = new QHBoxLayout();
                titleRow->setSpacing(8);
                titleRow->addWidget(makeLabel(hit.value(QStringLiteral("title")).toString(),
                                              QStringLiteral("subtitle"), widget));
                auto *chip = new Chip(source, widget);
                chip->setTone(Chip::Blue);
                chip->setCompact(true);
                titleRow->addWidget(chip);
                const qint64 downloads = qint64(hit.value(QStringLiteral("downloads")).toDouble());
                if (downloads > 0) {
                    auto *downloadChip = new Chip(QStringLiteral("%1 次下载").arg(downloads), widget);
                    downloadChip->setCompact(true);
                    titleRow->addWidget(downloadChip);
                }
                titleRow->addStretch(1);
                column->addLayout(titleRow);
                column->addWidget(makeLabel(hit.value(QStringLiteral("description")).toString(),
                                            QStringLiteral("hint"), widget));
                const QString note = hit.value(QStringLiteral("note")).toString();
                if (!note.isEmpty())
                    column->addWidget(makeLabel(note, QStringLiteral("hint"), widget));
                layout->addLayout(column, 1);

                if (hit.value(QStringLiteral("installable")).toBool()) {
                    auto *install = new GradientButton(QStringLiteral("安装"), widget);
                    install->setCompact(true);
                    layout->addWidget(install, 0, Qt::AlignVCenter);
                    connect(install, &QPushButton::clicked, this, [this, hit]() { installPlugin(hit); });
                } else {
                    auto *open = new GradientButton(QStringLiteral("打开页面"), widget);
                    open->setStyle(GradientButton::Outline);
                    open->setCompact(true);
                    layout->addWidget(open, 0, Qt::AlignVCenter);
                    const QString url = hit.value(QStringLiteral("pageUrl")).toString();
                    connect(open, &QPushButton::clicked, this, [url]() {
                        QDesktopServices::openUrl(QUrl(url));
                    });
                }

                item->setSizeHint(widget->sizeHint());
                m_results->setItemWidget(item, widget);
            }
        });
}

void PluginPage::installPlugin(const QJsonObject &hit)
{
    if (m_serverId.isEmpty() || m_busy)
        return;
    setBusy(true, QStringLiteral("正在下载…"));
    AppContext::instance()->backend()->request(
        {QStringLiteral("plugin"), QStringLiteral("install"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--source"), hit.value(QStringLiteral("source")).toString(),
         QStringLiteral("--slug"), hit.value(QStringLiteral("slug")).toString(),
         QStringLiteral("--title"), hit.value(QStringLiteral("title")).toString()},
        this, [this, hit](const Reply &reply) {
            setBusy(false);
            if (!reply.ok) {
                m_statusLabel->setText(QStringLiteral("安装失败：%1").arg(reply.errorText()));
                AppContext::instance()->logActivity(
                    QStringLiteral("插件安装失败：%1").arg(reply.errorText()), QStringLiteral("error"));
                return;
            }
            const QString file = reply.data.value(QStringLiteral("installedFile")).toString();
            m_statusLabel->setText(QStringLiteral("已安装 %1，重启服务器后生效").arg(file));
            AppContext::instance()->logActivity(
                QStringLiteral("已安装插件 %1").arg(hit.value(QStringLiteral("title")).toString()),
                QStringLiteral("success"));
            loadInstalled(m_serverId);
        });
}

void PluginPage::loadInstalled(const QString &serverId)
{
    if (serverId.isEmpty() || !m_installed)
        return;
    AppContext::instance()->backend()->request(
        {QStringLiteral("plugin"), QStringLiteral("list"), QStringLiteral("--id"), serverId}, this,
        [this](const Reply &reply) {
            m_installed->clear();
            if (!reply.ok)
                return;
            const QJsonArray plugins = reply.data.value(QStringLiteral("plugins")).toArray();
            for (const QJsonValue &value : plugins) {
                const QJsonObject plugin = value.toObject();
                const QString file = plugin.value(QStringLiteral("file")).toString();
                const bool enabled = plugin.value(QStringLiteral("enabled")).toBool();

                auto *item = new QListWidgetItem(m_installed);
                auto *widget = new QWidget(m_installed);
                auto *layout = new QVBoxLayout(widget);
                layout->setContentsMargins(10, 8, 10, 8);
                layout->setSpacing(4);

                auto *topRow = new QHBoxLayout();
                topRow->setSpacing(8);
                topRow->addWidget(makeLabel(plugin.value(QStringLiteral("name")).toString(),
                                            QStringLiteral("subtitle"), widget),
                                  1);
                auto *chip = new Chip(enabled ? QStringLiteral("已启用") : QStringLiteral("已禁用"), widget);
                chip->setTone(enabled ? Chip::Success : Chip::Neutral);
                chip->setCompact(true);
                topRow->addWidget(chip);
                layout->addLayout(topRow);
                layout->addWidget(makeLabel(QStringLiteral("%1 · %2")
                                                .arg(plugin.value(QStringLiteral("sizeText")).toString(),
                                                     plugin.value(QStringLiteral("modifiedAt"))
                                                         .toString()
                                                         .left(16)
                                                         .replace(QLatin1Char('T'), QLatin1Char(' '))),
                                            QStringLiteral("hint"), widget));

                auto *actions = new QHBoxLayout();
                actions->setSpacing(6);
                auto *toggle = new GradientButton(enabled ? QStringLiteral("禁用") : QStringLiteral("启用"),
                                                  widget);
                toggle->setStyle(GradientButton::Outline);
                toggle->setCompact(true);
                auto *remove = new GradientButton(QStringLiteral("删除"), widget);
                remove->setStyle(GradientButton::Danger);
                remove->setCompact(true);
                actions->addWidget(toggle);
                actions->addWidget(remove);
                actions->addStretch(1);
                layout->addLayout(actions);

                connect(toggle, &QPushButton::clicked, this, [this, file, enabled]() {
                    togglePlugin(file, !enabled);
                });
                connect(remove, &QPushButton::clicked, this, [this, file]() { removePlugin(file); });

                item->setSizeHint(widget->sizeHint());
                m_installed->setItemWidget(item, widget);
            }
            m_installedHint->setVisible(plugins.isEmpty());
        });
}

void PluginPage::togglePlugin(const QString &file, bool enabled)
{
    if (m_serverId.isEmpty())
        return;
    AppContext::instance()->backend()->request(
        {QStringLiteral("plugin"), QStringLiteral("toggle"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--file"), file, QStringLiteral("--enabled"),
         enabled ? QStringLiteral("true") : QStringLiteral("false")},
        this, [this, file, enabled](const Reply &reply) {
            if (!reply.ok) {
                AppContext::instance()->logActivity(
                    QStringLiteral("插件状态切换失败：%1").arg(reply.errorText()), QStringLiteral("error"));
                return;
            }
            AppContext::instance()->logActivity(
                QStringLiteral("%1 已%2").arg(file, enabled ? QStringLiteral("启用") : QStringLiteral("禁用")),
                QStringLiteral("info"));
            loadInstalled(m_serverId);
        });
}

void PluginPage::removePlugin(const QString &file)
{
    if (m_serverId.isEmpty())
        return;
    const auto answer = QMessageBox::question(this, QStringLiteral("删除插件"),
                                             QStringLiteral("确定删除 %1 吗？").arg(file),
                                             QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    AppContext::instance()->backend()->request(
        {QStringLiteral("plugin"), QStringLiteral("remove"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--file"), file},
        this, [this, file](const Reply &reply) {
            if (!reply.ok) {
                AppContext::instance()->logActivity(
                    QStringLiteral("删除插件失败：%1").arg(reply.errorText()), QStringLiteral("error"));
                return;
            }
            AppContext::instance()->logActivity(QStringLiteral("已删除插件 %1").arg(file),
                                                QStringLiteral("info"));
            loadInstalled(m_serverId);
        });
}

void PluginPage::setBusy(bool busy, const QString &message)
{
    m_busy = busy;
    if (busy && !message.isEmpty())
        m_statusLabel->setText(message);
}

void PluginPage::onActivated()
{
    const QString id = AppContext::instance()->selectedServerId();
    const bool changed = id != m_serverId;
    if (!id.isEmpty()
        && (changed || AppContext::instance()->throttle(QStringLiteral("plugin-page"), 2000)))
        loadInstalled(id);
}

void PluginPage::onServerSelectionChanged(const QString &serverId)
{
    if (serverId.isEmpty())
        return;
    m_selector->selectServer(serverId);
    m_serverId = serverId;
    loadInstalled(serverId);
}

} // namespace mcsm
