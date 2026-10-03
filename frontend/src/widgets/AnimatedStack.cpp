#include "widgets/AnimatedStack.h"

#include <QParallelAnimationGroup>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QShowEvent>

#include "app/Easing.h"
#include "app/ThemeManager.h"

namespace mcsm {

AnimatedStack::AnimatedStack(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
}

void AnimatedStack::addPage(QWidget *page)
{
    if (!page)
        return;
    page->setParent(this);
    page->hide();
    m_pages.append(page);
    if (m_current < 0) {
        m_current = 0;
        page->setGeometry(rect());
        page->show();
        emit currentChanged(0);
    }
}

QWidget *AnimatedStack::page(int index) const
{
    if (index < 0 || index >= m_pages.size())
        return nullptr;
    return m_pages.at(index);
}

QWidget *AnimatedStack::currentWidget() const
{
    return page(m_current);
}

void AnimatedStack::setGhostOpacity(qreal value)
{
    m_ghostOpacity = value;
    update();
}

void AnimatedStack::resetTransition()
{
    if (m_group) {
        m_group->stop();
        m_group->deleteLater();
        m_group = nullptr;
    }
    m_ghost = QPixmap();
    m_ghostOpacity = 0.0;
    for (QWidget *widget : m_pages)
        widget->hide();
    update();
}

void AnimatedStack::setCurrentIndex(int index, bool animate, int direction)
{
    if (index < 0 || index >= m_pages.size() || index == m_current)
        return;

    QWidget *incoming = m_pages.at(index);
    QWidget *outgoing = (m_current >= 0 && m_current < m_pages.size()) ? m_pages.at(m_current) : nullptr;
    const int previous = m_current;
    const bool useAnimation = animate && outgoing && outgoing != incoming
                              && ThemeManager::instance()->animationsEnabled()
                              && width() > 0 && height() > 0;

    m_current = index;
    // hide everything first: an interrupted transition must never leave pages
    // stacked on top of each other
    resetTransition();

    incoming->setGeometry(rect());
    if (!useAnimation) {
        incoming->show();
        incoming->raise();
        emit currentChanged(index);
        return;
    }

    // freeze the previous page into a pixmap so it can fade out as a still image
    m_ghost = outgoing->grab();
    m_ghostOpacity = m_ghost.isNull() ? 0.0 : 1.0;
    outgoing->hide();

    const int slideDirection = direction != 0 ? direction : (index > previous ? 1 : -1);
    const int offset = qMax(30, width() / 16) * slideDirection;
    incoming->setGeometry(rect().translated(offset, 0));
    incoming->show();
    incoming->raise();

    const int duration = Easing::scaled(m_duration);
    auto *group = new QParallelAnimationGroup(this);

    auto *slide = new QPropertyAnimation(incoming, "pos", group);
    slide->setDuration(duration);
    slide->setEasingCurve(Easing::standard());
    slide->setStartValue(rect().translated(offset, 0).topLeft());
    slide->setEndValue(rect().topLeft());

    auto *fade = new QPropertyAnimation(this, "ghostOpacity", group);
    fade->setDuration(duration);
    fade->setEasingCurve(Easing::entrance());
    fade->setStartValue(m_ghostOpacity);
    fade->setEndValue(0.0);

    QObject::connect(group, &QParallelAnimationGroup::finished, this, [this, incoming]() {
        m_ghost = QPixmap();
        m_ghostOpacity = 0.0;
        m_group = nullptr;
        if (incoming) {
            incoming->setGeometry(rect());
            incoming->raise();
        }
        update();
    });

    m_group = group;
    group->start();
    emit currentChanged(index);
}

void AnimatedStack::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // isHidden() instead of isVisible(): during the very first layout pass the
    // parent window is not mapped yet and isVisible() would report false for
    // every page, leaving them at their construction size.
    for (QWidget *widget : m_pages) {
        if (!widget->isHidden())
            widget->setGeometry(rect());
    }
    // the snapshot has the old geometry, drop it instead of scaling
    if (!m_ghost.isNull() && m_group && m_group->state() == QAbstractAnimation::Running) {
        m_ghost = QPixmap();
        m_ghostOpacity = 0.0;
        update();
    }
}

void AnimatedStack::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_current < 0 || m_current >= m_pages.size())
        return;
    resetTransition();
    QWidget *current = m_pages.at(m_current);
    current->setGeometry(rect());
    current->show();
    current->raise();
}

void AnimatedStack::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (m_ghost.isNull() || m_ghostOpacity <= 0.001)
        return;
    QPainter painter(this);
    painter.setOpacity(m_ghostOpacity);
    painter.drawPixmap(rect().topLeft(), m_ghost);
}

} // namespace mcsm
