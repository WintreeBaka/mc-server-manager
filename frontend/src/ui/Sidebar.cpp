#include "ui/Sidebar.h"

#include <QMouseEvent>
#include <QPainter>

#include "app/ThemeManager.h"

namespace mcsm {

Sidebar::Sidebar(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(196);
    setMouseTracking(true);
}

void Sidebar::addItem(const QString &glyph, const QString &label, const QString &tooltip)
{
    m_items.append(Item {glyph, label, tooltip});
    if (m_current < 0)
        m_current = 0;
    updateGeometry();
    update();
}

void Sidebar::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_items.size())
        return;
    if (m_current == index)
        return;
    m_current = index;
    update();
}

void Sidebar::truncate(int count)
{
    const int keep = qBound(0, count, m_items.size());
    if (keep == m_items.size())
        return;
    m_items.resize(keep);
    if (m_current >= keep)
        m_current = keep > 0 ? keep - 1 : -1;
    if (m_hovered >= keep)
        m_hovered = -1;
    updateGeometry();
    update();
}

QRectF Sidebar::itemRect(int index) const
{
    return QRectF(10, m_topPadding + m_itemHeight * index, width() - 20, m_itemHeight - 8);
}

int Sidebar::itemAt(const QPoint &position) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (itemRect(i).contains(position))
            return i;
    }
    return -1;
}

void Sidebar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const Palette &palette = ThemeManager::instance()->palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // translucent panel
    QColor panel = palette.surface;
    panel.setAlphaF(palette.dark ? 0.75 : 0.65);
    painter.setPen(QPen(palette.border, 1.0));
    painter.setBrush(panel);
    painter.drawRoundedRect(QRectF(rect()).adjusted(6, 6, -6, -6), 20, 20);

    QFont labelFont = font();
    labelFont.setWeight(QFont::DemiBold);
    QFont glyphFont = font();
    // the UI font has no symbol coverage; ask Qt for fonts that do
    glyphFont.setFamilies({QStringLiteral("Segoe UI Symbol"), QStringLiteral("Segoe UI Emoji"),
                           QStringLiteral("Noto Sans Symbols2"), QStringLiteral("DejaVu Sans"),
                           font().family()});
    glyphFont.setPointSizeF(font().pointSizeF() + 2.5);

    for (int i = 0; i < m_items.size(); ++i) {
        const QRectF rect = itemRect(i);
        const bool active = i == m_current;
        const bool hovered = i == m_hovered;

        if (active) {
            QLinearGradient activeGradient = ThemeManager::accentGradient(palette, rect);
            const QPair<QColor, QColor> colors =
                ThemeManager::accentColors(palette, palette.dark ? 96 : 78, palette.dark ? 80 : 60);
            activeGradient.setColorAt(0.0, colors.first);
            activeGradient.setColorAt(1.0, colors.second);
            painter.setPen(Qt::NoPen);
            painter.setBrush(activeGradient);
            painter.drawRoundedRect(rect, 14, 14);
        } else if (hovered) {
            QColor hover = palette.surfaceHover;
            hover.setAlphaF(0.6);
            painter.setPen(Qt::NoPen);
            painter.setBrush(hover);
            painter.drawRoundedRect(rect, 14, 14);
        }

        QRectF glyphRect = rect.adjusted(12, 0, 0, 0);
        painter.setFont(glyphFont);
        painter.setPen(active ? palette.text : palette.textMuted);
        painter.drawText(glyphRect, Qt::AlignVCenter | Qt::AlignLeft, m_items.at(i).glyph);

        QRectF textRect = rect.adjusted(44, 0, -8, 0);
        painter.setFont(labelFont);
        painter.setPen(active ? palette.text : (hovered ? palette.text : palette.textMuted));
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, m_items.at(i).label);
    }
}

void Sidebar::mousePressEvent(QMouseEvent *event)
{
    const int index = itemAt(event->position().toPoint());
    if (index >= 0) {
        setCurrentIndex(index);
        emit itemSelected(index);
    }
    QWidget::mousePressEvent(event);
}

void Sidebar::mouseMoveEvent(QMouseEvent *event)
{
    const int index = itemAt(event->position().toPoint());
    if (index != m_hovered) {
        m_hovered = index;
        setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (index >= 0 && index < m_items.size()) {
            const Item &item = m_items.at(index);
            setToolTip(item.tooltip.isEmpty() ? item.label : item.tooltip);
        }
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void Sidebar::leaveEvent(QEvent *event)
{
    m_hovered = -1;
    update();
    QWidget::leaveEvent(event);
}

void Sidebar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}

} // namespace mcsm
