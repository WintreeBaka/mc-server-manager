#pragma once

#include <QDialog>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

#include "widgets/Common.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QSpinBox;
class QStackedWidget;

namespace mcsm {

class SegmentedControl;
class ToggleSwitch;

/// One click auto setup dialog with a manual fallback mode.
class CreateServerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateServerDialog(QWidget *parent = nullptr);

signals:
    void serverCreated(const QString &serverId);

private:
    QWidget *buildAutoPane();
    QWidget *buildManualPane();
    QWidget *buildProgressArea();

    void loadVersions(const QString &type);
    void loadHostJdks();
    void browseJar();
    void startInstall();
    void setProgress(const QString &stage, int percent, const QString &detail);
    void setBusy(bool busy);
    void showError(const QString &message);
    void loadTypes();
    QString selectedVersion() const;

    SegmentedControl *m_modeSwitch = nullptr;
    QStackedWidget *m_stack = nullptr;

    QLineEdit *m_name = nullptr;
    QComboBox *m_type = nullptr;
    QComboBox *m_version = nullptr;
    QLabel *m_versionHint = nullptr;
    QSpinBox *m_port = nullptr;
    QComboBox *m_memory = nullptr;
    QLineEdit *m_levelName = nullptr;
    QComboBox *m_gamemode = nullptr;
    QComboBox *m_difficulty = nullptr;
    QSpinBox *m_maxPlayers = nullptr;
    ToggleSwitch *m_autoRestart = nullptr;
    ToggleSwitch *m_eula = nullptr;

    QComboBox *m_runtime = nullptr;      // docker | host (experimental)
    QComboBox *m_jdkSelect = nullptr;

    QLineEdit *m_manualName = nullptr;
    QLineEdit *m_manualJar = nullptr;
    QComboBox *m_manualType = nullptr;
    QComboBox *m_manualJava = nullptr;
    QSpinBox *m_manualPort = nullptr;
    QComboBox *m_manualMemory = nullptr;
    QLineEdit *m_manualLevel = nullptr;
    ToggleSwitch *m_manualEula = nullptr;

    QProgressBar *m_progress = nullptr;
    QLabel *m_stageLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    GradientButton *m_createButton = nullptr;
    GradientButton *m_cancelButton = nullptr;

    QJsonArray m_types;
    QJsonArray m_versions;
    QHash<QString, QJsonObject> m_versionMeta;
    QString m_currentType;
    bool m_busy = false;
};

} // namespace mcsm
