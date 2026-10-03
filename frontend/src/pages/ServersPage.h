#pragma once

#include "model/ServerTypes.h"
#include "widgets/Common.h"

class QLabel;
class QListWidget;

namespace mcsm {

class LogView;
class Chip;

class ServersPage : public PageBase
{
    Q_OBJECT

public:
    explicit ServersPage(QWidget *parent = nullptr);

    void onActivated() override;
    void onServerSelectionChanged(const QString &serverId) override;

signals:
    void createServerRequested();
    void openConsoleRequested(const QString &serverId);
    void openConfigRequested(const QString &serverId);
    void openBackupRequested(const QString &serverId);
    void openPluginRequested(const QString &serverId);

private slots:
    void onListSelectionChanged();
    void onServersChanged();

private:
    QWidget *buildListCard();
    QWidget *buildDetailCard();
    void rebuildList();
    void loadDetail(const QString &serverId);
    void applyDetail(const ServerInfo &info);
    void setBusy(bool busy, const QString &message = QString());
    void showStartFailure(const QString &message, const QString &hint, const QStringList &logTail);
    void requestStart(const QString &serverId);
    void requestStop(const QString &serverId, bool force);
    void refreshLogPreview();
    void updateActionButtons(const ServerInfo &info);

    QListWidget *m_list = nullptr;
    QLabel *m_emptyHint = nullptr;

    StatusDot *m_statusDot = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    Chip *m_typeChip = nullptr;
    Chip *m_versionChip = nullptr;
    Chip *m_portChip = nullptr;
    Chip *m_javaChip = nullptr;

    CardFrame *m_errorBanner = nullptr;
    QLabel *m_errorText = nullptr;
    QLabel *m_errorHint = nullptr;

    GradientButton *m_primaryButton = nullptr;
    GradientButton *m_restartButton = nullptr;
    GradientButton *m_killButton = nullptr;
    GradientButton *m_consoleButton = nullptr;
    GradientButton *m_configButton = nullptr;
    GradientButton *m_backupButton = nullptr;
    GradientButton *m_pluginButton = nullptr;

    QLabel *m_valuePort = nullptr;
    QLabel *m_valueMemory = nullptr;
    QLabel *m_valueJava = nullptr;
    QLabel *m_valueContainer = nullptr;
    QLabel *m_valueDirectory = nullptr;
    QLabel *m_valueStarted = nullptr;
    QLabel *m_valueSchedule = nullptr;

    LogView *m_logPreview = nullptr;
    QLabel *m_busyLabel = nullptr;

    ServerInfo m_current;
    bool m_busy = false;
};

} // namespace mcsm
