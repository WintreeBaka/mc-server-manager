#pragma once

#include <QAbstractButton>
#include <QComboBox>
#include <QFrame>
#include <QPushButton>
#include <QWidget>

#include "app/ThemeManager.h"

class QLabel;
class QVBoxLayout;
class QHBoxLayout;
class QPropertyAnimation;
class QScrollArea;

namespace mcsm {

/// Rounded surface with optional hover lift and gradient fill.
class CardFrame : public QFrame
{
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    enum Variant { Surface, Alt, Gradient, Accent };

    explicit CardFrame(QWidget *parent = nullptr);

    void setVariant(Variant variant);
    Variant variant() const { return m_variant; }
    void setInteractive(bool interactive);
    void setRadius(int radius);
    void setGlow(bool glow);

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal value);

    static QVBoxLayout *verticalLayout(CardFrame *card, int margin = 18, int spacing = 10);
    static QHBoxLayout *horizontalLayout(CardFrame *card, int margin = 18, int spacing = 10);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void animateHover(qreal target);

    Variant m_variant = Surface;
    bool m_interactive = false;
    bool m_glow = false;
    bool m_pressed = false;
    int m_radius = 18;
    qreal m_hoverProgress = 0.0;
    QPropertyAnimation *m_hoverAnimation = nullptr;
};

/// Button painted with the macaron pink to blue gradient.
class GradientButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(qreal glowProgress READ glowProgress WRITE setGlowProgress)

public:
    enum Style { Primary, Soft, Outline, Danger };

    explicit GradientButton(const QString &text = QString(), QWidget *parent = nullptr);

    void setStyle(Style style);
    Style style() const { return m_style; }
    void setGlyph(const QString &glyph);
    void setCompact(bool compact);

    qreal glowProgress() const { return m_glowProgress; }
    void setGlowProgress(qreal value);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Style m_style = Primary;
    QString m_glyph;
    bool m_compact = false;
    qreal m_glowProgress = 0.0;
    QPropertyAnimation *m_glowAnimation = nullptr;
};

/// iOS style animated switch.
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal knobProgress READ knobProgress WRITE setKnobProgress)

public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

    qreal knobProgress() const { return m_knobProgress; }
    void setKnobProgress(qreal value);

    void setOnText(const QString &on, const QString &off);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void nextCheckState() override;
    /// Keeps the knob in sync when the state is set programmatically.
    void checkStateSet() override;

private:
    void animateTo(bool checked);

    qreal m_knobProgress = 0.0;
    bool m_hovered = false;
    bool m_userToggle = false;
    QPropertyAnimation *m_animation = nullptr;
    QString m_onText;
    QString m_offText;
};

/// Animated segmented control (quick/expert mode, theme picker, ...).
class SegmentedControl : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal indicatorPos READ indicatorPos WRITE setIndicatorPos)

public:
    explicit SegmentedControl(QWidget *parent = nullptr);

    void setItems(const QStringList &items);
    QStringList items() const { return m_items; }
    void setCurrentIndex(int index, bool animate = true);
    int currentIndex() const { return m_current; }
    QSize sizeHint() const override;

    qreal indicatorPos() const { return m_indicatorPos; }
    void setIndicatorPos(qreal value);

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QRectF segmentRect(int index) const;

    QStringList m_items;
    int m_current = 0;
    qreal m_indicatorPos = 0.0;
    QPropertyAnimation *m_animation = nullptr;
};

/// Small pulsing indicator used for container state.
class StatusDot : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal pulse READ pulse WRITE setPulse)

public:
    explicit StatusDot(QWidget *parent = nullptr);

    void setState(const QString &state);
    QString state() const { return m_state; }
    void setDiameter(int diameter);

    QSize sizeHint() const override;

    qreal pulse() const { return m_pulse; }
    void setPulse(qreal value);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor stateColor() const;
    void restartPulse();

    QString m_state = QStringLiteral("stopped");
    int m_diameter = 10;
    qreal m_pulse = 0.0;
    QPropertyAnimation *m_animation = nullptr;
};

/// Dashboard statistic tile.
class StatCard : public CardFrame
{
    Q_OBJECT

public:
    explicit StatCard(QWidget *parent = nullptr);

    void setData(const QString &label, const QString &value, const QString &caption,
                 const QColor &accent, const QString &glyph);
    void setValue(const QString &value);
    QString value() const;

private:
    QLabel *m_label = nullptr;
    QLabel *m_value = nullptr;
    QLabel *m_caption = nullptr;
    QLabel *m_glyph = nullptr;
    QColor m_accent;
};

/// Section heading with an optional trailing widget slot.
QWidget *makeSectionHeader(const QString &title, const QString &subtitle = QString(),
                           QWidget *trailing = nullptr, QWidget *parent = nullptr);

QLabel *makeLabel(const QString &text, const QString &role, QWidget *parent = nullptr);

/// Combo box that stays in sync with the shared server list and selection.
class ServerSelector : public QComboBox
{
    Q_OBJECT

public:
    explicit ServerSelector(QWidget *parent = nullptr);

    QString currentServerId() const;
    void selectServer(const QString &id);
    void setPlaceholder(const QString &text);

signals:
    void serverChanged(const QString &id);

private:
    void scheduleSync();
    void sync();

    QString m_placeholder;
    bool m_syncing = false;
};

/// Base class for the navigation pages: heading plus a scroll friendly body.
class PageBase : public QWidget
{
    Q_OBJECT

public:
    explicit PageBase(const QString &title, const QString &subtitle, QWidget *parent = nullptr);

    virtual void onActivated() {}
    /// Called right before the user navigates away from this page: pages that
    /// hold a log stream / timer must release it here so a background page never
    /// keeps a process (or the CPU) busy.
    virtual void onDeactivated() {}
    virtual void onServerSelectionChanged(const QString &serverId) { Q_UNUSED(serverId) }

    QString title() const { return m_title; }
    QString subtitle() const { return m_subtitle; }
    void setSubtitle(const QString &subtitle);

protected:
    void resizeEvent(QResizeEvent *event) override;

    /// Layout that page content should be appended to.
    QVBoxLayout *body() const { return m_body; }
    void setHeaderTrailing(QWidget *widget);
    /// Puts `widget` in a column LEFT of the scrolling body. That column never
    /// scrolls, so a page menu stays visible no matter how long the content is.
    void setSideColumn(QWidget *widget);

private:
    QString m_title;
    QString m_subtitle;
    QVBoxLayout *m_body = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_content = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QHBoxLayout *m_headerLayout = nullptr;
    QHBoxLayout *m_bodyRow = nullptr;
    QWidget *m_sideColumn = nullptr;
};

} // namespace mcsm
