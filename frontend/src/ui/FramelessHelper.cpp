#include "ui/FramelessHelper.h"

#include <QEvent>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

namespace mcsm {
void FramelessHelper::attach(QWidget *window, int borderWidth)
{
    if (!window)
        return;
    window->setWindowFlag(Qt::FramelessWindowHint, true);
    window->setAttribute(Qt::WA_Hover, true);
    window->setMouseTracking(true);
    new FramelessHelper(window, borderWidth);
}

FramelessHelper::FramelessHelper(QWidget *window, int borderWidth)
    : QObject(window)
    , m_window(window)
    , m_border(borderWidth)
{
    window->installEventFilter(this);
}

Qt::Edges FramelessHelper::edgesAt(const QPoint &position) const
{
    Qt::Edges edges;
    if (!m_window || m_window->isMaximized() || m_window->isFullScreen())
        return edges;
    const int width = m_window->width();
    const int height = m_window->height();
    if (position.x() <= m_border)
        edges |= Qt::LeftEdge;
    if (position.x() >= width - m_border)
        edges |= Qt::RightEdge;
    if (position.y() <= m_border)
        edges |= Qt::TopEdge;
    if (position.y() >= height - m_border)
        edges |= Qt::BottomEdge;
    return edges;
}

void FramelessHelper::updateCursor(Qt::Edges edges)
{
    if (!m_window)
        return;
    if ((edges & Qt::LeftEdge && edges & Qt::TopEdge)
        || (edges & Qt::RightEdge && edges & Qt::BottomEdge)) {
        m_window->setCursor(Qt::SizeFDiagCursor);
    } else if ((edges & Qt::RightEdge && edges & Qt::TopEdge)
               || (edges & Qt::LeftEdge && edges & Qt::BottomEdge)) {
        m_window->setCursor(Qt::SizeBDiagCursor);
    } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
        m_window->setCursor(Qt::SizeHorCursor);
    } else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
        m_window->setCursor(Qt::SizeVerCursor);
    } else {
        m_window->unsetCursor();
    }
}

bool FramelessHelper::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window || !m_window)
        return QObject::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::HoverMove: {
        auto *hover = static_cast<QHoverEvent *>(event);
        m_edges = edgesAt(hover->position().toPoint());
        updateCursor(m_edges);
        break;
    }
    case QEvent::Leave:
        m_edges = Qt::Edges();
        m_window->unsetCursor();
        break;
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton)
            break;
        const Qt::Edges edges = edgesAt(mouse->position().toPoint());
        if (edges != Qt::Edges() && m_window->windowHandle()) {
            m_window->windowHandle()->startSystemResize(edges);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void FramelessHelper::toggleMaximized(QWidget *window)
{
    if (!window)
        return;
    if (window->isMaximized())
        window->showNormal();
    else
        window->showMaximized();
}

} // namespace mcsm
