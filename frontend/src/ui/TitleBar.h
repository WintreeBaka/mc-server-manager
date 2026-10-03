#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

namespace mcsm {

/// Custom window chrome: app identity, live status chips and window controls.
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    void setSubtitle(const QString &text);
    void setBackendState(bool available, const QString &detail = QString());
    void setDockerState(const QString &state, const QString &detail = QString());
    void setThemeIsDark(bool dark);

signals:
    void themeToggleRequested();
    void settingsRequested();
    void notificationsRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    /// The whole bar drags the frameless window, double click toggles maximize.
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QToolButton *makeControl(const QString &glyph, const QString &tooltip);

    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_backendChip = nullptr;
    QLabel *m_dockerChip = nullptr;
    QToolButton *m_themeButton = nullptr;
};

} // namespace mcsm
