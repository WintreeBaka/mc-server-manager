#include "widgets/Chip.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>

#include "app/ThemeManager.h"

namespace mcsm {

Chip::Chip(const QString &text, QWidget *parent)
    : QWidget(parent)
    , m_text(text)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void Chip::setText(const QString &text)
{
    m_text = text;
    updateGeometry();
    update();
}

void Chip::setTone(Tone tone)
{
    m_tone = tone;
    update();
}

void Chip::setCompact(bool compact)
{
    m_compact = compact;
    updateGeometry();
}

QSize Chip::sizeHint() const
{
    const QFontMetrics metrics(font());
    const int horizontal = m_compact ? 12 : 18;
    const int vertical = m_compact ? 4 : 6;
    return QSize(metrics.horizontalAdvance(m_text) + horizontal * 2,
                 metrics.height() + vertical * 2);
}

QColor Chip::background() const
{
    const Palette &p = ThemeManager::instance()->palette();
    QColor color = p.surfaceAlt;
    switch (m_tone) {
    case Pink: color = p.pink; break;
    case Blue: color = p.blue; break;
    case Success: color = p.success; break;
    case Warning: color = p.warning; break;
    case Danger: color = p.danger; break;
    case Gradient: color = p.pink; break;
    case Neutral: color = p.surfaceAlt; break;
    }
    if (m_tone != Neutral) {
        color.setAlphaF(p.dark ? 0.24 : 0.18);
    }
    return color;
}

QColor Chip::foreground() const
{
    const Palette &p = ThemeManager::instance()->palette();
    switch (m_tone) {
    case Pink: return p.dark ? p.pink : QColor(0xB2, 0x5F, 0x8B);
    case Blue: return p.dark ? p.blue : QColor(0x4A, 0x6E, 0x9E);
    case Success: return p.dark ? p.success : QColor(0x4E, 0x8A, 0x6C);
    case Warning: return p.dark ? p.warning : QColor(0x9A, 0x7A, 0x34);
    case Danger: return p.dark ? p.danger : QColor(0xB0, 0x63, 0x71);
    case Gradient: return p.text;
    case Neutral: break;
    }
    return p.textMuted;
}

void Chip::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Palette &p = ThemeManager::instance()->palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = m_compact ? 8.0 : 11.0;

    if (m_tone == Gradient) {
        const QPair<QColor, QColor> accent =
            ThemeManager::accentColors(p, p.dark ? 90 : 70, p.dark ? 80 : 60);
        QLinearGradient gradient = ThemeManager::accentGradient(p, rect);
        gradient.setColorAt(0.0, accent.first);
        gradient.setColorAt(1.0, accent.second);
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
    } else {
        painter.setPen(QPen(m_tone == Neutral ? p.border : background().lighter(140), 1.0));
        painter.setBrush(background());
    }
    painter.drawRoundedRect(rect, radius, radius);

    QFont font = this->font();
    font.setPointSizeF(qMax(7.5, font.pointSizeF() - 0.5));
    painter.setFont(font);
    painter.setPen(foreground());
    painter.drawText(rect, Qt::AlignCenter, m_text);
}

} // namespace mcsm
