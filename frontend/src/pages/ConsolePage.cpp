#include "pages/ConsolePage.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "widgets/Chip.h"
#include "widgets/LogView.h"

namespace mcsm {

ConsolePage::ConsolePage(QWidget *parent)
    : PageBase(QStringLiteral("控制台"),
               QStringLiteral("实时查看容器日志，并通过 RCON 发送服务器指令"), parent)
{
    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(8);

    m_stateChip = new Chip(QStringLiteral("--"), toolbar);
    m_selector = new ServerSelector(toolbar);
    m_selector->setMinimumWidth(260);
    auto *reload = new GradientButton(QStringLiteral("重新载入"), toolbar);
    reload->setStyle(GradientButton::Outline);
    reload->setCompact(true);

    toolbarLayout->addWidget(m_stateChip);
    toolbarLayout->addWidget(m_selector);
    toolbarLayout->addWidget(reload);
    setHeaderTrailing(toolbar);

    body()->addWidget(buildConsoleCard(), 1);

    connect(m_selector, &ServerSelector::serverChanged, this, [this](const QString &id) {
        if (id.isEmpty())
            return;
        AppContext::instance()->setSelectedServerId(id);
        m_streamServerId.clear();
        loadHistory(id);
        startStream(id, 300);
        emit serverSelected(id);
    });
    connect(reload, &QPushButton::clicked, this, [this]() {
        const QString id = m_selector->currentServerId();
        if (id.isEmpty())
            return;
        m_streamServerId.clear();
        loadHistory(id);
        startStream(id, 400);
    });
}

ConsolePage::~ConsolePage()
{
    stopStream();
}

QWidget *ConsolePage::buildConsoleCard()
{
    auto *card = new CardFrame(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(card);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 14, 18, 6);
    headerLayout->setSpacing(8);
    m_statusLabel = makeLabel(QStringLiteral("等待选择服务器"), QStringLiteral("caption"), header);
    headerLayout->addWidget(m_statusLabel, 1);

    m_filter = new QLineEdit(header);
    m_filter->setPlaceholderText(QStringLiteral("过滤关键字…"));
    m_filter->setMaximumWidth(200);
    m_followSwitch = new ToggleSwitch(header);
    m_followSwitch->setChecked(true);
    m_followSwitch->setOnText(QStringLiteral("跟随输出"), QStringLiteral("跟随输出"));

    auto *copy = new GradientButton(QStringLiteral("复制"), header);
    copy->setStyle(GradientButton::Outline);
    copy->setCompact(true);
    auto *clear = new GradientButton(QStringLiteral("清屏"), header);
    clear->setStyle(GradientButton::Outline);
    clear->setCompact(true);

    headerLayout->addWidget(m_filter);
    headerLayout->addWidget(copy);
    headerLayout->addWidget(clear);
    headerLayout->addWidget(m_followSwitch);
    layout->addWidget(header);

    m_log = new LogView(card);
    m_log->setMinimumHeight(320);
    layout->addWidget(m_log, 1);

    auto *commandBar = new QWidget(card);
    auto *commandLayout = new QHBoxLayout(commandBar);
    commandLayout->setContentsMargins(18, 12, 18, 16);
    commandLayout->setSpacing(8);
    m_command = new QLineEdit(commandBar);
    m_command->setPlaceholderText(QStringLiteral("输入服务器指令，例如 say 大家好 / list / whitelist add Steve"));
    auto *send = new GradientButton(QStringLiteral("发送"), commandBar);
    send->setGlyph(QStringLiteral("➤"));
    send->setCompact(true);
    commandLayout->addWidget(m_command, 1);
    commandLayout->addWidget(send);

    const QStringList quick = {QStringLiteral("save-all flush"), QStringLiteral("list"),
                               QStringLiteral("whitelist on"), QStringLiteral("time set day"),
                               QStringLiteral("weather clear")};
    for (const QString &command : quick) {
        auto *button = new GradientButton(command, commandBar);
        button->setStyle(GradientButton::Outline);
        button->setCompact(true);
        commandLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, command]() { sendCommand(command); });
    }
    layout->addWidget(commandBar);

    connect(send, &QPushButton::clicked, this, [this]() { sendCommand(m_command->text()); });
    connect(m_command, &QLineEdit::returnPressed, this, [this]() { sendCommand(m_command->text()); });
    connect(clear, &QPushButton::clicked, this, [this]() { m_log->clearLog(); });
    connect(copy, &QPushButton::clicked, this, [this]() {
        if (auto *clipboard = QGuiApplication::clipboard())
            clipboard->setText(m_log->plainLog());
        AppContext::instance()->logActivity(QStringLiteral("已复制控制台日志"), QStringLiteral("info"));
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_log->setFilter(text.trimmed());
    });
    connect(m_followSwitch, &ToggleSwitch::toggled, this, [this](bool checked) {
        m_log->setFollowOutput(checked);
    });
    return card;
}

void ConsolePage::onActivated()
{
    const QString id = AppContext::instance()->selectedServerId();
    if (id.isEmpty())
        return;
    m_selector->selectServer(id);
    if (m_streamServerId != id) {
        loadHistory(id);
        startStream(id, 300);
    }
}

void ConsolePage::onServerSelectionChanged(const QString &serverId)
{
    if (serverId.isEmpty())
        return;
    m_selector->selectServer(serverId);
}

void ConsolePage::loadHistory(const QString &serverId)
{
    AppContext::instance()->backend()->request(
        {QStringLiteral("server"), QStringLiteral("logs"), QStringLiteral("--id"), serverId,
         QStringLiteral("--tail"), QStringLiteral("300")},
        this, [this, serverId](const Reply &reply) {
            if (!reply.ok) {
                m_statusLabel->setText(QStringLiteral("无法读取日志：%1").arg(reply.errorText()));
                return;
            }
            const QJsonArray lines = reply.data.value(QStringLiteral("lines")).toArray();
            QStringList list;
            for (const QJsonValue &value : lines)
                list << value.toString();
            m_log->setLogLines(list);
            m_statusLabel->setText(QStringLiteral("已载入 %1 行历史日志 · %2")
                                       .arg(list.size())
                                       .arg(serverId));
        });
}

void ConsolePage::startStream(const QString &serverId, int tail)
{
    stopStream();
    m_stream = AppContext::instance()->backend()->streamLogs(
        serverId, tail, this, [this](const QJsonObject &line) {
            const QString type = line.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("log")) {
                m_log->appendLine(line.value(QStringLiteral("line")).toString());
            } else if (type == QLatin1String("error")) {
                m_log->appendLine(QStringLiteral("[mcsm] 日志流错误：%1")
                                      .arg(line.value(QStringLiteral("message")).toString()));
            } else if (type == QLatin1String("end")) {
                m_log->appendLine(QStringLiteral("[mcsm] 日志流已结束"));
            }
        });
    if (m_stream)
        m_streamServerId = serverId;
}

void ConsolePage::stopStream()
{
    if (!m_stream)
        return;
    m_stream->kill();
    m_stream->waitForFinished(1500);
    m_stream->deleteLater();
    m_stream = nullptr;
    m_streamServerId.clear();
}

void ConsolePage::sendCommand(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty())
        return;
    const QString id = m_selector->currentServerId();
    if (id.isEmpty()) {
        AppContext::instance()->logActivity(QStringLiteral("请先选择服务器"), QStringLiteral("warning"));
        return;
    }
    m_command->clear();
    m_log->appendLine(QStringLiteral("> %1").arg(trimmed));

    AppContext::instance()->backend()->request(
        {QStringLiteral("server"), QStringLiteral("command"), QStringLiteral("--id"), id,
         QStringLiteral("--command"), trimmed},
        this, [this, trimmed](const Reply &reply) {
            if (!reply.ok) {
                m_log->appendLine(QStringLiteral("[mcsm] 指令失败：%1").arg(reply.errorText()));
                AppContext::instance()->logActivity(
                    QStringLiteral("指令 %1 发送失败").arg(trimmed), QStringLiteral("error"));
                return;
            }
            const QString response = reply.data.value(QStringLiteral("response")).toString().trimmed();
            if (!response.isEmpty())
                m_log->appendLine(response);
        });
}

} // namespace mcsm
