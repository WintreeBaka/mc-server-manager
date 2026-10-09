#pragma once

#include <QWidget>

class QPropertyAnimation;

namespace mcsm {

/// Page container with slide transitions.
///
/// Design notes (performance + robustness):
///  * Only one page is ever visible: the outgoing page is hidden *before* the
///    new one slides in, so an interrupted animation can never stack pages.
///  * No snapshot of the outgoing page is taken. Grabbing a full page pixmap on
///    every switch was the main CPU cost when clicking through the menus fast.
///  * While a transition runs, further switches are applied instantly -
///    animating every step of a burst was wasted work and felt laggy.
class AnimatedStack : public QWidget
{
    Q_OBJECT

public:
    explicit AnimatedStack(QWidget *parent = nullptr);

    void addPage(QWidget *page);
    /// Detaches and deletes a contributed page. Used when plugins are reloaded
    /// or uninstalled: indices of the remaining pages stay consistent.
    void removePage(QWidget *page);
    QWidget *page(int index) const;
    int count() const { return m_pages.size(); }
    int currentIndex() const { return m_current; }
    void setCurrentIndexSilently(int index);
    QWidget *currentWidget() const;

    /// Direction: -1 = new page enters from the left, 1 = from the right,
    /// 0 = automatic based on the index delta.
    void setCurrentIndex(int index, bool animate = true, int direction = 0);

    void setDuration(int milliseconds) { m_duration = milliseconds; }
    int duration() const { return m_duration; }

    /// True while a slide animation is running.
    bool transitionRunning() const { return m_slide != nullptr; }

signals:
    void currentChanged(int index);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    /// Stops a pending animation and hides every page.
    void resetTransition();
    void stopTransition();

    QVector<QWidget *> m_pages;
    int m_current = -1;
    int m_duration = 300;
    QPropertyAnimation *m_slide = nullptr;
};

} // namespace mcsm
