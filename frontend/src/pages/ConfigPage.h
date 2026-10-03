#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

#include "widgets/Common.h"

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

namespace mcsm {

class AnimatedStack;

class ConfigPage : public PageBase
{
    Q_OBJECT

public:
    explicit ConfigPage(QWidget *parent = nullptr);

    void onActivated() override;
    void onServerSelectionChanged(const QString &serverId) override;

private:
    QWidget *buildQuickPane();
    QWidget *buildExpertPane();
    QWidget *buildHistoryCard();

    void loadSchema();
    void buildFields(const QJsonArray &fields);
    void loadServer(const QString &serverId);
    void applyValues(const QMap<QString, QString> &values);
    void saveQuick();
    void saveExpert();
    void validateExpert();
    void loadHistory(const QString &serverId);
    void rollbackTo(const QString &backupId);
    void setStatus(const QString &message, bool error = false);
    void setBusy(bool busy);

    QString editorValue(const QString &key) const;
    void setEditorValue(const QString &key, const QString &value);

    ServerSelector *m_selector = nullptr;
    SegmentedControl *m_modeSwitch = nullptr;
    AnimatedStack *m_stack = nullptr;

    QScrollArea *m_quickArea = nullptr;
    QWidget *m_quickContainer = nullptr;
    QVBoxLayout *m_quickLayout = nullptr;
    QHash<QString, QWidget *> m_editors;
    QHash<QString, QString> m_defaults;
    QVector<QString> m_advancedKeys;
    ToggleSwitch *m_advancedSwitch = nullptr;

    QPlainTextEdit *m_expertEdit = nullptr;
    QLabel *m_issuesLabel = nullptr;

    GradientButton *m_saveButton = nullptr;
    GradientButton *m_resetButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_pathLabel = nullptr;

    QListWidget *m_historyList = nullptr;
    QLabel *m_historyHint = nullptr;

    QString m_serverId;
    QJsonObject m_schemaValues;
    QMap<QString, QString> m_originalValues;
    bool m_busy = false;
};

} // namespace mcsm
