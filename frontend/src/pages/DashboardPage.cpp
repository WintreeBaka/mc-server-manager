#include "pages/DashboardPage.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "app/Easing.h"
#include "widgets/Chip.h"

namespace mcsm {

DashboardPage::DashboardPage(QWidget *parent)
    : PageBase(QStringLiteral("总览"),
               QStringLiteral("Docker 化的运行环境、自动配置与定时备份都在这里统一管理"),
               parent)
{
    body()->addWidget(buildHero());
    body()->addWidget(buildStats());

    auto *columns = new QHBoxLayout();
    columns->setSpacing(16);
    columns->addWidget(buildActivityCard(), 3);
    columns->addWidget(buildEnvironmentCard(), 2);
    body()->addLayout(columns, 1);

    connect(AppContext::instance(), &AppContext::activityAdded, this, &DashboardPage::onActivity);
    connect(AppContext::instance()->servers(), &ServerModel::changed, this, &DashboardPage::onServersChanged);
    onServersChanged();
}

QWidget *DashboardPage::buildHero()
{
    auto *card = new CardFrame(this);
    card->setVariant(CardFrame::Gradient);
    card->setRadius(22);
    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(26, 24, 26, 24);
    layout->setSpacing(18);

    auto *column = new QVBoxLayout();
    column->setSpacing(8);
    auto *title = makeLabel(QStringLiteral("一键配置属于你的服务器"), QStringLiteral("title"), card);
    auto *caption = makeLabel(
        QStringLiteral("选择版本即可自动下载对应服务端、拉取匹配的 JDK 容器镜像并生成配置；"
                       "也可以改用手动模式导入已有的服务端文件。"),
        QStringLiteral("caption"), card);
    caption->setMaximumWidth(620);
    column->addWidget(title);
    column->addWidget(caption);
    column->addSpacing(4);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(10);
    auto *create = new GradientButton(QStringLiteral("新建服务器"), card);
    create->setGlyph(QStringLiteral("＋"));
    auto *check = new GradientButton(QStringLiteral("重新检测环境"), card);
    check->setStyle(GradientButton::Outline);
    buttons->addWidget(create);
    buttons->addWidget(check);
    buttons->addStretch(1);
    column->addLayout(buttons);

    layout->addLayout(column, 1);

    auto *badge = new Chip(QStringLiteral("马卡龙主题 · 平滑过渡动画"), card);
    badge->setTone(Chip::Gradient);
    layout->addWidget(badge, 0, Qt::AlignTop);

    connect(create, &QPushButton::clicked, this, &DashboardPage::createServerRequested);
    connect(check, &QPushButton::clicked, this, &DashboardPage::refresh);
    return card;
}

QWidget *DashboardPage::buildStats()
{
    auto *container = new QWidget(this);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    const Palette &palette = ThemeManager::instance()->palette();
    m_totalCard = new StatCard(container);
    m_totalCard->setData(QStringLiteral("服务器总数"), QStringLiteral("0"), QStringLiteral("已纳管"),
                         palette.blue, QStringLiteral("▤"));
    m_runningCard = new StatCard(container);
    m_runningCard->setData(QStringLiteral("运行中"), QStringLiteral("0"), QStringLiteral("容器存活"),
                           palette.success, QStringLiteral("▶"));
    m_errorCard = new StatCard(container);
    m_errorCard->setData(QStringLiteral("需要处理"), QStringLiteral("0"), QStringLiteral("启动异常"),
                         palette.danger, QStringLiteral("!"));
    m_backupCard = new StatCard(container);
    m_backupCard->setData(QStringLiteral("最近备份"), QStringLiteral("--"), QStringLiteral("自动 / 手动"),
                          palette.pink, QStringLiteral("◷"));

    layout->addWidget(m_totalCard);
    layout->addWidget(m_runningCard);
    layout->addWidget(m_errorCard);
    layout->addWidget(m_backupCard);
    return container;
}

QWidget *DashboardPage::buildActivityCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 12);
    layout->addWidget(makeSectionHeader(QStringLiteral("最近活动"),
                                        QStringLiteral("启动、配置与备份的记录"), nullptr, card));

    m_activityList = new QListWidget(card);
    m_activityList->setFrameShape(QFrame::NoFrame);
    m_activityList->setSelectionMode(QAbstractItemView::NoSelection);
    m_activityList->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; }"));
    layout->addWidget(m_activityList, 1);

    m_emptyHint = makeLabel(QStringLiteral("还没有操作记录，先创建一台服务器吧。"),
                            QStringLiteral("hint"), card);
    layout->addWidget(m_emptyHint);
    return card;
}

QWidget *DashboardPage::buildEnvironmentCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 14);
    layout->addWidget(makeSectionHeader(QStringLiteral("运行环境"),
                                        QStringLiteral("后端、Docker 与 JDK 状态"), nullptr, card));

    auto makeRow = [card](const QString &label, const QString &glyph, QLabel **value, QLabel **hint) {
        auto *row = new QWidget(card);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(10);

        auto *icon = new QLabel(glyph, row);
        icon->setFixedSize(30, 30);
        icon->setAlignment(Qt::AlignCenter);
        const Palette &palette = ThemeManager::instance()->palette();
        icon->setStyleSheet(QStringLiteral("border-radius: 10px; background: %1; color: %2;")
                                .arg(palette.surfaceAlt.name(), palette.text.name()));

        auto *column = new QVBoxLayout();
        column->setSpacing(0);
        auto *title = makeLabel(label, QStringLiteral("caption"), row);
        *hint = makeLabel(QStringLiteral("--"), QStringLiteral("hint"), row);
        *value = makeLabel(QStringLiteral("检测中"), QStringLiteral("subtitle"), row);
        column->addWidget(title);
        column->addWidget(*value);
        column->addWidget(*hint);

        rowLayout->addWidget(icon, 0, Qt::AlignTop);
        rowLayout->addLayout(column, 1);
        return row;
    };

    layout->addWidget(makeRow(QStringLiteral("Docker 守护进程"), QStringLiteral("◈"),
                              &m_dockerValue, &m_dockerHint));
    layout->addWidget(makeRow(QStringLiteral("容器化 JDK"), QStringLiteral("☕"),
                              &m_javaValue, &m_javaHint));
    layout->addWidget(makeRow(QStringLiteral("数据目录"), QStringLiteral("▣"),
                              &m_homeValue, &m_homeHint));

    m_diskBar = new QProgressBar(card);
    m_diskBar->setRange(0, 100);
    m_diskBar->setValue(0);
    layout->addWidget(m_diskBar);
    layout->addStretch(1);
    return card;
}

void DashboardPage::onActivity(const QString &message, const QString &level)
{
    auto *item = new QListWidgetItem(message);
    const Palette &palette = ThemeManager::instance()->palette();
    if (level == QLatin1String("error"))
        item->setForeground(palette.danger);
    else if (level == QLatin1String("warning"))
        item->setForeground(palette.warning);
    else if (level == QLatin1String("success"))
        item->setForeground(palette.success);
    else
        item->setForeground(palette.textMuted);
    m_activityList->insertItem(0, item);
    while (m_activityList->count() > 40)
        delete m_activityList->takeItem(m_activityList->count() - 1);
    m_emptyHint->setVisible(m_activityList->count() == 0);
}

void DashboardPage::onServersChanged()
{
    ServerModel *model = AppContext::instance()->servers();
    m_totalCard->setValue(QString::number(model->rowCount()));
    m_runningCard->setValue(QString::number(model->runningCount()));
    m_errorCard->setValue(QString::number(model->errorCount()));

    QDateTime latest;
    QString latestName;
    for (const ServerInfo &info : model->servers()) {
        if (info.lastBackupAt.isValid() && (!latest.isValid() || info.lastBackupAt > latest)) {
            latest = info.lastBackupAt;
            latestName = info.name;
        }
    }
    if (latest.isValid()) {
        const qint64 minutes = latest.secsTo(QDateTime::currentDateTime()) / 60;
        const QString ago = minutes < 1 ? QStringLiteral("刚刚")
                                        : (minutes < 60 ? QStringLiteral("%1 分钟前").arg(minutes)
                                                        : QStringLiteral("%1 小时前").arg(minutes / 60));
        m_backupCard->setValue(ago);
    } else {
        m_backupCard->setValue(QStringLiteral("--"));
    }
}

void DashboardPage::onActivated()
{
    refresh();
}

void DashboardPage::refresh()
{
    AppContext *context = AppContext::instance();
    context->refreshServers([this, context](bool ok, const QString &message) {
        if (!ok)
            context->logActivity(QStringLiteral("刷新服务器列表失败：%1").arg(message),
                                 QStringLiteral("error"));
    });

    // keep the environment card in sync with the periodic environment refresh
    connect(context, &AppContext::environmentChanged, this,
            [this](const QJsonObject &doctor) { applyDoctor(doctor, QStringList()); },
            Qt::UniqueConnection);
    context->refreshEnvironment();
    if (!context->environment().isEmpty())
        applyDoctor(context->environment(), QStringList());
}

void DashboardPage::applyDoctor(const QJsonObject &data, const QStringList &warnings)
{
    const Palette &palette = ThemeManager::instance()->palette();
    const QJsonObject docker = data.value(QStringLiteral("docker")).toObject();
    const bool cliFound = docker.value(QStringLiteral("cliFound")).toBool();
    const bool running = docker.value(QStringLiteral("daemonRunning")).toBool();
    const QString version = docker.value(QStringLiteral("version")).toString();

    if (cliFound && running) {
        m_dockerValue->setText(QStringLiteral("运行中"));
        m_dockerValue->setStyleSheet(QStringLiteral("color: %1;").arg(palette.success.name()));
        m_dockerHint->setText(version.isEmpty() ? QStringLiteral("Docker 引擎可用")
                                                : QStringLiteral("引擎版本 %1").arg(version));
    } else {
        m_dockerValue->setText(cliFound ? QStringLiteral("未运行") : QStringLiteral("未安装"));
        m_dockerValue->setStyleSheet(QStringLiteral("color: %1;").arg(palette.danger.name()));
        const QString rawError = docker.value(QStringLiteral("error")).toString();
        m_dockerHint->setText(rawError.isEmpty()
                                  ? QStringLiteral("请启动 Docker Desktop")
                                  : rawError.left(120) + QStringLiteral("..."));
        m_dockerHint->setToolTip(rawError);
    }

    const QJsonObject java = data.value(QStringLiteral("java")).toObject();
    const QJsonArray images = java.value(QStringLiteral("images")).toArray();
    if (images.isEmpty()) {
        m_javaValue->setText(QStringLiteral("尚未准备"));
        m_javaHint->setText(QStringLiteral("创建服务器时会自动拉取匹配的 JDK 镜像"));
        m_javaValue->setStyleSheet(QStringLiteral("color: %1;").arg(palette.warning.name()));
    } else {
        QStringList labels;
        for (const QJsonValue &value : images)
            labels << value.toString().section(QLatin1Char(':'), 1);
        m_javaValue->setText(QStringLiteral("%1 个镜像").arg(images.size()));
        m_javaValue->setStyleSheet(QStringLiteral("color: %1;").arg(palette.success.name()));
        m_javaHint->setText(labels.join(QStringLiteral(" · ")));
    }

    const QJsonObject host = data.value(QStringLiteral("host")).toObject();
    const QString home = host.value(QStringLiteral("dataRoot")).toString();
    if (!home.isEmpty()) {
        m_homeValue->setText(QStringLiteral("已就绪"));
        m_homeHint->setText(home);
        m_homeHint->setToolTip(home);
        m_homeValue->setStyleSheet(QStringLiteral("color: %1;").arg(palette.text.name()));
    }

    if (!host.value(QStringLiteral("backend")).toString().isEmpty()) {
        setSubtitle(QStringLiteral("后端 mcsm-cli 已就绪 · 环境自检完成（Qt %1）")
                        .arg(host.value(QStringLiteral("qtVersion")).toString()));
    }

    if (m_diskBar)
        m_diskBar->setValue(running ? 100 : 12);

    for (const QString &warning : warnings)
        AppContext::instance()->logActivity(warning, QStringLiteral("warning"));
}

QString DashboardPage::healthText() const
{
    return AppContext::instance()->backend()->isConfigured() ? QStringLiteral("后端就绪")
                                                            : QStringLiteral("后端缺失");
}

} // namespace mcsm
