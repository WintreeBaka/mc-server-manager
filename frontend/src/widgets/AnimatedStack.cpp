#include "widgets/AnimatedStack.h"

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

/// Stops a running slide and snaps its page to the final geometry, so the next
/// transition always starts from a clean state.
void AnimatedStack::stopTransition()
{
    if (!m_slide)
        return;
    QPropertyAnimation *animation = m_slide;
    m_slide = nullptr;
    animation->stop();
    if (auto *target = qobject_cast<QWidget *>(animation->targetObject())) {
        target->setGeometry(rect());
        target->raise();
    }
    animation->deleteLater();
}

void AnimatedStack::resetTransition()
{
    stopTransition();
    for (QWidget *widget : m_pages)
        widget->hide();
}

void AnimatedStack::removePage(QWidget *page)
{
    const int index = m_pages.indexOf(page);
    if (index < 0)
        return;
    const bool wasCurrent = index == m_current;
    resetTransition();
    m_pages.removeAt(index);
    if (m_current > index)
        --m_current;
    else if (wasCurrent)
        m_current = -1;
    page->setParent(nullptr);
    page->deleteLater();
    if (wasCurrent) {
        // show whatever is left instead of an empty stack
        const int fallback = qBound(0, index - 1, m_pages.size() - 1);
        if (fallback >= 0 && fallback < m_pages.size()) {
            m_current = fallback;
            QWidget *widget = m_pages.at(fallback);
            widget->setGeometry(rect());
            widget->show();
            widget->raise();
            emit currentChanged(fallback);
        }
    }
}

void AnimatedStack::setCurrentIndexSilently(int index)
{
    if (index < 0 || index >= m_pages.size())
        return;
    m_current = index;
    resetTransition();
    QWidget *widget = m_pages.at(index);
    widget->setGeometry(rect());
    widget->show();
    widget->raise();
    emit currentChanged(index);
}

void AnimatedStack::setCurrentIndex(int index, bool animate, int direction)
{
    if (index < 0 || index >= m_pages.size() || index == m_current)
        return;

    QWidget *incoming = m_pages.at(index);
    const int previous = m_current;
    // A burst of switches (menu spam, held arrow keys) is served instantly:
    // animating every step is what made the UI feel laggy and burn CPU.
    const bool burst = m_slide != nullptr;

    m_current = index;
    stopTransition();
    // Hide everything first: an interrupted transition must never leave two
    // pages visible on top of each other (that caused the earlier ghosting bug).
    for (QWidget *widget : m_pages) {
        if (widget != incoming)
            widget->hide();
    }
    incoming->setGeometry(rect());
    incoming->show();
    incoming->raise();

    const bool useAnimation = animate && !burst && previous >= 0 && m_duration > 0
                              && ThemeManager::instance()->animationsEnabled()
                              && width() > 0 && height() > 0;
    if (!useAnimation) {
        emit currentChanged(index);
        return;
    }

    const int slideDirection = direction != 0 ? direction : (index > previous ? 1 : -1);
    const int offset = qMax(30, width() / 16) * slideDirection;
    incoming->setGeometry(rect().translated(offset, 0));

    auto *slide = new QPropertyAnimation(incoming, "pos", this);
    slide->setDuration(Easing::scaled(m_duration));
    slide->setEasingCurve(Easing::standard());
    slide->setStartValue(rect().translated(offset, 0).topLeft());
    slide->setEndValue(rect().topLeft());
    m_slide = slide;
    connect(slide, &QPropertyAnimation::finished, this, [this, incoming]() {
        m_slide = nullptr;
        incoming->setGeometry(rect());
        incoming->raise();
    });
    slide->start();
    emit currentChanged(index);
}

void AnimatedStack::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    stopTransition();
    // isHidden() instead of isVisible(): during the very first layout pass the
    // parent window is not mapped yet and isVisible() would report false for
    // every page, leaving them at their construction size.
    for (QWidget *widget : m_pages) {
        if (!widget->isHidden())
            widget->setGeometry(rect());
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

} // namespace mcsm
