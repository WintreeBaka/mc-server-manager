#include "pages/PluginPageView.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "widgets/Chip.h"
#include "widgets/LogView.h"

namespace mcsm {
namespace {

void clearLayout(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        if (QLayout *child = item->layout())
            clearLayout(child);
        delete item;
    }
}

QString blockStyle(const QJsonObject &block, const QString &fallback)
{
    const QString style = block.value(QStringLiteral("style")).toString().toLower();
    if (style == QLatin1String("hint") || style == QLatin1String("caption")
        || style == QLatin1String("subtitle") || style == QLatin1String("title"))
        return style;
    return fallback;
}

} // namespace

PluginPageView::PluginPageView(const PluginPageInfo &info, QWidget *parent)
    : PageBase(info.title, info.subtitle.isEmpty()
                                ? QStringLiteral("由插件 %1 提供").arg(info.pluginName)
                                : info.subtitle,
               parent)
    , m_info(info)
{
    auto *badge = new Chip(QStringLiteral("%1 %2").arg(info.icon, info.pluginName), this);
    badge->setTone(Chip::Gradient);
    badge->setCompact(true);
    setHeaderTrailing(badge);

    m_container = new QWidget(this);
    m_layout = new QVBoxLayout(m_container);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(14);
    m_layout->addStretch(1);
    body()->addWidget(m_container, 1);
}

void PluginPageView::setBlocks(const QJsonArray &blocks)
{
    m_blocks = blocks;
    rebuild();
}

void PluginPageView::rebuild()
{
    if (!m_layout)
        return;
    clearLayout(m_layout);
    m_log = nullptr;

    if (m_blocks.isEmpty()) {
        auto *empty = new CardFrame(m_container);
        auto *layout = CardFrame::verticalLayout(empty, 20, 8);
        layout->addWidget(makeSectionHeader(QStringLiteral("插件暂未提供内容"), QString(),
                                           nullptr, empty));
        layout->addWidget(makeLabel(QStringLiteral("插件脚本可以用 mcsm.ui.setPageContent() 随时更新这个页面。"),
                                    QStringLiteral("hint"), empty));
        m_layout->addWidget(empty);
    }

    for (const QJsonValue &value : m_blocks) {
        QWidget *widget = buildBlock(value.toObject());
        if (widget)
            m_layout->addWidget(widget);
    }
    m_layout->addStretch(1);

    for (const QString &line : m_pendingLog)
        appendLog(line);
    m_pendingLog.clear();
}

QWidget *PluginPageView::buildBlock(const QJsonObject &block)
{
    const QString type = block.value(QStringLiteral("type")).toString().toLower();

    if (type == QLatin1String("divider")) {
        auto *divider = new QFrame(m_container);
        divider->setProperty("role", "divider");
        divider->setFixedHeight(1);
        return divider;
    }

    if (type == QLatin1String("heading")) {
        auto *label = makeLabel(block.value(QStringLiteral("text")).toString(),
                                QStringLiteral("subtitle"), m_container);
        label->setWordWrap(true);
        QFont font = label->font();
        font.setPointSizeF(font.pointSizeF() + 2.0);
        font.setBold(true);
        label->setFont(font);
        return label;
    }

    if (type == QLatin1String("text")) {
        auto *label = makeLabel(block.value(QStringLiteral("text")).toString(),
                                blockStyle(block, QStringLiteral("caption")), m_container);
        label->setWordWrap(true);
        return label;
    }

    if (type == QLatin1String("html")) {
        auto *label = new QLabel(block.value(QStringLiteral("html")).toString(), m_container);
        label->setTextFormat(Qt::RichText);
        label->setWordWrap(true);
        label->setOpenExternalLinks(true);
        label->setProperty("role", QStringLiteral("caption"));
        return label;
    }

    if (type == QLatin1String("keyvalue")) {
        auto *card = new CardFrame(m_container);
        auto *grid = new QGridLayout(card);
        grid->setContentsMargins(20, 16, 20, 16);
        grid->setHorizontalSpacing(24);
        grid->setVerticalSpacing(8);
        int row = 0;
        const QJsonArray items = block.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : items) {
            QString key;
            QString text;
            if (value.isArray()) {
                const QJsonArray pair = value.toArray();
                key = pair.at(0).toString();
                text = pair.size() > 1 ? pair.at(1).toString() : QString();
            } else {
                const QJsonObject entry = value.toObject();
                key = entry.value(QStringLiteral("key")).toString();
                text = entry.value(QStringLiteral("value")).toString();
            }
            grid->addWidget(makeLabel(key, QStringLiteral("caption"), card), row, 0);
            auto *valueLabel = makeLabel(text, QStringLiteral("subtitle"), card);
            valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            grid->addWidget(valueLabel, row, 1);
            ++row;
        }
        grid->setColumnStretch(1, 1);
        return card;
    }

    if (type == QLatin1String("table")) {
        auto *card = new CardFrame(m_container);
        auto *layout = CardFrame::verticalLayout(card, 18, 10);
        const QString title = block.value(QStringLiteral("title")).toString();
        if (!title.isEmpty())
            layout->addWidget(makeSectionHeader(title, QString(), nullptr, card));

        auto *table = new QTableWidget(card);
        const QJsonArray columns = block.value(QStringLiteral("columns")).toArray();
        const QJsonArray rows = block.value(QStringLiteral("rows")).toArray();
        table->setColumnCount(columns.size());
        table->setRowCount(rows.size());
        QStringList header;
        for (const QJsonValue &column : columns)
            header << column.toString();
        table->setHorizontalHeaderLabels(header);
        table->verticalHeader()->setVisible(false);
        table->setFrameShape(QFrame::NoFrame);
        table->setShowGrid(false);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setWordWrap(false);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setStyleSheet(QStringLiteral("QTableWidget { background: transparent; border: none; }"));
        for (int row = 0; row < rows.size(); ++row) {
            const QJsonArray cells = rows.at(row).toArray();
            for (int column = 0; column < columns.size(); ++column) {
                const QString text = column < cells.size() ? cells.at(column).toString() : QString();
                table->setItem(row, column, new QTableWidgetItem(text));
            }
        }
        const int height = qMin(60 + rows.size() * 30, 360);
        table->setMinimumHeight(height);
        layout->addWidget(table);
        return card;
    }

    if (type == QLatin1String("buttons")) {
        auto *row = new QWidget(m_container);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        const QJsonArray items = block.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            const QString label = item.value(QStringLiteral("label")).toString();
            if (label.isEmpty())
                continue;
            auto *button = new GradientButton(label, row);
            button->setStyle(GradientButton::Soft);
            button->setCompact(true);
            const QString glyph = item.value(QStringLiteral("icon")).toString();
            if (!glyph.isEmpty())
                button->setGlyph(glyph);
            const QString command = item.value(QStringLiteral("command")).toString();
            const QString url = item.value(QStringLiteral("url")).toString();
            const QString toast = item.value(QStringLiteral("toast")).toString();
            const QString openUrlField = item.value(QStringLiteral("openUrlFrom")).toString();
            const QString tone = item.value(QStringLiteral("level")).toString();
            if (tone == QLatin1String("danger"))
                button->setStyle(GradientButton::Danger);
            connect(button, &QPushButton::clicked, this, [this, command, url, toast, openUrlField]() {
                if (!url.isEmpty()) {
                    QDesktopServices::openUrl(QUrl(url));
                    return;
                }
                if (!command.isEmpty())
                    runCommand(command, toast, openUrlField);
            });
            layout->addWidget(button);
        }
        layout->addStretch(1);
        return row;
    }

    if (type == QLatin1String("log")) {
        auto *card = new CardFrame(m_container);
        auto *layout = CardFrame::verticalLayout(card, 16, 8);
        layout->addWidget(makeSectionHeader(
            block.value(QStringLiteral("title")).toString(QStringLiteral("输出")), QString(), nullptr, card));
        m_log = new LogView(card);
        m_log->setMinimumHeight(140);
        m_log->setFollowOutput(true);
        m_log->setLogLines(QStringList());
        layout->addWidget(m_log, 1);
        const QString text = block.value(QStringLiteral("text")).toString();
        if (!text.isEmpty())
            m_log->appendLines(text.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
        return card;
    }

    if (type == QLatin1String("progress")) {
        auto *row = new QWidget(m_container);
        auto *layout = new QVBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        const QString label = block.value(QStringLiteral("label")).toString();
        if (!label.isEmpty())
            layout->addWidget(makeLabel(label, QStringLiteral("caption"), row));
        auto *bar = new QProgressBar(row);
        bar->setRange(0, 100);
        bar->setValue(qBound(0, block.value(QStringLiteral("value")).toInt(), 100));
        bar->setTextVisible(false);
        layout->addWidget(bar);
        return row;
    }

    // unknown block: show the raw payload so plugin authors can debug quickly
    auto *label = makeLabel(QStringLiteral("[未知区块 %1] %2")
                                .arg(type, QString::fromUtf8(QJsonDocument(block).toJson(QJsonDocument::Compact))),
                            QStringLiteral("hint"), m_container);
    label->setWordWrap(true);
    return label;
}

void PluginPageView::runCommand(const QString &command, const QString &toast,
                                const QString &openUrlField)
{
    appendLog(QStringLiteral("> %1").arg(command));
    const QStringList arguments = QProcess::splitCommand(command);
    AppContext::instance()->backend()->request(
        arguments, this, [this, command, toast, openUrlField](const Reply &reply) {
            if (!reply.ok) {
                appendLog(QStringLiteral("! %1").arg(reply.errorText()));
                AppContext::instance()->logActivity(
                    QStringLiteral("插件命令失败：%1").arg(reply.errorText()), QStringLiteral("error"));
                return;
            }
            const QString summary = QJsonDocument(reply.data).toJson(QJsonDocument::Compact);
            appendLog(QStringLiteral("✔ %1").arg(summary.left(2000)));
            if (!toast.isEmpty())
                AppContext::instance()->logActivity(toast, QStringLiteral("info"));
            if (!openUrlField.isEmpty()) {
                const QString url = reply.data.value(openUrlField).toString();
                if (!url.isEmpty())
                    QDesktopServices::openUrl(QUrl(url));
            }
        });
}

void PluginPageView::appendLog(const QString &line)
{
    if (!m_log) {
        m_pendingLog << line;
        return;
    }
    m_log->appendLine(QStringLiteral("%1  %2")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), line));
}

} // namespace mcsm
