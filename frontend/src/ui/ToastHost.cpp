#include "ui/ToastHost.h"

#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

#include "app/Easing.h"
#include "app/ThemeManager.h"
#include "widgets/Common.h"

namespace mcsm {

ToastHost::ToastHost(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(10);
    m_layout->addStretch(1);
    setFixedWidth(320);
    hide();
}

void ToastHost::reposition()
{
    if (!parentWidget())
        return;
    const int margin = 24;
    const int top = 70;
    setGeometry(parentWidget()->width() - width() - margin, top, width(),
                qMax(0, parentWidget()->height() - top - margin));
    raise();
}

void ToastHost::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reposition();
}

void ToastHost::push(const QString &message, const QString &level, int timeoutMs)
{
    const Palette &palette = ThemeManager::instance()->palette();
    QColor accent = palette.blue;
    QString glyph = QStringLiteral("ℹ");
    if (level == QLatin1String("success")) {
        accent = palette.success;
        glyph = QStringLiteral("✔");
    } else if (level == QLatin1String("warning")) {
        accent = palette.warning;
        glyph = QStringLiteral("!");
    } else if (level == QLatin1String("error")) {
        accent = palette.danger;
        glyph = QStringLiteral("✕");
    } else if (level == QLatin1String("pink")) {
        accent = palette.pink;
    }

    auto *card = new CardFrame(this);
    card->setVariant(CardFrame::Surface);
    card->setRadius(16);
    auto *layout = CardFrame::horizontalLayout(card, 14, 10);

    auto *icon = new QLabel(glyph, card);
    icon->setFixedSize(26, 26);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QStringLiteral("border-radius: 13px; color: white; background: %1;")
                            .arg(accent.name()));

    auto *text = new QLabel(message, card);
    text->setWordWrap(true);
    text->setStyleSheet(QStringLiteral("color: %1;").arg(palette.text.name()));

    layout->addWidget(icon, 0, Qt::AlignTop);
    layout->addWidget(text, 1);

    auto *effect = new QGraphicsOpacityEffect(card);
    effect->setOpacity(0.0);
    card->setGraphicsEffect(effect);

    m_layout->insertWidget(qMax(0, m_layout->count() - 1), card);
    m_toasts.append(card);
    while (m_toasts.size() > m_maxVisible) {
        QWidget *oldest = m_toasts.takeFirst();
        m_layout->removeWidget(oldest);
        oldest->deleteLater();
    }

    show();
    raise();
    reposition();

    const int travel = 34;
    card->move(card->x() + travel, card->y() + 12);

    auto *group = new QParallelAnimationGroup(card);
    auto *fade = new QPropertyAnimation(effect, "opacity", group);
    fade->setDuration(Easing::normal());
    fade->setEasingCurve(Easing::entrance());
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);

    auto *slide = new QPropertyAnimation(card, "pos", group);
    slide->setDuration(Easing::normal());
    slide->setEasingCurve(Easing::overshoot());
    slide->setStartValue(card->pos() + QPoint(travel, 0));
    slide->setEndValue(card->pos());

    group->start(QAbstractAnimation::DeleteWhenStopped);

    QTimer::singleShot(qMax(1200, timeoutMs), card, [this, card, effect]() {
        auto *outGroup = new QParallelAnimationGroup(card);
        auto *fadeOut = new QPropertyAnimation(effect, "opacity", outGroup);
        fadeOut->setDuration(Easing::fast());
        fadeOut->setEasingCurve(Easing::standard());
        fadeOut->setStartValue(effect->opacity());
        fadeOut->setEndValue(0.0);

        auto *slideOut = new QPropertyAnimation(card, "pos", outGroup);
        slideOut->setDuration(Easing::fast());
        slideOut->setEasingCurve(Easing::standard());
        slideOut->setStartValue(card->pos());
        slideOut->setEndValue(card->pos() + QPoint(28, 0));

        QObject::connect(outGroup, &QParallelAnimationGroup::finished, this, [this, card]() {
            m_toasts.removeAll(card);
            m_layout->removeWidget(card);
            card->deleteLater();
            if (m_toasts.isEmpty())
                hide();
        });
        outGroup->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

void ToastHost::clearAll()
{
    for (QWidget *toast : m_toasts) {
        m_layout->removeWidget(toast);
        toast->deleteLater();
    }
    m_toasts.clear();
    hide();
}

} // namespace mcsm
