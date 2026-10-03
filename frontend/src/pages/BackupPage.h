#pragma once

#include "widgets/Common.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

namespace mcsm {

class BackupPage : public PageBase
{
    Q_OBJECT

public:
    explicit BackupPage(QWidget *parent = nullptr);

    void onActivated() override;
    void onServerSelectionChanged(const QString &serverId) override;

private:
    QWidget *buildScheduleCard();
    QWidget *buildCreateCard();
    QWidget *buildListCard();

    void loadBackups(const QString &serverId);
    void applySchedule(const QJsonObject &schedule);
    void saveSchedule();
    void createBackup();
    void restoreBackup(const QString &name);
    void deleteBackup(const QString &name);
    void setBusy(bool busy, const QString &message = QString());

    ServerSelector *m_selector = nullptr;
    ToggleSwitch *m_scheduleEnabled = nullptr;
    QSpinBox *m_interval = nullptr;
    QSpinBox *m_keep = nullptr;
    ToggleSwitch *m_includePlugins = nullptr;
    ToggleSwitch *m_saveBefore = nullptr;
    QLabel *m_nextDueLabel = nullptr;
    QLabel *m_statsLabel = nullptr;

    QLineEdit *m_noteEdit = nullptr;
    ToggleSwitch *m_createPlugins = nullptr;

    QListWidget *m_list = nullptr;
    QLabel *m_emptyHint = nullptr;
    QString m_serverId;
    bool m_busy = false;
};

} // namespace mcsm
