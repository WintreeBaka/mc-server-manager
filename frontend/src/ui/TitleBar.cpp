#include "ui/TitleBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QWindow>

#include "app/ThemeManager.h"
#include "ui/FramelessHelper.h"
#include "widgets/Common.h"

namespace mcsm {
namespace {

QLabel *makeChipLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("role", "chip");
    // decorative: let drags pass through to the title bar
    label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    return label;
}

} // namespace

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(56);
    setAttribute(Qt::WA_StyledBackground, false);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 8, 12, 8);
    layout->setSpacing(10);

    auto *identity = new QWidget(this);
    auto *identityLayout = new QHBoxLayout(identity);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(10);

    auto *logo = new QLabel(QStringLiteral("⛏"), identity);
    logo->setFixedSize(30, 30);
    logo->setAlignment(Qt::AlignCenter);
    logo->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto *textColumn = new QWidget(identity);
    auto *textLayout = new QVBoxLayout(textColumn);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);
    m_title = makeLabel(QStringLiteral("我的世界服务器管理器"), QStringLiteral("subtitle"), textColumn);
    m_subtitle = makeLabel(QStringLiteral("Docker 化运行环境 · 一键配置"), QStringLiteral("caption"),
                           textColumn);
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    textColumn->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    identity->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    textLayout->addWidget(m_title);
    textLayout->addWidget(m_subtitle);

    identityLayout->addWidget(logo);
    identityLayout->addWidget(textColumn);
    layout->addWidget(identity);
    layout->addSpacing(8);

    m_backendChip = makeChipLabel(QStringLiteral("后端检测中"), this);
    m_dockerChip = makeChipLabel(QStringLiteral("Docker 检测中"), this);
    layout->addWidget(m_backendChip);
    layout->addWidget(m_dockerChip);
    layout->addStretch(1);

    m_themeButton = makeControl(QStringLiteral("◐"), QStringLiteral("切换亮色 / 暗色主题"));
    auto *settingsButton = makeControl(QStringLiteral("⚙"), QStringLiteral("设置"));
    auto *minimizeButton = makeControl(QStringLiteral("—"), QStringLiteral("最小化"));
    auto *maximizeButton = makeControl(QStringLiteral("▢"), QStringLiteral("最大化"));
    auto *closeButton = makeControl(QStringLiteral("✕"), QStringLiteral("关闭"));

    layout->addWidget(m_themeButton);
    layout->addWidget(settingsButton);
    layout->addSpacing(6);
    layout->addWidget(minimizeButton);
    layout->addWidget(maximizeButton);
    layout->addWidget(closeButton);

    QWidget *window = parent;

    connect(m_themeButton, &QToolButton::clicked, this, &TitleBar::themeToggleRequested);
    connect(settingsButton, &QToolButton::clicked, this, &TitleBar::settingsRequested);
    connect(minimizeButton, &QToolButton::clicked, this, [window]() {
        if (window)
            window->showMinimized();
    });
    connect(maximizeButton, &QToolButton::clicked, this, [window]() {
        FramelessHelper::toggleMaximized(window);
    });
    connect(closeButton, &QToolButton::clicked, this, [window]() {
        if (window)
            window->close();
    });

    const Palette &palette = ThemeManager::instance()->palette();
    const QPair<QColor, QColor> logoColors = ThemeManager::accentColors(palette);
    const QString logoBackground =
        ThemeManager::isFlat()
            ? logoColors.first.name()
            : QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %1, stop:1 %2)")
                  .arg(logoColors.first.name(), logoColors.second.name());
    logo->setStyleSheet(QStringLiteral("border-radius: 10px; font-size: 16px; color: #2A2333;"
                                       "background: %1;")
                            .arg(logoBackground));
}

QToolButton *TitleBar::makeControl(const QString &glyph, const QString &tooltip)
{
    auto *button = new QToolButton(this);
    button->setText(glyph);
    button->setToolTip(tooltip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(30, 30);
    button->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: 9px; color: %1; font-size: 13px; }"
        "QToolButton:hover { background: %2; }"
        "QToolButton:pressed { background: %3; }"));
    const Palette &palette = ThemeManager::instance()->palette();
    button->setStyleSheet(button->styleSheet().arg(palette.textMuted.name(),
                                                  palette.surfaceHover.name(),
                                                  palette.border.name()));
    return button;
}

void TitleBar::setSubtitle(const QString &text)
{
    m_subtitle->setText(text);
}

void TitleBar::setBackendState(bool available, const QString &detail)
{
    const Palette &palette = ThemeManager::instance()->palette();
    m_backendChip->setText(available ? QStringLiteral("● 后端就绪") : QStringLiteral("● 后端缺失"));
    m_backendChip->setToolTip(detail);
    m_backendChip->setStyleSheet(
        QStringLiteral("color: %1; background: %2; border-radius: 9px; padding: 2px 9px; font-size: 12px;")
            .arg((available ? palette.success : palette.danger).name(),
                 palette.surfaceAlt.name()));
}

void TitleBar::setDockerState(const QString &state, const QString &detail)
{
    const Palette &palette = ThemeManager::instance()->palette();
    const bool running = state == QLatin1String("running");
    m_dockerChip->setText(running ? QStringLiteral("● Docker 运行中")
                                  : QStringLiteral("● Docker 未运行"));
    m_dockerChip->setToolTip(detail);
    m_dockerChip->setStyleSheet(
        QStringLiteral("color: %1; background: %2; border-radius: 9px; padding: 2px 9px; font-size: 12px;")
            .arg((running ? palette.success : palette.warning).name(),
                 palette.surfaceAlt.name()));
}

void TitleBar::setThemeIsDark(bool dark)
{
    m_themeButton->setText(dark ? QStringLiteral("☾") : QStringLiteral("☀"));
}

void TitleBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Palette &palette = ThemeManager::instance()->palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient gradient(0, 0, width(), 0);
    const QPair<QColor, QColor> tint = ThemeManager::accentColors(
        palette, palette.dark ? 26 : 40, palette.dark ? 26 : 40);
    gradient.setColorAt(0.0, tint.first);
    gradient.setColorAt(1.0, tint.second);
    painter.fillRect(QRectF(0, 0, width(), height()), gradient);

    painter.setPen(Qt::NoPen);
    painter.setBrush(palette.border);
    painter.drawRect(QRectF(0, height() - 1, width(), 1));
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && window() && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    FramelessHelper::toggleMaximized(window());
    event->accept();
}

} // namespace mcsm
