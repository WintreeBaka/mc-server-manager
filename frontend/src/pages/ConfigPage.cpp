#include "pages/ConfigPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "widgets/AnimatedStack.h"
#include "widgets/Chip.h"

namespace mcsm {
namespace {

QString jsonToText(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 12);
    return value.toString();
}

} // namespace

ConfigPage::ConfigPage(QWidget *parent)
    : PageBase(QStringLiteral("配置文件"),
               QStringLiteral("快捷表单与专家模式双模式编辑 server.properties，每次保存都会自动备份"),
               parent)
{
    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(10);
    m_modeSwitch = new SegmentedControl(toolbar);
    m_modeSwitch->setItems({QStringLiteral("快捷配置"), QStringLiteral("专家模式")});
    m_selector = new ServerSelector(toolbar);
    toolbarLayout->addWidget(m_modeSwitch);
    toolbarLayout->addWidget(m_selector);
    setHeaderTrailing(toolbar);

    auto *columns = new QHBoxLayout();
    columns->setSpacing(16);
    columns->addWidget(buildHistoryCard(), 2);

    auto *mainCard = new CardFrame(this);
    auto *mainLayout = new QVBoxLayout(mainCard);
    mainLayout->setContentsMargins(18, 16, 18, 16);
    mainLayout->setSpacing(12);

    m_stack = new AnimatedStack(mainCard);
    m_stack->addPage(buildQuickPane());
    m_stack->addPage(buildExpertPane());
    mainLayout->addWidget(m_stack, 1);

    auto *footer = new QWidget(mainCard);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(10);
    m_statusLabel = makeLabel(QStringLiteral("选择服务器后可在此修改配置"), QStringLiteral("hint"), footer);
    m_resetButton = new GradientButton(QStringLiteral("还原未保存的修改"), footer);
    m_resetButton->setStyle(GradientButton::Outline);
    m_resetButton->setCompact(true);
    m_saveButton = new GradientButton(QStringLiteral("保存并备份"), footer);
    m_saveButton->setGlyph(QStringLiteral("✔"));
    m_saveButton->setCompact(true);
    footerLayout->addWidget(m_statusLabel, 1);
    footerLayout->addWidget(m_resetButton);
    footerLayout->addWidget(m_saveButton);
    mainLayout->addWidget(footer);

    columns->addWidget(mainCard, 5);
    body()->addLayout(columns, 1);

    connect(m_modeSwitch, &SegmentedControl::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index, true, index > 0 ? 1 : -1);
        if (index == 1 && m_expertEdit->toPlainText().trimmed().isEmpty())
            loadServer(m_selector->currentServerId());
    });
    connect(m_selector, &ServerSelector::serverChanged, this, [this](const QString &id) {
        if (id.isEmpty())
            return;
        AppContext::instance()->setSelectedServerId(id);
        loadServer(id);
    });
    connect(m_saveButton, &QPushButton::clicked, this, [this]() {
        if (m_modeSwitch->currentIndex() == 0)
            saveQuick();
        else
            saveExpert();
    });
    connect(m_resetButton, &QPushButton::clicked, this, [this]() { loadServer(m_serverId); });

    const QString current = AppContext::instance()->selectedServerId();
    if (!current.isEmpty())
        loadServer(current);
    loadSchema();
}

QWidget *ConfigPage::buildQuickPane()
{
    m_quickContainer = new QWidget();
    m_quickLayout = new QVBoxLayout(m_quickContainer);
    m_quickLayout->setContentsMargins(2, 2, 2, 2);
    m_quickLayout->setSpacing(14);

    auto *hint = makeLabel(QStringLiteral("正在载入配置项…"), QStringLiteral("hint"), m_quickContainer);
    m_quickLayout->addWidget(hint);
    m_quickLayout->addStretch(1);

    m_quickArea = new QScrollArea();
    m_quickArea->setWidgetResizable(true);
    m_quickArea->setWidget(m_quickContainer);
    m_quickArea->setFrameShape(QFrame::NoFrame);
    return m_quickArea;
}

QWidget *ConfigPage::buildExpertPane()
{
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout();
    header->setSpacing(8);
    m_pathLabel = makeLabel(QStringLiteral("server.properties"), QStringLiteral("caption"), container);
    auto *validate = new GradientButton(QStringLiteral("检查语法"), container);
    validate->setStyle(GradientButton::Outline);
    validate->setCompact(true);
    header->addWidget(m_pathLabel, 1);
    header->addWidget(validate);
    layout->addLayout(header);

    m_expertEdit = new QPlainTextEdit(container);
    m_expertEdit->setProperty("mono", "true");
    m_expertEdit->setPlaceholderText(QStringLiteral("直接编辑 server.properties 原文…"));
    layout->addWidget(m_expertEdit, 1);

    m_issuesLabel = makeLabel(QString(), QStringLiteral("hint"), container);
    layout->addWidget(m_issuesLabel);

    connect(validate, &QPushButton::clicked, this, &ConfigPage::validateExpert);
    return container;
}

QWidget *ConfigPage::buildHistoryCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 18, 12);

    auto *headerRow = new QWidget(card);
    auto *headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    headerLayout->addWidget(makeSectionHeader(QStringLiteral("配置备份"),
                                              QStringLiteral("每次保存前自动留档"), nullptr, headerRow),
                            1);
    auto *refresh = new GradientButton(QStringLiteral("刷新"), headerRow);
    refresh->setStyle(GradientButton::Outline);
    refresh->setCompact(true);
    headerLayout->addWidget(refresh, 0, Qt::AlignVCenter);
    layout->addWidget(headerRow);

    m_historyList = new QListWidget(card);
    m_historyList->setFrameShape(QFrame::NoFrame);
    m_historyList->setSpacing(6);
    m_historyList->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; }"));
    layout->addWidget(m_historyList, 1);

    m_historyHint = makeLabel(QStringLiteral("还没有备份记录，保存一次配置后即可回滚。"),
                              QStringLiteral("hint"), card);
    layout->addWidget(m_historyHint);

    connect(refresh, &QPushButton::clicked, this, [this]() { loadHistory(m_serverId); });
    connect(m_historyList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            rollbackTo(id);
    });
    return card;
}

void ConfigPage::loadSchema()
{
    AppContext::instance()->backend()->request({QStringLiteral("config"), QStringLiteral("schema")}, this,
                                               [this](const Reply &reply) {
                                                   if (!reply.ok)
                                                       return;
                                                   m_schemaValues = reply.data;
                                                   buildFields(
                                                       reply.data.value(QStringLiteral("fields")).toArray());
                                               });
}

void ConfigPage::buildFields(const QJsonArray &fields)
{
    if (!m_quickLayout || fields.isEmpty())
        return;

    // clear previous content
    while (QLayoutItem *item = m_quickLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_editors.clear();
    m_defaults.clear();
    m_advancedKeys.clear();

    auto *topRow = new QWidget(m_quickContainer);
    auto *topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(10);
    auto *hint = makeLabel(
        QStringLiteral("这里改动的是 server.properties；带“进阶”标记的参数需要重载或重启服务器才会生效。"),
        QStringLiteral("hint"), topRow);
    m_advancedSwitch = new ToggleSwitch(topRow);
    m_advancedSwitch->setOnText(QStringLiteral("显示进阶项"), QStringLiteral("显示进阶项"));
    topLayout->addWidget(hint, 1);
    topLayout->addWidget(m_advancedSwitch);
    m_quickLayout->addWidget(topRow);

    QString currentGroup;
    QVBoxLayout *groupLayout = nullptr;
    CardFrame *groupCard = nullptr;

    for (const QJsonValue &value : fields) {
        const QJsonObject field = value.toObject();
        const QString key = field.value(QStringLiteral("key")).toString();
        const QString group = field.value(QStringLiteral("group")).toString();
        const QString label = field.value(QStringLiteral("label")).toString(key);
        const QString type = field.value(QStringLiteral("type")).toString(QStringLiteral("text"));
        const QString hintText = field.value(QStringLiteral("hint")).toString();
        const QString unit = field.value(QStringLiteral("unit")).toString();
        const bool advanced = field.value(QStringLiteral("advanced")).toBool();
        m_defaults.insert(key, field.value(QStringLiteral("default")).toString());

        if (group != currentGroup || !groupLayout) {
            currentGroup = group;
            groupCard = new CardFrame(m_quickContainer);
            groupCard->setVariant(CardFrame::Alt);
            groupLayout = new QVBoxLayout(groupCard);
            groupLayout->setContentsMargins(16, 14, 16, 14);
            groupLayout->setSpacing(10);
            groupLayout->addWidget(makeLabel(group, QStringLiteral("subtitle"), groupCard));
            m_quickLayout->addWidget(groupCard);
        }

        auto *row = new QWidget(groupCard);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(12);

        auto *textColumn = new QVBoxLayout();
        textColumn->setSpacing(2);
        QString caption = label;
        if (advanced)
            caption += QStringLiteral("（进阶）");
        if (!unit.isEmpty())
            caption += QStringLiteral(" · %1").arg(unit);
        textColumn->addWidget(makeLabel(caption, QStringLiteral("caption"), row));
        if (!hintText.isEmpty())
            textColumn->addWidget(makeLabel(hintText, QStringLiteral("hint"), row));
        rowLayout->addLayout(textColumn, 1);

        QWidget *editor = nullptr;
        if (type == QLatin1String("bool")) {
            auto *toggle = new ToggleSwitch(row);
            toggle->setOnText(QStringLiteral("开启"), QStringLiteral("关闭"));
            editor = toggle;
        } else if (type == QLatin1String("int")) {
            auto *spin = new QSpinBox(row);
            spin->setRange(field.value(QStringLiteral("min")).toInt(-1000000),
                           field.value(QStringLiteral("max")).toInt(1000000));
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            spin->setMaximumWidth(160);
            editor = spin;
        } else if (type == QLatin1String("choice")) {
            auto *combo = new QComboBox(row);
            const QJsonArray options = field.value(QStringLiteral("options")).toArray();
            for (const QJsonValue &option : options)
                combo->addItem(option.toString());
            combo->setMinimumWidth(180);
            editor = combo;
        } else {
            auto *edit = new QLineEdit(row);
            edit->setMinimumWidth(220);
            editor = edit;
        }
        editor->setProperty("configKey", key);
        editor->setToolTip(key);
        if (advanced) {
            m_advancedKeys.append(key);
            row->setProperty("advanced", true);
        }
        rowLayout->addWidget(editor, 0, Qt::AlignVCenter);
        groupLayout->addWidget(row);
        m_editors.insert(key, editor);
    }

    m_quickLayout->addStretch(1);

    connect(m_advancedSwitch, &ToggleSwitch::toggled, this, [this](bool checked) {
        for (const QString &key : m_advancedKeys) {
            if (QWidget *editor = m_editors.value(key)) {
                if (QWidget *row = editor->parentWidget())
                    row->setVisible(checked);
            }
        }
    });
    for (const QString &key : m_advancedKeys) {
        if (QWidget *editor = m_editors.value(key)) {
            if (QWidget *row = editor->parentWidget())
                row->setVisible(false);
        }
    }
}

void ConfigPage::loadServer(const QString &serverId)
{
    if (serverId.isEmpty())
        return;
    m_serverId = serverId;
    setStatus(QStringLiteral("正在读取配置…"));

    AppContext::instance()->backend()->request(
        {QStringLiteral("config"), QStringLiteral("read"), QStringLiteral("--id"), serverId}, this,
        [this](const Reply &reply) {
            if (!reply.ok) {
                setStatus(QStringLiteral("读取失败：%1").arg(reply.errorText()), true);
                return;
            }
            m_originalValues.clear();
            const QJsonObject values = reply.data.value(QStringLiteral("values")).toObject();
            for (auto it = values.constBegin(); it != values.constEnd(); ++it)
                m_originalValues.insert(it.key(), jsonToText(it.value()));
            applyValues(m_originalValues);

            const QString raw = reply.data.value(QStringLiteral("raw")).toString();
            m_expertEdit->setPlainText(raw);
            m_pathLabel->setText(reply.data.value(QStringLiteral("path")).toString());
            setStatus(QStringLiteral("已载入 %1 项配置 · %2")
                          .arg(values.size())
                          .arg(QStringLiteral("与磁盘内容一致")));
        });
    loadHistory(serverId);
}

void ConfigPage::applyValues(const QMap<QString, QString> &values)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        setEditorValue(it.key(), it.value());
}

QString ConfigPage::editorValue(const QString &key) const
{
    QWidget *editor = m_editors.value(key);
    if (!editor)
        return m_originalValues.value(key);
    if (auto *toggle = qobject_cast<ToggleSwitch *>(editor))
        return toggle->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
    if (auto *spin = qobject_cast<QSpinBox *>(editor))
        return QString::number(spin->value());
    if (auto *combo = qobject_cast<QComboBox *>(editor))
        return combo->currentText();
    if (auto *edit = qobject_cast<QLineEdit *>(editor))
        return edit->text();
    return QString();
}

void ConfigPage::setEditorValue(const QString &key, const QString &value)
{
    QWidget *editor = m_editors.value(key);
    if (!editor) {
        m_originalValues.insert(key, value);
        return;
    }
    if (auto *toggle = qobject_cast<ToggleSwitch *>(editor)) {
        toggle->setChecked(value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
        return;
    }
    if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
        spin->setValue(value.toInt());
        return;
    }
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        const int index = combo->findText(value);
        if (index >= 0)
            combo->setCurrentIndex(index);
        else if (!value.isEmpty())
            combo->setCurrentText(value);
        return;
    }
    if (auto *edit = qobject_cast<QLineEdit *>(editor))
        edit->setText(value);
}

void ConfigPage::saveQuick()
{
    if (m_serverId.isEmpty() || m_busy)
        return;
    QJsonObject payload;
    int changed = 0;
    for (auto it = m_editors.constBegin(); it != m_editors.constEnd(); ++it) {
        const QString value = editorValue(it.key());
        payload.insert(it.key(), value);
        if (m_originalValues.value(it.key()) != value)
            ++changed;
    }
    if (changed == 0) {
        setStatus(QStringLiteral("没有检测到改动"));
        AppContext::instance()->logActivity(QStringLiteral("配置未变化，跳过保存"), QStringLiteral("info"));
        return;
    }
    setBusy(true);
    const QString json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    AppContext::instance()->backend()->request(
        {QStringLiteral("config"), QStringLiteral("apply"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--values"), json, QStringLiteral("--note"),
         QStringLiteral("快捷配置修改 %1 项").arg(changed)},
        this, [this, changed](const Reply &reply) {
            setBusy(false);
            if (!reply.ok) {
                setStatus(QStringLiteral("保存失败：%1").arg(reply.errorText()), true);
                AppContext::instance()->logActivity(QStringLiteral("配置保存失败：%1").arg(reply.errorText()),
                                                    QStringLiteral("error"));
                return;
            }
            setStatus(QStringLiteral("已保存 %1 项改动，旧版本已备份").arg(changed));
            AppContext::instance()->logActivity(
                QStringLiteral("已更新 %1 的配置（%2 项）").arg(m_serverId).arg(changed),
                QStringLiteral("success"));
            loadServer(m_serverId);
        });
}

void ConfigPage::saveExpert()
{
    if (m_serverId.isEmpty() || m_busy)
        return;
    const QString text = m_expertEdit->toPlainText();
    if (text.trimmed().isEmpty()) {
        setStatus(QStringLiteral("内容为空，已取消保存"), true);
        return;
    }
    setBusy(true);
    AppContext::instance()->backend()->request(
        {QStringLiteral("config"), QStringLiteral("raw"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--text"), text, QStringLiteral("--note"), QStringLiteral("专家模式编辑")},
        this, [this](const Reply &reply) {
            setBusy(false);
            if (!reply.ok) {
                setStatus(QStringLiteral("保存失败：%1").arg(reply.errorText()), true);
                return;
            }
            const int count = reply.data.value(QStringLiteral("count")).toInt();
            setStatus(QStringLiteral("专家模式内容已保存（%1 处变更），旧版本已备份").arg(count));
            AppContext::instance()->logActivity(
                QStringLiteral("专家模式保存了 %1 的配置").arg(m_serverId), QStringLiteral("success"));
            loadServer(m_serverId);
        });
}

void ConfigPage::validateExpert()
{
    if (m_serverId.isEmpty())
        return;
    AppContext::instance()->backend()->request(
        {QStringLiteral("config"), QStringLiteral("validate"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--text"), m_expertEdit->toPlainText()},
        this, [this](const Reply &reply) {
            if (!reply.ok) {
                m_issuesLabel->setText(QStringLiteral("检查失败：%1").arg(reply.errorText()));
                return;
            }
            const QJsonArray issues = reply.data.value(QStringLiteral("issues")).toArray();
            if (issues.isEmpty()) {
                m_issuesLabel->setText(QStringLiteral("✔ 语法检查通过，没有发现问题"));
                m_issuesLabel->setStyleSheet(
                    QStringLiteral("color: %1;").arg(ThemeManager::instance()->palette().success.name()));
                return;
            }
            QStringList list;
            for (const QJsonValue &value : issues)
                list << QStringLiteral("• ") + value.toString();
            m_issuesLabel->setText(list.join(QLatin1Char('\n')));
            m_issuesLabel->setStyleSheet(
                QStringLiteral("color: %1;").arg(ThemeManager::instance()->palette().warning.name()));
        });
}

void ConfigPage::loadHistory(const QString &serverId)
{
    if (serverId.isEmpty() || !m_historyList)
        return;
    AppContext::instance()->backend()->request(
        {QStringLiteral("config"), QStringLiteral("backups"), QStringLiteral("--id"), serverId}, this,
        [this](const Reply &reply) {
            m_historyList->clear();
            if (!reply.ok)
                return;
            const QJsonArray backups = reply.data.value(QStringLiteral("backups")).toArray();
            for (const QJsonValue &value : backups) {
                const QJsonObject backup = value.toObject();
                const QString id = backup.value(QStringLiteral("id")).toString();
                const QString note = backup.value(QStringLiteral("note")).toString();
                const int count = backup.value(QStringLiteral("changeCount")).toInt();
                const QString time = backup.value(QStringLiteral("createdAt")).toString();

                auto *item = new QListWidgetItem(m_historyList);
                item->setData(Qt::UserRole, id);
                auto *widget = new QWidget(m_historyList);
                auto *layout = new QVBoxLayout(widget);
                layout->setContentsMargins(8, 6, 8, 6);
                layout->setSpacing(4);

                auto *topRow = new QHBoxLayout();
                topRow->setSpacing(8);
                auto *timeLabel = makeLabel(time.left(19).replace(QLatin1Char('T'), QLatin1Char(' ')),
                                            QStringLiteral("caption"), widget);
                auto *chip = new Chip(QStringLiteral("%1 项改动").arg(count), widget);
                chip->setTone(Chip::Pink);
                chip->setCompact(true);
                topRow->addWidget(timeLabel, 1);
                topRow->addWidget(chip);
                layout->addLayout(topRow);

                if (!note.isEmpty())
                    layout->addWidget(makeLabel(note, QStringLiteral("hint"), widget));

                auto *actions = new QHBoxLayout();
                actions->setSpacing(6);
                auto *rollback = new GradientButton(QStringLiteral("回滚到此版本"), widget);
                rollback->setStyle(GradientButton::Outline);
                rollback->setCompact(true);
                actions->addWidget(rollback);
                actions->addStretch(1);
                layout->addLayout(actions);
                connect(rollback, &QPushButton::clicked, this, [this, id]() { rollbackTo(id); });

                item->setSizeHint(widget->sizeHint());
                m_historyList->setItemWidget(item, widget);
            }
            if (m_historyHint)
                m_historyHint->setVisible(backups.isEmpty());
        });
}

void ConfigPage::rollbackTo(const QString &backupId)
{
    if (m_serverId.isEmpty() || backupId.isEmpty() || m_busy)
        return;
    setBusy(true);
    setStatus(QStringLiteral("正在回滚…"));
    AppContext::instance()->backend()->request(
        {QStringLiteral("config"), QStringLiteral("rollback"), QStringLiteral("--id"), m_serverId,
         QStringLiteral("--backup"), backupId},
        this, [this, backupId](const Reply &reply) {
            setBusy(false);
            if (!reply.ok) {
                setStatus(QStringLiteral("回滚失败：%1").arg(reply.errorText()), true);
                return;
            }
            setStatus(QStringLiteral("已回滚到 %1").arg(backupId));
            AppContext::instance()->logActivity(
                QStringLiteral("配置已回滚（%1）").arg(backupId), QStringLiteral("success"));
            loadServer(m_serverId);
        });
}

void ConfigPage::setStatus(const QString &message, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(message);
    const Palette &palette = ThemeManager::instance()->palette();
    m_statusLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(error ? palette.danger.name() : palette.textMuted.name()));
}

void ConfigPage::setBusy(bool busy)
{
    m_busy = busy;
    m_saveButton->setEnabled(!busy);
    m_saveButton->setText(busy ? QStringLiteral("处理中…") : QStringLiteral("保存并备份"));
}

void ConfigPage::onActivated()
{
    const QString id = AppContext::instance()->selectedServerId();
    const bool changed = id != m_serverId;
    if (!id.isEmpty()
        && (changed || AppContext::instance()->throttle(QStringLiteral("config-read"), 1500)))
        loadServer(id);
}

void ConfigPage::onServerSelectionChanged(const QString &serverId)
{
    if (serverId.isEmpty() || serverId == m_serverId)
        return;
    m_selector->selectServer(serverId);
    loadServer(serverId);
}

} // namespace mcsm
