#include "pages/ServersPage.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "widgets/Chip.h"
#include "widgets/LogView.h"

namespace mcsm {
namespace {

/// Small helper that renders a caption + value row inside the detail card.
QWidget *makeInfoRow(const QString &label, QLabel *value, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto *key = makeLabel(label, QStringLiteral("caption"), row);
    key->setMinimumWidth(72);
    layout->addWidget(key);
    layout->addWidget(value, 1);
    return row;
}

} // namespace

ServersPage::ServersPage(QWidget *parent)
    : PageBase(QStringLiteral("服务器"),
               QStringLiteral("选择服务器即可启动、查看日志、调整配置或创建备份"), parent)
{
    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(8);
    auto *refreshButton = new GradientButton(QStringLiteral("刷新"), toolbar);
    refreshButton->setStyle(GradientButton::Outline);
    refreshButton->setCompact(true);
    auto *createButton = new GradientButton(QStringLiteral("新建服务器"), toolbar);
    createButton->setGlyph(QStringLiteral("＋"));
    createButton->setCompact(true);
    toolbarLayout->addWidget(refreshButton);
    toolbarLayout->addWidget(createButton);
    setHeaderTrailing(toolbar);

    auto *columns = new QHBoxLayout();
    columns->setSpacing(16);
    columns->addWidget(buildListCard(), 2);
    columns->addWidget(buildDetailCard(), 5);
    body()->addLayout(columns, 1);

    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        AppContext::instance()->refreshServers();
    });
    connect(createButton, &QPushButton::clicked, this, &ServersPage::createServerRequested);
    connect(AppContext::instance()->servers(), &ServerModel::changed, this, &ServersPage::onServersChanged);
    connect(AppContext::instance(), &AppContext::selectionChanged, this,
            &ServersPage::onServerSelectionChanged);
    rebuildList();
}

QWidget *ServersPage::buildListCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 16, 10);
    layout->addWidget(makeSectionHeader(QStringLiteral("服务器列表"),
                                        QStringLiteral("点击卡片切换"), nullptr, card));
    m_list = new QListWidget(card);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSpacing(6);
    m_list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; }"));
    layout->addWidget(m_list, 1);

    m_emptyHint = makeLabel(QStringLiteral("还没有服务器，点击右上角“新建服务器”开始。"),
                            QStringLiteral("hint"), card);
    layout->addWidget(m_emptyHint);

    connect(m_list, &QListWidget::itemSelectionChanged, this, &ServersPage::onListSelectionChanged);
    return card;
}

QWidget *ServersPage::buildDetailCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 14);

    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(10);
    m_statusDot = new StatusDot(card);
    m_statusDot->setDiameter(12);
    auto *titleColumn = new QVBoxLayout();
    titleColumn->setSpacing(2);
    m_nameLabel = makeLabel(QStringLiteral("未选择服务器"), QStringLiteral("title"), card);
    m_summaryLabel = makeLabel(QStringLiteral("从左侧列表选择一台服务器"), QStringLiteral("caption"), card);
    titleColumn->addWidget(m_nameLabel);
    titleColumn->addWidget(m_summaryLabel);
    headerRow->addWidget(m_statusDot, 0, Qt::AlignVCenter);
    headerRow->addLayout(titleColumn, 1);

    m_typeChip = new Chip(QStringLiteral("--"), card);
    m_typeChip->setTone(Chip::Pink);
    m_versionChip = new Chip(QStringLiteral("--"), card);
    m_versionChip->setTone(Chip::Blue);
    m_portChip = new Chip(QStringLiteral("--"), card);
    m_javaChip = new Chip(QStringLiteral("--"), card);
    headerRow->addWidget(m_typeChip, 0, Qt::AlignVCenter);
    headerRow->addWidget(m_versionChip, 0, Qt::AlignVCenter);
    headerRow->addWidget(m_portChip, 0, Qt::AlignVCenter);
    headerRow->addWidget(m_javaChip, 0, Qt::AlignVCenter);
    layout->addLayout(headerRow);

    m_errorBanner = new CardFrame(card);
    m_errorBanner->setVariant(CardFrame::Alt);
    auto *errorLayout = new QHBoxLayout(m_errorBanner);
    errorLayout->setContentsMargins(16, 12, 16, 12);
    errorLayout->setSpacing(12);
    auto *errorIcon = new QLabel(QStringLiteral("!"), m_errorBanner);
    errorIcon->setFixedSize(26, 26);
    errorIcon->setAlignment(Qt::AlignCenter);
    auto *errorColumn = new QVBoxLayout();
    errorColumn->setSpacing(2);
    m_errorText = makeLabel(QString(), QStringLiteral("subtitle"), m_errorBanner);
    m_errorHint = makeLabel(QString(), QStringLiteral("hint"), m_errorBanner);
    errorColumn->addWidget(m_errorText);
    errorColumn->addWidget(m_errorHint);
    auto *showLog = new GradientButton(QStringLiteral("查看日志"), m_errorBanner);
    showLog->setStyle(GradientButton::Outline);
    showLog->setCompact(true);
    errorLayout->addWidget(errorIcon, 0, Qt::AlignTop);
    errorLayout->addLayout(errorColumn, 1);
    errorLayout->addWidget(showLog, 0, Qt::AlignVCenter);
    m_errorBanner->hide();
    layout->addWidget(m_errorBanner);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(8);
    m_primaryButton = new GradientButton(QStringLiteral("一键启动"), card);
    m_primaryButton->setGlyph(QStringLiteral("▶"));
    m_restartButton = new GradientButton(QStringLiteral("重启"), card);
    m_restartButton->setStyle(GradientButton::Soft);
    m_killButton = new GradientButton(QStringLiteral("强制停止"), card);
    m_killButton->setStyle(GradientButton::Danger);
    m_consoleButton = new GradientButton(QStringLiteral("控制台"), card);
    m_consoleButton->setStyle(GradientButton::Outline);
    m_configButton = new GradientButton(QStringLiteral("配置"), card);
    m_configButton->setStyle(GradientButton::Outline);
    m_backupButton = new GradientButton(QStringLiteral("备份"), card);
    m_backupButton->setStyle(GradientButton::Outline);
    m_pluginButton = new GradientButton(QStringLiteral("插件"), card);
    m_pluginButton->setStyle(GradientButton::Outline);

    const QVector<GradientButton *> actionButtons = {m_primaryButton, m_restartButton, m_killButton,
                                                     m_consoleButton, m_configButton, m_backupButton,
                                                     m_pluginButton};
    for (GradientButton *button : actionButtons) {
        button->setCompact(true);
        actions->addWidget(button);
    }
    actions->addStretch(1);
    m_busyLabel = makeLabel(QString(), QStringLiteral("hint"), card);
    actions->addWidget(m_busyLabel, 0, Qt::AlignVCenter);
    layout->addLayout(actions);

    auto *infoCard = new CardFrame(card);
    infoCard->setVariant(CardFrame::Alt);
    auto *infoLayout = new QHBoxLayout(infoCard);
    infoLayout->setContentsMargins(18, 14, 18, 14);
    infoLayout->setSpacing(24);

    m_valuePort = makeLabel(QStringLiteral("--"), QStringLiteral("subtitle"), infoCard);
    m_valueMemory = makeLabel(QStringLiteral("--"), QStringLiteral("subtitle"), infoCard);
    m_valueJava = makeLabel(QStringLiteral("--"), QStringLiteral("caption"), infoCard);
    m_valueContainer = makeLabel(QStringLiteral("--"), QStringLiteral("caption"), infoCard);
    m_valueDirectory = makeLabel(QStringLiteral("--"), QStringLiteral("caption"), infoCard);
    m_valueStarted = makeLabel(QStringLiteral("--"), QStringLiteral("caption"), infoCard);
    m_valueSchedule = makeLabel(QStringLiteral("--"), QStringLiteral("caption"), infoCard);

    auto *leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(8);
    leftColumn->addWidget(makeInfoRow(QStringLiteral("端口"), m_valuePort, infoCard));
    leftColumn->addWidget(makeInfoRow(QStringLiteral("内存"), m_valueMemory, infoCard));
    leftColumn->addWidget(makeInfoRow(QStringLiteral("运行环境"), m_valueJava, infoCard));
    leftColumn->addWidget(makeInfoRow(QStringLiteral("定时备份"), m_valueSchedule, infoCard));

    auto *rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(8);
    rightColumn->addWidget(makeInfoRow(QStringLiteral("容器"), m_valueContainer, infoCard));
    rightColumn->addWidget(makeInfoRow(QStringLiteral("目录"), m_valueDirectory, infoCard));
    rightColumn->addWidget(makeInfoRow(QStringLiteral("最近启动"), m_valueStarted, infoCard));

    infoLayout->addLayout(leftColumn, 3);
    infoLayout->addLayout(rightColumn, 4);
    layout->addWidget(infoCard);

    auto *logCard = new CardFrame(card);
    auto *logLayout = new QVBoxLayout(logCard);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(0);
    auto *logHeader = new QWidget(logCard);
    auto *logHeaderLayout = new QHBoxLayout(logHeader);
    logHeaderLayout->setContentsMargins(18, 14, 18, 4);
    logHeaderLayout->setSpacing(8);
    logHeaderLayout->addWidget(
        makeSectionHeader(QStringLiteral("最近日志"),
                          QStringLiteral("启动异常时会在这里给出原因"), nullptr, logHeader),
        1);
    auto *refreshLog = new GradientButton(QStringLiteral("刷新"), logHeader);
    refreshLog->setStyle(GradientButton::Outline);
    refreshLog->setCompact(true);
    logHeaderLayout->addWidget(refreshLog, 0, Qt::AlignVCenter);
    logLayout->addWidget(logHeader);

    m_logPreview = new LogView(logCard);
    m_logPreview->setMinimumHeight(150);
    m_logPreview->setMaxLines(400);
    logLayout->addWidget(m_logPreview, 1);
    layout->addWidget(logCard, 1);

    connect(m_primaryButton, &QPushButton::clicked, this, [this]() {
        if (m_current.id.isEmpty())
            return;
        if (m_current.isRunning())
            requestStop(m_current.id, false);
        else
            requestStart(m_current.id);
    });
    connect(m_restartButton, &QPushButton::clicked, this, [this]() {
        if (m_current.id.isEmpty())
            return;
        const QString id = m_current.id;
        setBusy(true, QStringLiteral("正在重启…"));
        AppContext::instance()->backend()->request(
            {QStringLiteral("server"), QStringLiteral("stop"), QStringLiteral("--id"), id}, this,
            [this, id](const Reply &) { requestStart(id); });
    });
    connect(m_killButton, &QPushButton::clicked, this, [this]() {
        if (!m_current.id.isEmpty())
            requestStop(m_current.id, true);
    });
    connect(m_consoleButton, &QPushButton::clicked, this, [this]() {
        if (!m_current.id.isEmpty())
            emit openConsoleRequested(m_current.id);
    });
    connect(m_configButton, &QPushButton::clicked, this, [this]() {
        if (!m_current.id.isEmpty())
            emit openConfigRequested(m_current.id);
    });
    connect(m_backupButton, &QPushButton::clicked, this, [this]() {
        if (!m_current.id.isEmpty())
            emit openBackupRequested(m_current.id);
    });
    connect(m_pluginButton, &QPushButton::clicked, this, [this]() {
        if (!m_current.id.isEmpty())
            emit openPluginRequested(m_current.id);
    });
    connect(showLog, &QPushButton::clicked, this, [this]() {
        if (!m_current.id.isEmpty())
            emit openConsoleRequested(m_current.id);
    });
    connect(refreshLog, &QPushButton::clicked, this, &ServersPage::refreshLogPreview);

    return card;
}

void ServersPage::rebuildList()
{
    if (!m_list)
        return;
    const QString selected = AppContext::instance()->selectedServerId();
    m_list->blockSignals(true);
    m_list->clear();

    const QVector<ServerInfo> servers = AppContext::instance()->servers()->servers();
    for (const ServerInfo &info : servers) {
        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, info.id);

        auto *widget = new QWidget(m_list);
        auto *layout = new QVBoxLayout(widget);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(6);

        auto *topRow = new QHBoxLayout();
        topRow->setSpacing(8);
        auto *dot = new StatusDot(widget);
        dot->setState(info.status);
        auto *name = makeLabel(info.name, QStringLiteral("subtitle"), widget);
        topRow->addWidget(dot, 0, Qt::AlignVCenter);
        topRow->addWidget(name, 1);
        layout->addLayout(topRow);

        auto *chips = new QHBoxLayout();
        chips->setSpacing(6);
        auto *typeChip = new Chip(info.typeLabel(), widget);
        typeChip->setTone(Chip::Pink);
        typeChip->setCompact(true);
        const QString versionText = info.mcVersion.isEmpty() ? QStringLiteral("自定义")
                                                            : info.mcVersion;
        auto *versionChip = new Chip(versionText, widget);
        versionChip->setTone(Chip::Blue);
        versionChip->setCompact(true);
        auto *statusChip = new Chip(info.statusText(), widget);
        statusChip->setCompact(true);
        switch (info.state()) {
        case ServerStatus::Running: statusChip->setTone(Chip::Success); break;
        case ServerStatus::Starting:
        case ServerStatus::Stopping: statusChip->setTone(Chip::Warning); break;
        case ServerStatus::Error: statusChip->setTone(Chip::Danger); break;
        default: statusChip->setTone(Chip::Neutral); break;
        }
        chips->addWidget(typeChip);
        chips->addWidget(versionChip);
        chips->addWidget(statusChip);
        chips->addStretch(1);
        layout->addLayout(chips);

        item->setSizeHint(widget->sizeHint());
        m_list->setItemWidget(item, widget);
        if (info.id == selected)
            m_list->setCurrentItem(item);
    }
    m_list->blockSignals(false);
    if (m_emptyHint)
        m_emptyHint->setVisible(servers.isEmpty());
}

void ServersPage::onServersChanged()
{
    // a hidden page only records that it is stale
    if (!m_active) {
        m_listDirty = true;
        return;
    }
    rebuildList();
    m_listDirty = false;
    const QString id = AppContext::instance()->selectedServerId();
    if (!id.isEmpty()
        && AppContext::instance()->throttle(QStringLiteral("servers-detail"), 1500))
        loadDetail(id);
}

void ServersPage::onListSelectionChanged()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return;
    AppContext::instance()->setSelectedServerId(id);
    loadDetail(id);
}

void ServersPage::onServerSelectionChanged(const QString &serverId)
{
    if (serverId.isEmpty())
        return;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() != serverId)
            continue;
        m_list->blockSignals(true);
        m_list->setCurrentRow(i);
        m_list->blockSignals(false);
        break;
    }
    loadDetail(serverId);
}

void ServersPage::onActivated()
{
    m_active = true;
    const QString id = AppContext::instance()->selectedServerId();
    // always load when the selected server changed; only rate limit re-visits of
    // the same server (rapid menu switching should not spawn a process per click)
    const bool changed = id != m_current.id;
    if (!id.isEmpty()
        && (changed || AppContext::instance()->throttle(QStringLiteral("servers-detail"), 1500)))
        loadDetail(id);
    if (m_listDirty) {
        rebuildList();
        m_listDirty = false;
    }
}

void ServersPage::onDeactivated()
{
    m_active = false;
}

void ServersPage::loadDetail(const QString &serverId)
{
    if (serverId.isEmpty())
        return;
    AppContext::instance()->backend()->request(
        {QStringLiteral("server"), QStringLiteral("get"), QStringLiteral("--id"), serverId}, this,
        [this, serverId](const Reply &reply) {
            if (!reply.ok)
                return;
            ServerInfo info = ServerInfo::fromJson(reply.data);
            if (info.id.isEmpty())
                info.id = serverId;
            applyDetail(info);
            const QJsonArray tail = reply.data.value(QStringLiteral("logTail")).toArray();
            QStringList lines;
            for (const QJsonValue &value : tail)
                lines << value.toString();
            if (!lines.isEmpty())
                m_logPreview->setLogLines(lines);
        });
}

void ServersPage::applyDetail(const ServerInfo &info)
{
    const Palette &palette = ThemeManager::instance()->palette();
    m_current = info;
    m_statusDot->setState(info.status);
    m_nameLabel->setText(info.name.isEmpty() ? info.id : info.name);
    m_summaryLabel->setText(info.summary());
    m_typeChip->setText(info.typeLabel());
    m_versionChip->setText(info.mcVersion.isEmpty() ? QStringLiteral("自定义版本") : info.mcVersion);
    m_portChip->setText(QStringLiteral("端口 %1").arg(info.port));
    m_javaChip->setText(info.javaLabel());

    m_valuePort->setText(QString::number(info.port));
    m_valueMemory->setText(info.memoryText());
    m_valueJava->setText(info.image.isEmpty() ? QStringLiteral("未配置") : info.image);
    m_valueContainer->setText(QStringLiteral("mcsm-%1").arg(info.id));
    m_valueDirectory->setText(info.dir);
    m_valueDirectory->setToolTip(info.dir);
    m_valueStarted->setText(info.lastStartAt.isValid()
                                ? info.lastStartAt.toString(QStringLiteral("MM-dd HH:mm"))
                                : QStringLiteral("尚未启动"));
    if (info.backupSchedule.value(QStringLiteral("enabled")).toBool()) {
        m_valueSchedule->setText(QStringLiteral("每 %1 分钟")
                                     .arg(info.backupSchedule.value(QStringLiteral("intervalMinutes")).toInt()));
    } else {
        m_valueSchedule->setText(QStringLiteral("未开启"));
    }

    if (info.lastError.isEmpty()) {
        m_errorBanner->hide();
    } else {
        m_errorBanner->show();
        m_errorText->setText(info.lastError);
        m_errorText->setStyleSheet(QStringLiteral("color: %1;").arg(palette.danger.name()));
        m_errorHint->setText(QStringLiteral("日志末尾内容已保留在下方，可打开控制台查看完整输出"));
    }
    updateActionButtons(info);
}

void ServersPage::updateActionButtons(const ServerInfo &info)
{
    const bool running = info.isRunning();
    const bool busyState = info.isBusy() || m_busy;
    m_primaryButton->setText(running ? QStringLiteral("停止服务器") : QStringLiteral("一键启动"));
    m_primaryButton->setGlyph(running ? QStringLiteral("■") : QStringLiteral("▶"));
    // when the server runs, stop/restart stay as bright as "force stop": the
    // translucent (Soft) look was being read as "disabled"
    m_primaryButton->setStyle(GradientButton::Primary);
    m_restartButton->setStyle(GradientButton::Primary);
    m_primaryButton->setEnabled(!info.id.isEmpty() && !busyState);
    m_restartButton->setEnabled(running && !busyState);
    m_killButton->setEnabled(running && !busyState);
    m_consoleButton->setEnabled(!info.id.isEmpty());
    m_configButton->setEnabled(!info.id.isEmpty());
    m_backupButton->setEnabled(!info.id.isEmpty());
    m_pluginButton->setEnabled(!info.id.isEmpty());
}

void ServersPage::setBusy(bool busy, const QString &message)
{
    m_busy = busy;
    m_busyLabel->setText(busy ? message : QString());
    updateActionButtons(m_current);
}

void ServersPage::showStartFailure(const QString &message, const QString &hint,
                                   const QStringList &logTail)
{
    const Palette &palette = ThemeManager::instance()->palette();
    m_errorBanner->show();
    m_errorText->setText(message);
    m_errorText->setStyleSheet(QStringLiteral("color: %1;").arg(palette.danger.name()));
    if (!hint.isEmpty()) {
        m_errorHint->setText(hint);
    } else {
        m_errorHint->setText(logTail.isEmpty()
                                 ? QStringLiteral("没有捕获到日志输出，请检查容器是否创建成功")
                                 : QStringLiteral("日志末尾：%1")
                                       .arg(logTail.last().trimmed().left(150)));
    }
    if (!logTail.isEmpty())
        m_logPreview->setLogLines(logTail);
    AppContext::instance()->logActivity(
        QStringLiteral("%1 启动失败：%2").arg(m_current.name.isEmpty() ? m_current.id : m_current.name,
                                              message),
        QStringLiteral("error"));
}

void ServersPage::requestStart(const QString &serverId)
{
    setBusy(true, QStringLiteral("正在启动，等待服务器就绪…"));
    AppContext::instance()->backend()->request(
        {QStringLiteral("server"), QStringLiteral("start"), QStringLiteral("--id"), serverId,
         QStringLiteral("--wait"), QStringLiteral("120")},
        this, [this, serverId](const Reply &reply) {
            setBusy(false);
            QStringList logTail;
            const QJsonArray tail = reply.data.value(QStringLiteral("logTail")).toArray();
            for (const QJsonValue &value : tail)
                logTail << value.toString();
            if (reply.ok) {
                AppContext::instance()->logActivity(
                    QStringLiteral("%1 已启动").arg(m_current.name.isEmpty() ? serverId : m_current.name),
                    QStringLiteral("success"));
                for (const QString &warning : reply.warnings)
                    AppContext::instance()->logActivity(warning, QStringLiteral("warning"));
                m_errorBanner->hide();
                if (!logTail.isEmpty())
                    m_logPreview->setLogLines(logTail);
            } else {
                const QString hint = reply.data.value(QStringLiteral("hint")).toString();
                if (!hint.isEmpty())
                    AppContext::instance()->logActivity(hint, QStringLiteral("warning"));
                showStartFailure(reply.message.isEmpty() ? QStringLiteral("启动失败") : reply.message,
                                 hint, logTail);
            }
            loadDetail(serverId);
            AppContext::instance()->refreshServers();
        });
}

void ServersPage::requestStop(const QString &serverId, bool force)
{
    setBusy(true, force ? QStringLiteral("正在强制停止…") : QStringLiteral("正在保存并停止…"));
    QStringList arguments {QStringLiteral("server"), QStringLiteral("stop"), QStringLiteral("--id"),
                           serverId};
    if (force)
        arguments << QStringLiteral("--force");
    AppContext::instance()->backend()->request(arguments, this, [this, serverId](const Reply &reply) {
        setBusy(false);
        if (reply.ok) {
            AppContext::instance()->logActivity(
                QStringLiteral("%1 已停止").arg(m_current.name.isEmpty() ? serverId : m_current.name),
                QStringLiteral("info"));
        } else {
            AppContext::instance()->logActivity(QStringLiteral("停止失败：%1").arg(reply.errorText()),
                                                QStringLiteral("error"));
        }
        loadDetail(serverId);
        AppContext::instance()->refreshServers();
    });
}

void ServersPage::refreshLogPreview()
{
    if (m_current.id.isEmpty())
        return;
    AppContext::instance()->backend()->request(
        {QStringLiteral("server"), QStringLiteral("logs"), QStringLiteral("--id"), m_current.id,
         QStringLiteral("--tail"), QStringLiteral("120")},
        this, [this](const Reply &reply) {
            if (!reply.ok)
                return;
            const QJsonArray lines = reply.data.value(QStringLiteral("lines")).toArray();
            QStringList list;
            for (const QJsonValue &value : lines)
                list << value.toString();
            m_logPreview->setLogLines(list);
        });
}

} // namespace mcsm
