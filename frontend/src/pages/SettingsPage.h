#pragma once

#include <QJsonObject>
#include <QVector>

#include "widgets/Common.h"

class QLabel;
class QLineEdit;
class QSlider;
class QTimer;
class QComboBox;
class QFontComboBox;
class QStackedWidget;
class QListWidget;

namespace mcsm {

class PluginExtensionsPage;

/// Settings with their own left hand menu. The main navigation is hidden while
/// this page is visible, so the sections live here instead.
class SettingsPage : public PageBase
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

    void onActivated() override;
    /// Switches to a section by name: appearance | plugins | runtime | experimental.
    void showSection(const QString &name);

signals:
    /// Emitted by the "返回主页" entry in the settings menu.
    void backRequested();

private:
    QWidget *buildNavCard();
    QWidget *buildAppearancePage();
    QWidget *buildPluginPage();
    QWidget *buildRuntimePage();
    QWidget *buildExperimentalPage();

    QWidget *buildAppearanceCard();
    QWidget *buildBackendCard();
    QWidget *buildJdkCard();
    QWidget *buildEnvironmentCard();
    QWidget *buildAboutCard();

    void setSection(int index);
    void updateNavState();
    void refreshJdkList();
    /// Enables / disables the host JDK card depending on the experimental switch.
    void updateJdkSection();
    void detectEnvironment();
    /// Writes the doctor result into the environment card. Connected exactly once
    /// in the constructor: connecting per page activation leaked a connection for
    /// every visit, so each environment update ran the handler N times.
    void applyDoctor(const QJsonObject &doctor);
    void browseBackend();
    void applyDataHome(const QString &path);
    void openPath(const QString &path);
    void updateSpeedLabel(int value);
    void applyFontPreset(int index);
    void syncFontControls();

    QVector<GradientButton *> m_navButtons;
    PluginExtensionsPage *m_pluginsPage = nullptr;
    QStackedWidget *m_stack = nullptr;
    ToggleSwitch *m_experimentalSwitch = nullptr;
    int m_section = 0;

    SegmentedControl *m_themeSwitch = nullptr;
    SegmentedControl *m_accentSwitch = nullptr;
    ToggleSwitch *m_animations = nullptr;
    QSlider *m_speed = nullptr;
    QLabel *m_speedLabel = nullptr;
    QTimer *m_speedSaveTimer = nullptr;
    QComboBox *m_fontPreset = nullptr;
    QFontComboBox *m_fontCustom = nullptr;
    QLabel *m_fontPreview = nullptr;

    QLineEdit *m_backendPath = nullptr;
    QLineEdit *m_homePath = nullptr;
    QLabel *m_backendStatus = nullptr;

    QLabel *m_dockerValue = nullptr;
    QLabel *m_dockerHint = nullptr;
    QLabel *m_javaValue = nullptr;
    QLabel *m_javaHint = nullptr;
    QLabel *m_toolsValue = nullptr;
    QLabel *m_toolsHint = nullptr;

    QLabel *m_aboutHome = nullptr;
    QLabel *m_aboutLog = nullptr;
    QLabel *m_aboutVersion = nullptr;

    QListWidget *m_jdkList = nullptr;
    QLabel *m_jdkHint = nullptr;
    QLabel *m_jdkGateHint = nullptr;
    QLabel *m_jdkExplain = nullptr;
    GradientButton *m_jdkScan = nullptr;
    bool m_scanning = false;
    bool m_jdkScanned = false;
    bool m_syncingAppearance = false;
};

} // namespace mcsm
