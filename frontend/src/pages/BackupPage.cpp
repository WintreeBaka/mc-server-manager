#include "pages/BackupPage.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "widgets/Chip.h"

namespace mcsm {

BackupPage::BackupPage(QWidget *parent)
    : PageBase(QStringLiteral("存档与备份"),
               QStringLiteral("自动定时备份、手动快照与一键回滚，运行中的服务器会先执行 save-all"),
               parent)
{
    m_selector = new ServerSelector(this);
    setHeaderTrailing(m_selector);

    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(16);
    topRow->addWidget(buildScheduleCard(), 3);
    topRow->addWidget(buildCreateCard(), 2);
    body()->addLayout(topRow);
    body()->addWidget(buildListCard(), 1);

    connect(m_selector, &ServerSelector::serverChanged, this, [this](const QString &id) {
        if (id.isEmpty())
            return;
        m_serverId = id;
        AppContext::instance()->setSelectedServerId(id);
        loadBackups(id);
    });
    connect(AppContext::instance(), &AppContext::selectionChanged, this,
            &BackupPage::onServerSelectionChanged);
}

QWidget *BackupPage::buildScheduleCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 12);
    layout->addWidget(makeSectionHeader(QStringLiteral("定时自动备份"),
                                        QStringLiteral("守护进程每分钟检查一次计划"), nullptr, card));

    auto *grid = new QWidget(card);
    auto *gridLayout = new QHBoxLayout(grid);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(16);

    auto makeSetting = [grid](const QString &label, QWidget *editor, const QString &hint) {
        auto *column = new QVBoxLayout();
        column->setSpacing(4);
        column->addWidget(makeLabel(label, QStringLiteral("caption"), grid));
        column->addWidget(editor);
        column->addWidget(makeLabel(hint, QStringLiteral("hint"), grid));
        return column;
    };

    m_scheduleEnabled = new ToggleSwitch(grid);
    m_scheduleEnabled->setOnText(QStringLiteral("已开启"), QStringLiteral("已关闭"));
    m_interval = new QSpinBox(grid);
    m_interval->setRange(5, 1440);
    m_interval->setSingleStep(15);
    m_interval->setSuffix(QStringLiteral(" 分钟"));
    m_keep = new QSpinBox(grid);
    m_keep->setRange(1, 200);
    m_keep->setSuffix(QStringLiteral(" 份"));
    m_includePlugins = new ToggleSwitch(grid);
    m_saveBefore = new ToggleSwitch(grid);

    gridLayout->addLayout(makeSetting(QStringLiteral("自动备份"), m_scheduleEnabled,
                                      QStringLiteral("关闭后仅保留手动备份")), 2);
    gridLayout->addLayout(makeSetting(QStringLiteral("备份间隔"), m_interval,
                                      QStringLiteral("建议 60 - 360 分钟")), 2);
    gridLayout->addLayout(makeSetting(QStringLiteral("保留份数"), m_keep,
                                      QStringLiteral("超出后自动清理最旧的自动备份")), 2);
    gridLayout->addLayout(makeSetting(QStringLiteral("包含插件"), m_includePlugins,
                                      QStringLiteral("一起归档 plugins 目录")), 2);
    gridLayout->addLayout(makeSetting(QStringLiteral("备份前存档"), m_saveBefore,
                                      QStringLiteral("通过 RCON 执行 save-all flush")), 2);
    layout->addWidget(grid);

    auto *footer = new QHBoxLayout();
    footer->setSpacing(10);
    m_nextDueLabel = makeLabel(QStringLiteral("--"), QStringLiteral("hint"), card);
    auto *save = new GradientButton(QStringLiteral("保存计划"), card);
    save->setCompact(true);
    footer->addWidget(m_nextDueLabel, 1);
    footer->addWidget(save);
    layout->addLayout(footer);

    connect(save, &QPushButton::clicked, this, &BackupPage::saveSchedule);
    return card;
}

QWidget *BackupPage::buildCreateCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 12);
    layout->addWidget(makeSectionHeader(QStringLiteral("立即备份"),
                                        QStringLiteral("为当前存档创建一个快照"), nullptr, card));

    m_noteEdit = new QLineEdit(card);
    m_noteEdit->setPlaceholderText(QStringLiteral("备注，例如：更新插件前"));
    layout->addWidget(m_noteEdit);

    auto *row = new QHBoxLayout();
    row->setSpacing(10);
    auto *pluginsLabel = makeLabel(QStringLiteral("包含插件"), QStringLiteral("caption"), card);
    m_createPlugins = new ToggleSwitch(card);
    m_createPlugins->setChecked(true);
    row->addWidget(pluginsLabel);
    row->addWidget(m_createPlugins);
    row->addStretch(1);
    layout->addLayout(row);

    auto *create = new GradientButton(QStringLiteral("立即创建备份"), card);
    create->setGlyph(QStringLiteral("⛁"));
    layout->addWidget(create);

    m_statsLabel = makeLabel(QStringLiteral("尚未读取备份信息"), QStringLiteral("hint"), card);
    layout->addWidget(m_statsLabel);
    layout->addStretch(1);

    connect(create, &QPushButton::clicked, this, &BackupPage::createBackup);
    return card;
}

QWidget *BackupPage::buildListCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 12);

    auto *header = new QWidget(card);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    headerLayout->addWidget(makeSectionHeader(QStringLiteral("备份列表"),
                                              QStringLiteral("双击可恢复，恢复前会自动再备份一次"),
                                              nullptr, header),
                            1);
    auto *refresh = new GradientButton(QStringLiteral("刷新"), header);
    refresh->setStyle(GradientButton::Outline);
    refresh->setCompact(true);
    headerLayout->addWidget(refresh, 0, Qt::AlignVCenter);
    layout->addWidget(header);

    m_list = new QListWidget(card);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSpacing(6);
    m_list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; }"));
    layout->addWidget(m_list, 1);
    m_emptyHint = makeLabel(QStringLiteral("还没有备份，点击“立即创建备份”生成第一份快照。"),
                            QStringLiteral("hint"), card);
    layout->addWidget(m_emptyHint);

    connect(refresh, &QPushButton::clicked, this, [this]() { loadBackups(m_serverId); });
    return card;
}

void BackupPage::loadBackups(const QString &serverId)
{
    if (serverId.isEmpty() || !m_list)
        return;
    AppContext::instance()->backend()->request(
        {QStringLiteral("backup"), QStringLiteral("list"), QStringLiteral("--id"), serverId}, this,
        [this](const Reply &reply) {
            m_list->clear();
            if (!reply.ok) {
                m_statsLabel->setText(QStringLiteral("读取失败：%1").arg(reply.errorText()));
                return;
            }
            const QJsonObject schedule = reply.data.value(QStringLiteral("schedule")).toObject();
            applySchedule(schedule);
            m_statsLabel->setText(QStringLiteral("共 %1 份备份 · 占用 %2")
                                      .arg(reply.data.value(QStringLiteral("count")).toInt())
                                      .arg(reply.data.value(QStringLiteral("totalSizeText")).toString()));

            const QJsonArray backups = reply.data.value(QStringLiteral("backups")).toArray();
            for (const QJsonValue &value : backups) {
                const QJsonObject backup = value.toObject();
                const QString name = backup.value(QStringLiteral("name")).toString();
                const QString size = backup.value(QStringLiteral("sizeText")).toString();
                const QString created = backup.value(QStringLiteral("createdAt")).toString();
                const QString trigger = backup.value(QStringLiteral("trigger")).toString();
                const QString note = backup.value(QStringLiteral("note")).toString();

                auto *item = new QListWidgetItem(m_list);
                item->setData(Qt::UserRole, name);
                auto *widget = new QWidget(m_list);
                auto *layout = new QHBoxLayout(widget);
                layout->setContentsMargins(10, 8, 10, 8);
                layout->setSpacing(12);

                auto *column = new QVBoxLayout();
                column->setSpacing(2);
                auto *titleRow = new QHBoxLayout();
                titleRow->setSpacing(8);
                titleRow->addWidget(makeLabel(name, QStringLiteral("subtitle"), widget));
                QString triggerLabel = QStringLiteral("手动");
                Chip::Tone triggerTone = Chip::Pink;
                if (trigger == QLatin1String("schedule")) {
                    triggerLabel = QStringLiteral("自动");
                    triggerTone = Chip::Blue;
                } else if (trigger == QLatin1String("pre-restore")) {
                    triggerLabel = QStringLiteral("恢复前");
                    triggerTone = Chip::Warning;
                }
                auto *chip = new Chip(triggerLabel, widget);
                chip->setTone(triggerTone);
                chip->setCompact(true);
                titleRow->addWidget(chip);
                titleRow->addStretch(1);
                column->addLayout(titleRow);
                const QString meta = QStringLiteral("%1 · %2%3")
                                         .arg(created.left(19).replace(QLatin1Char('T'), QLatin1Char(' ')), size,
                                              note.isEmpty() ? QString()
                                                             : QStringLiteral(" · ") + note);
                column->addWidget(makeLabel(meta, QStringLiteral("hint"), widget));
                layout->addLayout(column, 1);

                auto *restore = new GradientButton(QStringLiteral("恢复"), widget);
                restore->setStyle(GradientButton::Outline);
                restore->setCompact(true);
                auto *remove = new GradientButton(QStringLiteral("删除"), widget);
                remove->setStyle(GradientButton::Danger);
                remove->setCompact(true);
                layout->addWidget(restore, 0, Qt::AlignVCenter);
                layout->addWidget(remove, 0, Qt::AlignVCenter);

                connect(restore, &QPushButton::clicked, this, [this, name]() { restoreBackup(name); });
                connect(remove, &QPushButton::clicked, this, [this, name]() { deleteBackup(name); });

                item->setSizeHint(widget->sizeHint());
                m_list->setItemWidget(item, widget);
            }
            m_emptyHint->setVisible(backups.isEmpty());
        });
}

void BackupPage::applySchedule(const QJsonObject &schedule)
{
    m_scheduleEnabled->setChecked(schedule.value(QStringLiteral("enabled")).toBool());
    m_interval->setValue(qMax(5, schedule.value(QStringLiteral("intervalMinutes")).toInt(180)));
    m_keep->setValue(qMax(1, schedule.value(QStringLiteral("keep")).toInt(10)));
    m_includePlugins->setChecked(schedule.value(QStringLiteral("includePlugins")).toBool(true));
    m_saveBefore->setChecked(schedule.value(QStringLiteral("saveBeforeBackup")).toBool(true));

    const QString last = schedule.value(QStringLiteral("lastBackupAt")).toString();
    if (last.isEmpty()) {
        m_nextDueLabel->setText(QStringLiteral("还没有执行过自动备份"));
        return;
    }
    const QDateTime lastTime = QDateTime::fromString(last, Qt::ISODate);
    if (!lastTime.isValid()) {
        m_nextDueLabel->setText(QStringLiteral("上次备份：%1").arg(last));
        return;
    }
    const QDateTime next = lastTime.addSecs(qint64(m_interval->value()) * 60);
    m_nextDueLabel->setText(QStringLiteral("上次备份 %1 · 下次预计 %2")
                                .arg(lastTime.toString(QStringLiteral("MM-dd HH:mm")),
                                     next.toString(QStringLiteral("MM-dd HH:mm"))));
}

void BackupPage::saveSchedule()
{
    if (m_serverId.isEmpty() || m_busy)
        return;
    setBusy(true, QStringLiteral("正在保存计划…"));
    AppContext::instance()->backend()->request(
        {QStringLiteral("backup"), QStringLiteral("schedule"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--enabled"), m_scheduleEnabled->isChecked() ? QStringLiteral("true")
                                                                     : QStringLiteral("false"),
         QStringLiteral("--interval"), QString::number(m_interval->value()),
         QStringLiteral("--keep"), QString::number(m_keep->value()),
         QStringLiteral("--plugins"), m_includePlugins->isChecked() ? QStringLiteral("true")
                                                                    : QStringLiteral("false"),
         QStringLiteral("--save-first"), m_saveBefore->isChecked() ? QStringLiteral("true")
                                                                   : QStringLiteral("false")},
        this, [this](const Reply &reply) {
            setBusy(false);
            if (!reply.ok) {
                AppContext::instance()->logActivity(
                    QStringLiteral("保存备份计划失败：%1").arg(reply.errorText()), QStringLiteral("error"));
                return;
            }
            AppContext::instance()->logActivity(
                QStringLiteral("%1 的自动备份计划已更新").arg(m_serverId), QStringLiteral("success"));
            loadBackups(m_serverId);
        });
}

void BackupPage::createBackup()
{
    if (m_serverId.isEmpty() || m_busy)
        return;
    setBusy(true, QStringLiteral("正在创建备份…"));
    AppContext::instance()->backend()->request(
        {QStringLiteral("backup"), QStringLiteral("create"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--note"),
         m_noteEdit->text().isEmpty() ? QStringLiteral("手动备份") : m_noteEdit->text(),
         QStringLiteral("--plugins"), m_createPlugins->isChecked() ? QStringLiteral("true")
                                                                   : QStringLiteral("false")},
        this, [this](const Reply &reply) {
            setBusy(false);
            if (!reply.ok) {
                AppContext::instance()->logActivity(
                    QStringLiteral("创建备份失败：%1").arg(reply.errorText()), QStringLiteral("error"));
                return;
            }
            m_noteEdit->clear();
            AppContext::instance()->logActivity(
                QStringLiteral("已创建备份 %1（%2）")
                    .arg(reply.data.value(QStringLiteral("name")).toString(),
                         reply.data.value(QStringLiteral("sizeText")).toString()),
                QStringLiteral("success"));
            loadBackups(m_serverId);
        });
}

void BackupPage::restoreBackup(const QString &name)
{
    if (m_serverId.isEmpty() || m_busy)
        return;
    const auto answer = QMessageBox::question(this, QStringLiteral("恢复存档"),
                                             QStringLiteral("确定要用 %1 覆盖当前存档吗？\n"
                                                            "服务器会先停止，并在恢复前自动再备份一次。")
                                                 .arg(name),
                                             QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    setBusy(true, QStringLiteral("正在恢复存档…"));
    AppContext::instance()->backend()->request(
        {QStringLiteral("backup"), QStringLiteral("restore"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--backup"), name, QStringLiteral("--restart"), QStringLiteral("true")},
        this, [this, name](const Reply &reply) {
            setBusy(false);
            if (!reply.ok) {
                AppContext::instance()->logActivity(
                    QStringLiteral("恢复失败：%1").arg(reply.errorText()), QStringLiteral("error"));
                return;
            }
            AppContext::instance()->logActivity(
                QStringLiteral("已从 %1 恢复存档").arg(name), QStringLiteral("success"));
            loadBackups(m_serverId);
        });
}

void BackupPage::deleteBackup(const QString &name)
{
    if (m_serverId.isEmpty() || m_busy)
        return;
    const auto answer = QMessageBox::question(this, QStringLiteral("删除备份"),
                                             QStringLiteral("确定删除 %1 吗？该操作不可撤销。").arg(name),
                                             QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    AppContext::instance()->backend()->request(
        {QStringLiteral("backup"), QStringLiteral("remove"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--backup"), name},
        this, [this, name](const Reply &reply) {
            if (!reply.ok) {
                AppContext::instance()->logActivity(
                    QStringLiteral("删除备份失败：%1").arg(reply.errorText()), QStringLiteral("error"));
                return;
            }
            AppContext::instance()->logActivity(QStringLiteral("已删除备份 %1").arg(name),
                                                QStringLiteral("info"));
            loadBackups(m_serverId);
        });
}

void BackupPage::setBusy(bool busy, const QString &message)
{
    m_busy = busy;
    if (busy && !message.isEmpty())
        m_statsLabel->setText(message);
}

void BackupPage::onActivated()
{
    const QString id = AppContext::instance()->selectedServerId();
    const bool changed = id != m_serverId;
    if (!id.isEmpty()
        && (changed || AppContext::instance()->throttle(QStringLiteral("backup-list"), 2000)))
        loadBackups(id);
}

void BackupPage::onServerSelectionChanged(const QString &serverId)
{
    if (serverId.isEmpty())
        return;
    m_selector->selectServer(serverId);
    m_serverId = serverId;
    loadBackups(serverId);
}

} // namespace mcsm
