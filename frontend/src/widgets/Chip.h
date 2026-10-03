#pragma once

#include <QWidget>

namespace mcsm {

/// Small rounded label used for state, flavour and version badges.
class Chip : public QWidget
{
    Q_OBJECT

public:
    enum Tone { Neutral, Pink, Blue, Success, Warning, Danger, Gradient };

    explicit Chip(const QString &text = QString(), QWidget *parent = nullptr);

    void setText(const QString &text);
    QString text() const { return m_text; }
    void setTone(Tone tone);
    Tone tone() const { return m_tone; }
    void setCompact(bool compact);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor background() const;
    QColor foreground() const;

    QString m_text;
    Tone m_tone = Neutral;
    bool m_compact = false;
};

} // namespace mcsm
