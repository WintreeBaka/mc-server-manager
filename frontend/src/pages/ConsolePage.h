#pragma once

#include "widgets/Common.h"

class QLabel;
class QLineEdit;
class QProcess;
class QTimer;

namespace mcsm {

class LogView;
class Chip;

class ConsolePage : public PageBase
{
    Q_OBJECT

public:
    explicit ConsolePage(QWidget *parent = nullptr);
    ~ConsolePage() override;

    void onActivated() override;
    void onDeactivated() override;
    void onServerSelectionChanged(const QString &serverId) override;

signals:
    void serverSelected(const QString &serverId);

private:
    QWidget *buildConsoleCard();
    void startStream(const QString &serverId, int tail);
    void stopStream();
    void sendCommand(const QString &command);
    void loadHistory(const QString &serverId);
    /// Stops the log stream once the page has been left for a while, so a hidden
    /// page never keeps a `docker logs --follow` process (and its CPU) alive.
    void scheduleStreamRelease();

    ServerSelector *m_selector = nullptr;
    LogView *m_log = nullptr;
    QLineEdit *m_filter = nullptr;
    QLineEdit *m_command = nullptr;
    ToggleSwitch *m_followSwitch = nullptr;
    QLabel *m_statusLabel = nullptr;
    Chip *m_stateChip = nullptr;
    QProcess *m_stream = nullptr;
    QString m_streamServerId;
    QTimer *m_releaseTimer = nullptr;
};

} // namespace mcsm
