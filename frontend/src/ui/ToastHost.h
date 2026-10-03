#pragma once

#include <QWidget>

class QVBoxLayout;

namespace mcsm {

/// Floating notification stack anchored to the top right of the main window.
class ToastHost : public QWidget
{
    Q_OBJECT

public:
    explicit ToastHost(QWidget *parent);

    void push(const QString &message, const QString &level = QStringLiteral("info"), int timeoutMs = 3600);
    void clearAll();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void reposition();

    QVBoxLayout *m_layout = nullptr;
    QVector<QWidget *> m_toasts;
    int m_maxVisible = 4;
};

} // namespace mcsm
