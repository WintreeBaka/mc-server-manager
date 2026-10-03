#pragma once

#include <QJsonObject>

#include "widgets/Common.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QComboBox;

namespace mcsm {

class PluginPage : public PageBase
{
    Q_OBJECT

public:
    explicit PluginPage(QWidget *parent = nullptr);

    void onActivated() override;
    void onServerSelectionChanged(const QString &serverId) override;

private:
    QWidget *buildSearchCard();
    QWidget *buildResultsCard();
    QWidget *buildInstalledCard();

    void search();
    void installPlugin(const QJsonObject &hit);
    void loadInstalled(const QString &serverId);
    void togglePlugin(const QString &file, bool enabled);
    void removePlugin(const QString &file);
    void setBusy(bool busy, const QString &message = QString());

    ServerSelector *m_selector = nullptr;
    QComboBox *m_source = nullptr;
    QLineEdit *m_query = nullptr;
    QLabel *m_statusLabel = nullptr;
    QListWidget *m_results = nullptr;
    QListWidget *m_installed = nullptr;
    QLabel *m_installedHint = nullptr;
    QString m_serverId;
    bool m_busy = false;
};

} // namespace mcsm
