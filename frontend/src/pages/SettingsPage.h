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

/// Settings with their own left hand menu. The main navigation is hidden while
/// this page is visible, so the sections live here instead.
class SettingsPage : public PageBase
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

    void onActivated() override;

signals:
    /// Emitted by the "返回主页" entry in the settings menu.
    void backRequested();

private:
    QWidget *buildNavCard();
    QWidget *buildAppearancePage();
    QWidget *buildRuntimePage();
    QWidget *buildExperimentalPage();

    QWidget *buildAppearanceCard();
    QWidget *buildBackendCard();
    QWidget *buildEnvironmentCard();
    QWidget *buildAboutCard();

    void setSection(int index);
    void updateNavState();
    void refreshJdkList();
    void detectEnvironment();
    void browseBackend();
    void applyDataHome(const QString &path);
    void openPath(const QString &path);
    void updateSpeedLabel(int value);
    void applyFontPreset(int index);
    void syncFontControls();

    QVector<GradientButton *> m_navButtons;
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
    bool m_scanning = false;
    bool m_syncingAppearance = false;
};

} // namespace mcsm
