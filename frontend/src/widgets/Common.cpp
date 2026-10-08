#include "widgets/Common.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "app/Easing.h"
#include "app/AppContext.h"

namespace mcsm {
namespace {

/// Free helper: avoids clashing with QWidget::palette() inside member functions.
const Palette &themePalette()
{
    return ThemeManager::instance()->palette();
}

qreal mix(qreal from, qreal to, qreal t)
{
    return from + (to - from) * t;
}

QColor blend(const QColor &from, const QColor &to, qreal t)
{
    return QColor::fromRgbF(mix(from.redF(), to.redF(), t),
                            mix(from.greenF(), to.greenF(), t),
                            mix(from.blueF(), to.blueF(), t),
                            mix(from.alphaF(), to.alphaF(), t));
}

} // namespace

CardFrame::CardFrame(QWidget *parent)
    : QFrame(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setProperty("card", "true");
}

void CardFrame::setVariant(Variant variant)
{
    m_variant = variant;
    setProperty("card", variant == Alt ? "alt" : (variant == Gradient ? "gradient" : "true"));
    update();
}

void CardFrame::setInteractive(bool interactive)
{
    m_interactive = interactive;
    setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void CardFrame::setRadius(int radius)
{
    m_radius = radius;
    update();
}

void CardFrame::setGlow(bool glow)
{
    m_glow = glow;
    update();
}

void CardFrame::setHoverProgress(qreal value)
{
    m_hoverProgress = value;
    update();
}

void CardFrame::animateHover(qreal target)
{
    if (!m_hoverAnimation) {
        m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
        m_hoverAnimation->setEasingCurve(Easing::standard());
    }
    if (!ThemeManager::instance()->animationsEnabled()) {
        setHoverProgress(target);
        return;
    }
    m_hoverAnimation->stop();
    m_hoverAnimation->setDuration(Easing::fast());
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(target);
    m_hoverAnimation->start();
}

void CardFrame::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Palette &p = themePalette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = m_radius + m_hoverProgress;

    if (m_variant == Gradient || m_variant == Accent) {
        QLinearGradient gradient = m_variant == Gradient
                                       ? ThemeManager::softAccentGradient(p, rect)
                                       : ThemeManager::accentGradient(p, rect);
        if (m_hoverProgress > 0.0) {
            QLinearGradient stronger = ThemeManager::accentGradient(p, rect);
            if (m_variant == Gradient)
                stronger = ThemeManager::softAccentGradient(p, rect);
            gradient.setColorAt(0.0, blend(gradient.stops().at(0).second,
                                           stronger.stops().at(0).second, m_hoverProgress));
            gradient.setColorAt(0.5, blend(gradient.stops().at(1).second,
                                           stronger.stops().at(1).second, m_hoverProgress));
            gradient.setColorAt(1.0, blend(gradient.stops().at(2).second,
                                           stronger.stops().at(2).second, m_hoverProgress));
        }
        painter.setBrush(gradient);
        painter.setPen(QPen(QColor(p.border.red(), p.border.green(), p.border.blue(),
                                   p.border.alpha()), 1.0));
    } else {
        QColor background = m_variant == Alt ? p.surfaceAlt : p.surface;
        background = blend(background, p.surfaceHover, m_hoverProgress * 0.8);
        painter.setBrush(background);
        QColor border = blend(p.border, p.pink, m_hoverProgress * 0.45);
        painter.setPen(QPen(border, 1.0));
    }

    painter.drawRoundedRect(rect, radius, radius);

    if (m_variant == Surface && m_hoverProgress > 0.01) {
        QLinearGradient accent = ThemeManager::accentGradient(p, QRectF(rect.left(), rect.bottom() - 3,
                                                                        rect.width(), 3));
        const QPair<QColor, QColor> colors =
            ThemeManager::accentColors(p, int(200 * m_hoverProgress), int(200 * m_hoverProgress));
        accent.setColorAt(0.0, colors.first);
        accent.setColorAt(1.0, colors.second);
        QPainterPath path;
        path.addRoundedRect(rect.adjusted(10, rect.height() - 3, -10, 0), 1.5, 1.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawPath(path);
    }
}

void CardFrame::enterEvent(QEnterEvent *event)
{
    if (m_interactive)
        animateHover(1.0);
    QFrame::enterEvent(event);
}

void CardFrame::leaveEvent(QEvent *event)
{
    if (m_interactive)
        animateHover(0.0);
    QFrame::leaveEvent(event);
}

void CardFrame::mousePressEvent(QMouseEvent *event)
{
    m_pressed = m_interactive && event->button() == Qt::LeftButton;
    QFrame::mousePressEvent(event);
}

void CardFrame::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_pressed && m_interactive && rect().contains(event->position().toPoint()))
        emit clicked();
    m_pressed = false;
    QFrame::mouseReleaseEvent(event);
}

QVBoxLayout *CardFrame::verticalLayout(CardFrame *card, int margin, int spacing)
{
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(spacing);
    return layout;
}

QHBoxLayout *CardFrame::horizontalLayout(CardFrame *card, int margin, int spacing)
{
    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(spacing);
    return layout;
}

GradientButton::GradientButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent)
{
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(38);
}

void GradientButton::setStyle(Style newStyle)
{
    m_style = newStyle;
    setProperty("variant",
                newStyle == Outline ? "ghost" : (newStyle == Danger ? "danger" : "primary"));
    QWidget::style()->unpolish(this);
    QWidget::style()->polish(this);
    update();
}

void GradientButton::setGlyph(const QString &glyph)
{
    m_glyph = glyph;
    update();
}

void GradientButton::setCompact(bool compact)
{
    m_compact = compact;
    setMinimumHeight(compact ? 30 : 38);
    updateGeometry();
}

void GradientButton::setGlowProgress(qreal value)
{
    m_glowProgress = value;
    update();
}

void GradientButton::enterEvent(QEnterEvent *event)
{
    if (!m_glowAnimation) {
        m_glowAnimation = new QPropertyAnimation(this, "glowProgress", this);
        m_glowAnimation->setEasingCurve(Easing::standard());
        m_glowAnimation->setDuration(Easing::fast());
    }
    if (ThemeManager::instance()->animationsEnabled()) {
        m_glowAnimation->stop();
        m_glowAnimation->setStartValue(m_glowProgress);
        m_glowAnimation->setEndValue(1.0);
        m_glowAnimation->start();
    } else {
        setGlowProgress(1.0);
    }
    QPushButton::enterEvent(event);
}

void GradientButton::leaveEvent(QEvent *event)
{
    if (m_glowAnimation && ThemeManager::instance()->animationsEnabled()) {
        m_glowAnimation->stop();
        m_glowAnimation->setStartValue(m_glowProgress);
        m_glowAnimation->setEndValue(0.0);
        m_glowAnimation->start();
    } else {
        setGlowProgress(0.0);
    }
    QPushButton::leaveEvent(event);
}

void GradientButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (m_style == Outline) {
        QPushButton::paintEvent(event);
        return;
    }

    const Palette &p = themePalette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF rect = QRectF(this->rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    const qreal radius = m_compact ? 10.0 : 12.0;
    const qreal lift = m_glowProgress * 1.0;
    rect.adjust(0, -lift, 0, -lift);

    const bool enabled = isEnabled();
    QLinearGradient gradient = ThemeManager::accentGradient(p, rect);
    if (m_style == Soft) {
        const int first = int(255 * (0.28 + 0.16 * m_glowProgress));
        const int second = int(255 * (0.24 + 0.16 * m_glowProgress));
        const QPair<QColor, QColor> colors = ThemeManager::accentColors(p, first, second);
        gradient.setColorAt(0.0, colors.first);
        gradient.setColorAt(1.0, colors.second);
    } else if (m_style == Danger) {
        gradient.setColorAt(0.0, p.danger);
        gradient.setColorAt(1.0, ThemeManager::isFlat() ? p.danger : blend(p.danger, p.pink, 0.4));
    } else {
        const QPair<QColor, QColor> colors = ThemeManager::accentColors(p);
        const qreal lift = m_glowProgress * 0.12;
        gradient.setColorAt(0.0, blend(colors.first, QColor(255, 255, 255), lift));
        gradient.setColorAt(1.0, blend(colors.second, QColor(255, 255, 255), lift));
    }
    if (!enabled) {
        const QPair<QColor, QColor> colors = ThemeManager::accentColors(p);
        gradient.setColorAt(0.0, blend(colors.first, p.surface, 0.7));
        gradient.setColorAt(1.0, blend(colors.second, p.surface, 0.7));
    }

    if (isDown())
        rect.adjust(0, 1.4, 0, 1.4);

    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(rect, radius, radius);

    if (m_glowProgress > 0.01 && enabled) {
        QColor glow = p.pink;
        glow.setAlphaF(0.30 * m_glowProgress);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(glow, 2.0));
        painter.drawRoundedRect(rect.adjusted(-1.5, -1.5, 1.5, 1.5), radius + 2, radius + 2);
    }

    // Pastel fills (macaron palette) read better with dark text in both themes.
    QColor textColor = (m_style == Outline) ? p.text : QColor(0x3A, 0x30, 0x44);
    if (p.dark && m_style != Outline)
        textColor = QColor(0x2A, 0x23, 0x33);
    if (!enabled)
        textColor = p.textMuted;

    QFont font = this->font();
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(textColor);

    QString label = QPushButton::text();
    if (!m_glyph.isEmpty())
        label = m_glyph + QStringLiteral("  ") + label;
    painter.drawText(rect, Qt::AlignCenter, label);
}

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    // keep the pill compact: a form layout would otherwise stretch it across the
    // whole row
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    m_animation = new QPropertyAnimation(this, "knobProgress", this);
    m_animation->setEasingCurve(Easing::overshoot());
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(52, 28);
}

void ToggleSwitch::setKnobProgress(qreal value)
{
    m_knobProgress = value;
    update();
}

void ToggleSwitch::setOnText(const QString &on, const QString &off)
{
    m_onText = on;
    m_offText = off;
    update();
}

void ToggleSwitch::animateTo(bool checked)
{
    const qreal target = checked ? 1.0 : 0.0;
    if (!ThemeManager::instance()->animationsEnabled()) {
        setKnobProgress(target);
        return;
    }
    m_animation->stop();
    m_animation->setDuration(Easing::normal());
    m_animation->setStartValue(m_knobProgress);
    m_animation->setEndValue(target);
    m_animation->start();
}

void ToggleSwitch::nextCheckState()
{
    m_userToggle = true;
    QAbstractButton::nextCheckState();
    m_userToggle = false;
    animateTo(isChecked());
}

void ToggleSwitch::checkStateSet()
{
    QAbstractButton::checkStateSet();
    if (m_userToggle)
        return; // the click handler animates instead of snapping
    setKnobProgress(isChecked() ? 1.0 : 0.0);
}

void ToggleSwitch::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ToggleSwitch::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ToggleSwitch::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Palette &p = themePalette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal progress = isChecked() ? qMax(m_knobProgress, 0.001) : m_knobProgress;
    QRectF track = QRectF(rect()).adjusted(1.0, 3.0, -1.0, -3.0);
    const qreal radius = track.height() / 2.0;

    QColor offColor = blend(p.surfaceAlt, p.border, m_hovered ? 0.6 : 0.35);
    const QPair<QColor, QColor> colors = ThemeManager::accentColors(p);
    painter.setPen(QPen(blend(offColor, p.pink, progress * 0.6), 1.0));
    painter.setBrush(blend(offColor, colors.first, progress));
    if (progress > 0.001) {
        QLinearGradient fill = ThemeManager::accentGradient(p, track);
        fill.setColorAt(0.0, blend(offColor, colors.first, progress));
        fill.setColorAt(1.0, blend(offColor, colors.second, progress));
        painter.setBrush(fill);
    }
    painter.drawRoundedRect(track, radius, radius);

    const qreal knobSize = track.height() - 6.0;
    const qreal travel = track.width() - knobSize - 6.0;
    const qreal x = track.left() + 3.0 + travel * progress;
    QRectF knob(x, track.top() + 3.0, knobSize, knobSize);

    painter.setPen(Qt::NoPen);
    QColor knobColor = QColor(0xFF, 0xFF, 0xFF);
    if (m_hovered)
        knobColor = blend(knobColor, p.pinkSoft, 0.25);
    painter.setBrush(knobColor);
    painter.drawEllipse(knob);

    if (!m_onText.isEmpty() || !m_offText.isEmpty()) {
        painter.setPen(p.textMuted);
        QFont font = this->font();
        font.setPointSizeF(qMax(7.0, font.pointSizeF() - 1.5));
        painter.setFont(font);
        const QString label = isChecked() ? m_onText : m_offText;
        painter.drawText(QRectF(track.right() + 6, rect().top(), width(), rect().height()),
                         Qt::AlignVCenter | Qt::AlignLeft, label);
    }
}

SegmentedControl::SegmentedControl(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(36);
    setCursor(Qt::PointingHandCursor);
    m_animation = new QPropertyAnimation(this, "indicatorPos", this);
    m_animation->setEasingCurve(Easing::emphasized());
}

void SegmentedControl::setItems(const QStringList &items)
{
    m_items = items;
    m_current = qBound(0, m_current, qMax(0, items.size() - 1));
    updateGeometry();
    update();
}

void SegmentedControl::setCurrentIndex(int index, bool animate)
{
    if (m_items.isEmpty())
        return;
    const int clamped = qBound(0, index, m_items.size() - 1);
    if (clamped == m_current && animate) {
        update();
        return;
    }
    m_current = clamped;
    const qreal target = qreal(clamped);
    if (!animate || !ThemeManager::instance()->animationsEnabled()) {
        setIndicatorPos(target);
    } else {
        m_animation->stop();
        m_animation->setDuration(Easing::normal());
        m_animation->setStartValue(m_indicatorPos);
        m_animation->setEndValue(target);
        m_animation->start();
    }
    emit currentChanged(clamped);
}

void SegmentedControl::setIndicatorPos(qreal value)
{
    m_indicatorPos = value;
    update();
}

QSize SegmentedControl::sizeHint() const
{
    if (m_items.isEmpty())
        return QSize(160, 36);
    int width = 0;
    const QFontMetrics metrics(font());
    for (const QString &item : m_items)
        width += metrics.horizontalAdvance(item) + 42;
    return QSize(qMax(width, 160), 36);
}

QRectF SegmentedControl::segmentRect(int index) const
{
    if (m_items.isEmpty())
        return QRectF();
    const qreal segmentWidth = qreal(width() - 6) / m_items.size();
    return QRectF(3.0 + segmentWidth * index, 3.0, segmentWidth, height() - 6.0);
}

void SegmentedControl::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Palette &p = themePalette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF container = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(p.border, 1.0));
    painter.setBrush(p.surfaceAlt);
    painter.drawRoundedRect(container, 12.0, 12.0);

    if (m_items.isEmpty())
        return;

    const qreal segmentWidth = qreal(width() - 6) / m_items.size();
    QRectF indicator(3.0 + segmentWidth * m_indicatorPos, 3.0, segmentWidth, height() - 6.0);
    QLinearGradient gradient = ThemeManager::accentGradient(p, indicator);
    if (p.dark) {
        const QPair<QColor, QColor> colors = ThemeManager::accentColors(p);
        gradient.setColorAt(0.0, blend(colors.first, p.surface, 0.35));
        gradient.setColorAt(1.0, blend(colors.second, p.surface, 0.35));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(indicator, 10.0, 10.0);

    QFont font = this->font();
    font.setWeight(QFont::DemiBold);
    for (int i = 0; i < m_items.size(); ++i) {
        const QRectF segment = segmentRect(i);
        const bool active = i == m_current;
        painter.setFont(font);
        painter.setPen(active ? p.text : p.textMuted);
        painter.drawText(segment, Qt::AlignCenter, m_items.at(i));
    }
}

void SegmentedControl::mousePressEvent(QMouseEvent *event)
{
    if (m_items.isEmpty()) {
        QWidget::mousePressEvent(event);
        return;
    }
    for (int i = 0; i < m_items.size(); ++i) {
        if (segmentRect(i).contains(event->position()))
            setCurrentIndex(i);
    }
    QWidget::mousePressEvent(event);
}

void SegmentedControl::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}

StatusDot::StatusDot(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_animation = new QPropertyAnimation(this, "pulse", this);
    m_animation->setEasingCurve(Easing::standard());
}

void StatusDot::setState(const QString &state)
{
    if (m_state == state)
        return;
    m_state = state;
    restartPulse();
    update();
}

void StatusDot::setDiameter(int diameter)
{
    m_diameter = diameter;
    updateGeometry();
    update();
}

QSize StatusDot::sizeHint() const
{
    return QSize(m_diameter + 6, m_diameter + 6);
}

void StatusDot::setPulse(qreal value)
{
    m_pulse = value;
    update();
}

QColor StatusDot::stateColor() const
{
    const Palette &p = themePalette();
    if (m_state == QLatin1String("running"))
        return p.success;
    if (m_state == QLatin1String("starting") || m_state == QLatin1String("stopping")
        || m_state == QLatin1String("restarting"))
        return p.warning;
    if (m_state == QLatin1String("error"))
        return p.danger;
    return p.textMuted;
}

void StatusDot::restartPulse()
{
    const bool animated = ThemeManager::instance()->animationsEnabled()
                          && (m_state != QLatin1String("stopped"));
    m_animation->stop();
    if (!animated) {
        setPulse(0.0);
        return;
    }
    m_animation->setDuration(Easing::scaled(1400));
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    m_animation->setLoopCount(-1);
    m_animation->start();
}

void StatusDot::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor color = stateColor();
    QColor halo = color;
    halo.setAlphaF(0.28 * (1.0 - m_pulse));
    painter.setPen(Qt::NoPen);
    painter.setBrush(halo);
    const qreal haloSize = m_diameter + 6.0 * m_pulse;
    painter.drawEllipse(QRectF((width() - haloSize) / 2.0, (height() - haloSize) / 2.0,
                               haloSize, haloSize));
    painter.setBrush(color);
    painter.drawEllipse(QRectF((width() - m_diameter) / 2.0, (height() - m_diameter) / 2.0,
                               m_diameter, m_diameter));
}

StatCard::StatCard(QWidget *parent)
    : CardFrame(parent)
{
    auto *layout = CardFrame::horizontalLayout(this, 18, 12);
    auto *textColumn = new QVBoxLayout();
    textColumn->setSpacing(2);

    m_label = makeLabel(QString(), QStringLiteral("caption"), this);
    m_value = makeLabel(QStringLiteral("--"), QStringLiteral("value"), this);
    m_caption = makeLabel(QString(), QStringLiteral("hint"), this);
    textColumn->addWidget(m_label);
    textColumn->addWidget(m_value);
    textColumn->addWidget(m_caption);
    textColumn->addStretch();

    m_glyph = makeLabel(QStringLiteral("◆"), QStringLiteral("value"), this);
    m_glyph->setAlignment(Qt::AlignCenter);

    layout->addLayout(textColumn, 1);
    layout->addWidget(m_glyph, 0, Qt::AlignTop);
    setMinimumHeight(104);
}

void StatCard::setData(const QString &label, const QString &value, const QString &caption,
                       const QColor &accent, const QString &glyph)
{
    m_accent = accent;
    m_label->setText(label);
    m_value->setText(value);
    m_caption->setText(caption);
    m_glyph->setText(glyph);
    m_value->setStyleSheet(QStringLiteral("color: %1; font-size: 24px; font-weight: 700;")
                               .arg(accent.name()));
    m_glyph->setStyleSheet(QStringLiteral("color: %1; font-size: 20px;").arg(accent.name()));
}

void StatCard::setValue(const QString &value)
{
    m_value->setText(value);
}

QString StatCard::value() const
{
    return m_value ? m_value->text() : QString();
}

QLabel *makeLabel(const QString &text, const QString &role, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("role", role);
    if (role == QLatin1String("caption") || role == QLatin1String("hint"))
        label->setWordWrap(true);
    return label;
}

QWidget *makeSectionHeader(const QString &title, const QString &subtitle, QWidget *trailing, QWidget *parent)
{
    auto *container = new QWidget(parent);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *column = new QVBoxLayout();
    column->setSpacing(2);
    column->addWidget(makeLabel(title, QStringLiteral("subtitle"), container));
    if (!subtitle.isEmpty())
        column->addWidget(makeLabel(subtitle, QStringLiteral("caption"), container));
    layout->addLayout(column, 1);
    if (trailing)
        layout->addWidget(trailing, 0, Qt::AlignRight | Qt::AlignVCenter);
    return container;
}

PageBase::PageBase(const QString &title, const QString &subtitle, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
    , m_subtitle(subtitle)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 12, 14);
    root->setSpacing(16);

    auto *header = new QWidget(this);
    m_headerLayout = new QHBoxLayout(header);
    m_headerLayout->setContentsMargins(0, 0, 0, 0);
    m_headerLayout->setSpacing(12);

    auto *column = new QVBoxLayout();
    column->setSpacing(3);
    m_titleLabel = makeLabel(title, QStringLiteral("title"), header);
    m_subtitleLabel = makeLabel(subtitle, QStringLiteral("caption"), header);
    // The page subtitle is a single line by design: word wrap would shrink the
    // header column and produce awkward two line headings.
    m_subtitleLabel->setWordWrap(false);
    column->addWidget(m_titleLabel);
    column->addWidget(m_subtitleLabel);
    m_headerLayout->addLayout(column);
    m_headerLayout->addStretch(1);

    root->addWidget(header);

    // The body scrolls instead of being squeezed: a Qt layout compresses cards
    // below their natural height when the window is short, which made text
    // overlap.
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // never let the viewport flash the palette base colour (white) while a page
    // is being switched or scrolled
    m_scroll->setAutoFillBackground(false);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    m_content = new QWidget(m_scroll);
    m_content->setObjectName(QStringLiteral("pageBody"));
    m_content->setAutoFillBackground(false);
    m_body = new QVBoxLayout(m_content);
    m_body->setContentsMargins(0, 0, 14, 8);
    m_body->setSpacing(16);
    m_scroll->setWidget(m_content);

    // header, then [optional fixed side column | scrolling body]
    m_bodyRow = new QHBoxLayout();
    m_bodyRow->setContentsMargins(0, 0, 0, 0);
    m_bodyRow->setSpacing(16);
    m_bodyRow->addWidget(m_scroll, 1);
    root->addLayout(m_bodyRow, 1);
}

void PageBase::setSideColumn(QWidget *widget)
{
    if (!m_bodyRow || !widget)
        return;
    if (m_sideColumn) {
        m_bodyRow->removeWidget(m_sideColumn);
        m_sideColumn->setParent(nullptr);
        m_sideColumn->deleteLater();
    }
    m_sideColumn = widget;
    widget->setParent(this);
    m_bodyRow->insertWidget(0, widget, 0);
}

void PageBase::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Fill the viewport when the content is shorter, but never let a short
    // window squeeze the cards: pin the content to its natural height so the
    // scroll area scrolls instead (a squeezed layout makes text overlap).
    if (!m_scroll || !m_content)
        return;
    const int viewportHeight = m_scroll->viewport()->height();
    const int naturalHeight = m_content->layout() ? m_content->layout()->sizeHint().height() : 0;
    const int target = qMax(viewportHeight, naturalHeight);
    if (m_content->minimumHeight() != target)
        m_content->setMinimumHeight(target);
}

void PageBase::setSubtitle(const QString &subtitle)
{
    m_subtitle = subtitle;
    if (m_subtitleLabel)
        m_subtitleLabel->setText(subtitle);
}

void PageBase::setHeaderTrailing(QWidget *widget)
{
    if (!widget || !m_headerLayout)
        return;
    m_headerLayout->addWidget(widget, 0, Qt::AlignRight | Qt::AlignVCenter);
}

ServerSelector::ServerSelector(QWidget *parent)
    : QComboBox(parent)
    , m_placeholder(QStringLiteral("请选择服务器"))
{
    setMinimumWidth(240);
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
    AppContext *context = AppContext::instance();
    connect(context->servers(), &ServerModel::changed, this, &ServerSelector::scheduleSync);
    connect(context, &AppContext::selectionChanged, this, [this](const QString &) { scheduleSync(); });
    connect(this, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_syncing)
            return;
        emit serverChanged(currentServerId());
    });
    sync();
}

void ServerSelector::setPlaceholder(const QString &text)
{
    m_placeholder = text;
    sync();
}

void ServerSelector::scheduleSync()
{
    QTimer::singleShot(0, this, &ServerSelector::sync);
}

QString ServerSelector::currentServerId() const
{
    return currentData().toString();
}

void ServerSelector::selectServer(const QString &id)
{
    for (int i = 0; i < count(); ++i) {
        if (itemData(i).toString() != id)
            continue;
        if (currentIndex() != i) {
            m_syncing = true;
            setCurrentIndex(i);
            m_syncing = false;
        }
        return;
    }
}

void ServerSelector::sync()
{
    AppContext *context = AppContext::instance();
    const QVector<ServerInfo> servers = context->servers()->servers();
    QString wanted = context->selectedServerId();
    if (wanted.isEmpty())
        wanted = currentServerId();

    m_syncing = true;
    clear();
    if (servers.isEmpty()) {
        addItem(m_placeholder, QString());
    } else {
        for (const ServerInfo &info : servers) {
            const QString label = QStringLiteral("%1 · %2 · %3")
                                      .arg(info.name, info.statusText(), info.typeLabel());
            addItem(label, info.id);
        }
        selectServer(wanted.isEmpty() ? servers.first().id : wanted);
    }
    m_syncing = false;
    if (!currentData().toString().isEmpty())
        emit serverChanged(currentData().toString());
}

} // namespace mcsm
