#pragma once

#include <QWidget>

namespace mcsm {

/// Vertical navigation on the left with a gradient highlight on the active item.
class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    void addItem(const QString &glyph, const QString &label, const QString &tooltip = QString());
    /// Removes every trailing item so only `count` entries remain. Plugin pages
    /// always append to the end, so this is enough to drop them again.
    void truncate(int count);
    /// -1 clears the highlight (used when a page outside the sidebar is shown).
    void setCurrentIndex(int index);
    int currentIndex() const { return m_current; }
    int itemCount() const { return m_items.size(); }

signals:
    void itemSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Item
    {
        QString glyph;
        QString label;
        QString tooltip;
    };

    QRectF itemRect(int index) const;
    int itemAt(const QPoint &position) const;

    QVector<Item> m_items;
    int m_current = -1;
    int m_hovered = -1;
    int m_itemHeight = 52;
    int m_topPadding = 16;
};

} // namespace mcsm
