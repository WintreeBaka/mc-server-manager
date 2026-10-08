#pragma once

#include <QPixmap>
#include <QWidget>

class QParallelAnimationGroup;

namespace mcsm {

/// Page container with slide transitions.
///
/// Only one page widget is ever visible: the new page slides in while a
/// *snapshot* of the previous page fades out. Keeping two live pages on screen
/// (or leaving them visible after an interrupted animation) was what caused the
/// stacking and ghosting artefacts.
class AnimatedStack : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal ghostOpacity READ ghostOpacity WRITE setGhostOpacity)

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

    qreal ghostOpacity() const { return m_ghostOpacity; }
    void setGhostOpacity(qreal value);

signals:
    void currentChanged(int index);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    /// Stops pending animations, drops the snapshot and hides every page.
    void resetTransition();

    QVector<QWidget *> m_pages;
    int m_current = -1;
    int m_duration = 300;
    QParallelAnimationGroup *m_group = nullptr;
    QPixmap m_ghost;
    qreal m_ghostOpacity = 0.0;
};

} // namespace mcsm
