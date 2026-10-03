#pragma once

#include <QPlainTextEdit>
#include <QVector>

class QPropertyAnimation;

namespace mcsm {

/// Console view with level colouring, filtering and smooth auto scroll.
class LogView : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit LogView(QWidget *parent = nullptr);

    void appendLine(const QString &line);
    void appendLines(const QStringList &lines);
    void setLogLines(const QStringList &lines);
    void clearLog();

    void setFilter(const QString &filter);
    QString filter() const { return m_filter; }
    void setMaxLines(int maxLines) { m_maxLines = qMax(50, maxLines); }
    int maxLines() const { return m_maxLines; }

    QStringList lines() const { return m_lines; }
    QString plainLog() const;
    int lineCount() const { return m_lines.size(); }

    void scrollToBottomAnimated();
    void setFollowOutput(bool follow) { m_follow = follow; }
    bool followOutput() const { return m_follow; }

signals:
    void lineCountChanged(int count);

private:
    void rebuild();
    void appendRaw(const QString &line);
    static QString classify(const QString &line);

    QVector<QString> m_lines;
    QString m_filter;
    int m_maxLines = 4000;
    bool m_follow = true;
    QPropertyAnimation *m_scrollAnimation = nullptr;
};

} // namespace mcsm
