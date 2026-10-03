#pragma once

#include <QJsonObject>

#include "widgets/Common.h"

class QLabel;
class QListWidget;
class QProgressBar;

namespace mcsm {

class DashboardPage : public PageBase
{
    Q_OBJECT

public:
    explicit DashboardPage(QWidget *parent = nullptr);

    void onActivated() override;
    void refresh();

signals:
    void createServerRequested();
    void openServerRequested(const QString &serverId);

private slots:
    void onActivity(const QString &message, const QString &level);
    void onServersChanged();

private:
    QWidget *buildHero();
    QWidget *buildStats();
    QWidget *buildEnvironmentCard();
    QWidget *buildActivityCard();
    void applyDoctor(const QJsonObject &data, const QStringList &warnings);
    QString healthText() const;

    StatCard *m_totalCard = nullptr;
    StatCard *m_runningCard = nullptr;
    StatCard *m_errorCard = nullptr;
    StatCard *m_backupCard = nullptr;

    QLabel *m_dockerValue = nullptr;
    QLabel *m_dockerHint = nullptr;
    QLabel *m_javaValue = nullptr;
    QLabel *m_javaHint = nullptr;
    QLabel *m_homeValue = nullptr;
    QLabel *m_homeHint = nullptr;
    QProgressBar *m_diskBar = nullptr;
    QListWidget *m_activityList = nullptr;
    QLabel *m_emptyHint = nullptr;
};

} // namespace mcsm
