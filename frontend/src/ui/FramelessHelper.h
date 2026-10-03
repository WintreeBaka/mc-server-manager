#pragma once

#include <QObject>
#include <Qt>

class QWidget;

namespace mcsm {

/// Frameless window support built on Qt's native startSystemMove/Resize, which
/// keeps snapping, multi-monitor and DPI behaviour identical to native windows.
class FramelessHelper : public QObject
{
    Q_OBJECT

public:
    static void attach(QWidget *window, int borderWidth = 6);
    static void toggleMaximized(QWidget *window);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    FramelessHelper(QWidget *window, int borderWidth);
    Qt::Edges edgesAt(const QPoint &position) const;
    void updateCursor(Qt::Edges edges);

    QWidget *m_window = nullptr;
    int m_border = 6;
    Qt::Edges m_edges;
};

} // namespace mcsm
