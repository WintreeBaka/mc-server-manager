#include "widgets/LogView.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

#include "app/Easing.h"
#include "app/ThemeManager.h"

namespace mcsm {

LogView::LogView(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setProperty("mono", "true");
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setMaximumBlockCount(m_maxLines + 50);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    document()->setDocumentMargin(10);
}

QString LogView::classify(const QString &line)
{
    const QString lowered = line.toLower();
    if (lowered.contains(QLatin1String("error")) || lowered.contains(QLatin1String("severe"))
        || lowered.contains(QLatin1String("exception")) || lowered.contains(QLatin1String("failed"))
        || lowered.contains(QStringLiteral("无法"))) {
        return QStringLiteral("error");
    }
    if (lowered.contains(QLatin1String("warn")) || lowered.contains(QStringLiteral("警告")))
        return QStringLiteral("warn");
    if (line.contains(QLatin1String("Done (")) || lowered.contains(QLatin1String("success"))
        || lowered.contains(QLatin1String("[mcsm]"))) {
        return QStringLiteral("success");
    }
    if (lowered.contains(QLatin1String("joined the game"))
        || lowered.contains(QLatin1String("left the game"))) {
        return QStringLiteral("player");
    }
    return QStringLiteral("info");
}

void LogView::appendLine(const QString &line)
{
    if (line.isEmpty())
        return;
    m_lines.append(line);
    while (m_lines.size() > m_maxLines)
        m_lines.removeFirst();
    emit lineCountChanged(m_lines.size());

    if (!m_filter.isEmpty() && !line.contains(m_filter, Qt::CaseInsensitive))
        return;
    appendRaw(line);
    if (m_follow)
        scrollToBottomAnimated();
}

void LogView::appendLines(const QStringList &lines)
{
    for (const QString &line : lines)
        appendLine(line);
}

void LogView::setLogLines(const QStringList &lines)
{
    m_lines = lines.toVector();
    while (m_lines.size() > m_maxLines)
        m_lines.removeFirst();
    rebuild();
    emit lineCountChanged(m_lines.size());
}

void LogView::clearLog()
{
    m_lines.clear();
    clear();
    emit lineCountChanged(0);
}

void LogView::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    rebuild();
}

void LogView::rebuild()
{
    clear();
    for (const QString &line : m_lines) {
        if (!m_filter.isEmpty() && !line.contains(m_filter, Qt::CaseInsensitive))
            continue;
        appendRaw(line);
    }
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void LogView::appendRaw(const QString &line)
{
    const Palette &p = ThemeManager::instance()->palette();
    const QString level = classify(line);

    QColor color = p.text;
    if (level == QLatin1String("error"))
        color = p.danger;
    else if (level == QLatin1String("warn"))
        color = p.warning;
    else if (level == QLatin1String("success"))
        color = p.success;
    else if (level == QLatin1String("player"))
        color = p.blue;
    else
        color = p.textMuted;

    QTextCharFormat format;
    format.setForeground(color);
    if (level == QLatin1String("error"))
        format.setFontWeight(QFont::DemiBold);

    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(line + QLatin1Char('\n'), format);
}

QString LogView::plainLog() const
{
    return m_lines.join(QLatin1Char('\n'));
}

void LogView::scrollToBottomAnimated()
{
    QScrollBar *bar = verticalScrollBar();
    if (bar->value() >= bar->maximum() - 4) {
        if (!ThemeManager::instance()->animationsEnabled()) {
            bar->setValue(bar->maximum());
            return;
        }
        if (!m_scrollAnimation) {
            m_scrollAnimation = new QPropertyAnimation(bar, "value", this);
            m_scrollAnimation->setEasingCurve(Easing::standard());
            m_scrollAnimation->setDuration(Easing::fast());
        }
        m_scrollAnimation->stop();
        m_scrollAnimation->setStartValue(bar->value());
        m_scrollAnimation->setEndValue(bar->maximum());
        m_scrollAnimation->start();
    }
}

} // namespace mcsm
