#include "pages/SettingsPage.h"

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "app/Easing.h"
#include "pages/PluginExtensionsPage.h"
#include "widgets/Chip.h"

namespace mcsm {
namespace {

QWidget *makeStatusRow(const QString &title, const QString &glyph, QLabel **value, QLabel **hint,
                       QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *icon = new QLabel(glyph, row);
    icon->setFixedSize(32, 32);
    icon->setAlignment(Qt::AlignCenter);
    const Palette &palette = ThemeManager::instance()->palette();
    icon->setStyleSheet(QStringLiteral("border-radius: 11px; background: %1; color: %2;")
                            .arg(palette.surfaceAlt.name(), palette.text.name()));

    auto *column = new QVBoxLayout();
    column->setSpacing(1);
    column->addWidget(makeLabel(title, QStringLiteral("caption"), row));
    *value = makeLabel(QStringLiteral("检测中…"), QStringLiteral("subtitle"), row);
    *hint = makeLabel(QString(), QStringLiteral("hint"), row);
    column->addWidget(*value);
    column->addWidget(*hint);

    layout->addWidget(icon, 0, Qt::AlignTop);
    layout->addLayout(column, 1);
    return row;
}

/// Wraps one or more cards into a scroll friendly page.
QWidget *wrapPage(const QVector<QWidget *> &cards, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    for (QWidget *card : cards)
        layout->addWidget(card);
    layout->addStretch(1);
    return page;
}

} // namespace

SettingsPage::SettingsPage(QWidget *parent)
    : PageBase(QStringLiteral("设置"),
               QStringLiteral("外观、运行环境与实验性功能都在这里调整"), parent)
{
    // The menu lives in the fixed side column: it must never scroll away with the
    // (often long) settings content, and 实验性功能 / 返回主页 stay pinned near the
    // bottom edge of the window.
    setSideColumn(buildNavCard());
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildAppearancePage());
    m_stack->addWidget(buildPluginPage());
    m_stack->addWidget(buildRuntimePage());
    m_stack->addWidget(buildExperimentalPage());
    body()->addWidget(m_stack, 1);

    AppContext *context = AppContext::instance();
    connect(context, &AppContext::themeChanged, this, [this]() {
        m_themeSwitch->setCurrentIndex(ThemeManager::instance()->isDark() ? 0 : 1, false);
    });
    connect(context, &AppContext::experimentalChanged, this, [this](bool) {
        updateNavState();
        updateJdkSection();
    });
    connect(context->backend(), &BackendClient::availabilityChanged, this, [this](bool available) {
        m_backendStatus->setText(available ? QStringLiteral("✔ 后端可用")
                                           : QStringLiteral("✕ 未找到 mcsm-cli"));
    });
    connect(context, &AppContext::environmentChanged, this, &SettingsPage::applyDoctor);

    setSection(0);
    updateNavState();
}

// ---------------------------------------------------------------- navigation --

QWidget *SettingsPage::buildNavCard()
{
    auto *card = new CardFrame(this);
    card->setFixedWidth(190);
    auto *layout = CardFrame::verticalLayout(card, 14, 8);

    const QStringList labels = {QStringLiteral("外观"), QStringLiteral("插件扩展"),
                                QStringLiteral("运行环境"), QStringLiteral("实验性功能")};
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new GradientButton(labels.at(i), card);
        button->setStyle(GradientButton::Outline);
        button->setCompact(true);
        connect(button, &QPushButton::clicked, this, [this, i]() { setSection(i); });
        layout->addWidget(button);
        m_navButtons.append(button);
    }
    layout->addStretch(1);

    auto *divider = new QFrame(card);
    divider->setProperty("role", "divider");
    divider->setFixedHeight(1);
    layout->addWidget(divider);

    auto *switchRow = new QWidget(card);
    auto *switchLayout = new QHBoxLayout(switchRow);
    switchLayout->setContentsMargins(0, 0, 0, 0);
    switchLayout->setSpacing(8);
    switchLayout->addWidget(makeLabel(QStringLiteral("实验性功能"), QStringLiteral("caption"), switchRow), 1);
    m_experimentalSwitch = new ToggleSwitch(switchRow);
    m_experimentalSwitch->setChecked(AppContext::instance()->experimentalEnabled());
    switchLayout->addWidget(m_experimentalSwitch);
    layout->addWidget(switchRow);

    connect(m_experimentalSwitch, &ToggleSwitch::toggled, this, [this](bool checked) {
        AppContext::instance()->setExperimentalEnabled(checked);
        AppContext::instance()->logActivity(
            checked ? QStringLiteral("已开启实验性功能") : QStringLiteral("已关闭实验性功能"),
            QStringLiteral("info"));
        if (!checked && m_section == 3)
            setSection(0);
        updateNavState();
        updateJdkSection();
    });

    auto *back = new GradientButton(QStringLiteral("返回主页"), card);
    back->setStyle(GradientButton::Outline);
    back->setCompact(true);
    connect(back, &QPushButton::clicked, this, &SettingsPage::backRequested);
    layout->addWidget(back);
    return card;
}

void SettingsPage::setSection(int index)
{
    if (index < 0 || index >= m_stack->count())
        return;
    if (index == 3 && !AppContext::instance()->experimentalEnabled())
        return;
    m_section = index;
    m_stack->setCurrentIndex(index);
    updateNavState();
    if (index == 2) {
        detectEnvironment();
        updateJdkSection();
        // fill the JDK list the first time so the card is not an empty box
        if (!m_jdkScanned && !m_scanning)
            refreshJdkList();
    } else if (index == 1 && m_pluginsPage) {
        m_pluginsPage->refresh();
    }
}

void SettingsPage::updateNavState()
{
    const bool experimental = AppContext::instance()->experimentalEnabled();
    if (m_navButtons.size() > 3)
        m_navButtons.at(3)->setVisible(experimental);
    for (int i = 0; i < m_navButtons.size(); ++i) {
        const bool active = i == m_section;
        m_navButtons.at(i)->setStyle(active ? GradientButton::Primary : GradientButton::Outline);
    }
}

// ------------------------------------------------------------------- pages ----

void SettingsPage::showSection(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("plugins") || key == QLatin1String("plugin")
        || key == QLatin1String("extensions"))
        setSection(1);
    else if (key == QLatin1String("runtime") || key == QLatin1String("environment"))
        setSection(2);
    else if (key == QLatin1String("experimental"))
        setSection(3);
    else
        setSection(0);
}

QWidget *SettingsPage::buildAppearancePage()
{
    return wrapPage({buildAppearanceCard()}, this);
}

QWidget *SettingsPage::buildRuntimePage()
{
    // the host JDK scanner used to live on the experimental page; it is a runtime
    // topic, so it now sits next to the backend / environment cards
    return wrapPage({buildBackendCard(), buildJdkCard(), buildEnvironmentCard(), buildAboutCard()},
                    this);
}

QWidget *SettingsPage::buildPluginPage()
{
    m_pluginsPage = new PluginExtensionsPage(this);
    return wrapPage({m_pluginsPage}, this);
}

QWidget *SettingsPage::buildExperimentalPage()
{
    // 实验性功能页面：内容暂时留空，等后续实验性能力确定后再填入。
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 14);
    layout->addWidget(makeSectionHeader(
        QStringLiteral("实验性功能"),
        QStringLiteral("这里预留给还在打磨中的能力，目前暂时为空"),
        nullptr, card));
    auto *placeholder = makeLabel(
        QStringLiteral("暂时没有实验性内容。\n\n"
                       "当前已开放的实验性能力是「本机 JDK」，它属于运行环境的一部分，"
                       "已经移动到「运行环境」页面：在那里可以扫描本机已安装的 JDK，"
                       "新建服务器时选择克隆一份直接运行（不经过 Docker 容器）。"),
        QStringLiteral("hint"), card);
    placeholder->setWordWrap(true);
    layout->addWidget(placeholder);
    return wrapPage({card}, this);
}

QWidget *SettingsPage::buildJdkCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 14);
    layout->addWidget(makeSectionHeader(
        QStringLiteral("本机 JDK（实验性）"),
        QStringLiteral("扫描这台电脑上已安装的 JDK，创建服务器时可克隆一份使用（不再依赖容器）"),
        nullptr, card));

    m_jdkGateHint = makeLabel(
        QStringLiteral("「本机 JDK」属于实验性功能：请先在左侧「实验性功能」中打开开关，这里才会显示扫描结果。"),
        QStringLiteral("hint"), card);
    m_jdkGateHint->setWordWrap(true);
    layout->addWidget(m_jdkGateHint);

    m_jdkExplain = makeLabel(
        QStringLiteral("开启后，新建服务器时可以选择「本机 JDK」：管理器会把所选 JDK 完整复制到该服务器目录下，"
                       "服务器将直接在这台电脑上运行（不经过 Docker 容器），控制台、RCON、备份等功能保持不变。"),
        QStringLiteral("hint"), card);
    m_jdkExplain->setWordWrap(true);
    layout->addWidget(m_jdkExplain);

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(10);
    m_jdkHint = makeLabel(QStringLiteral("点击「重新扫描」查看本机已安装的 JDK"), QStringLiteral("hint"),
                          card);
    m_jdkScan = new GradientButton(QStringLiteral("重新扫描"), card);
    m_jdkScan->setStyle(GradientButton::Outline);
    m_jdkScan->setCompact(true);
    toolbar->addWidget(m_jdkHint, 1);
    toolbar->addWidget(m_jdkScan);
    layout->addLayout(toolbar);

    m_jdkList = new QListWidget(card);
    m_jdkList->setFrameShape(QFrame::NoFrame);
    m_jdkList->setSpacing(6);
    m_jdkList->setMinimumHeight(150);
    m_jdkList->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; }"));
    layout->addWidget(m_jdkList, 1);

    connect(m_jdkScan, &QPushButton::clicked, this, &SettingsPage::refreshJdkList);
    updateJdkSection();
    return card;
}

void SettingsPage::updateJdkSection()
{
    if (!m_jdkList)
        return;
    const bool enabled = AppContext::instance()->experimentalEnabled();
    m_jdkGateHint->setVisible(!enabled);
    m_jdkExplain->setVisible(enabled);
    m_jdkHint->setVisible(enabled);
    m_jdkScan->setVisible(enabled);
    m_jdkList->setVisible(enabled);
}

void SettingsPage::refreshJdkList()
{
    if (m_scanning || !m_jdkList)
        return;
    m_scanning = true;
    m_jdkHint->setText(QStringLiteral("正在扫描本机 JDK…"));
    m_jdkList->clear();

    AppContext::instance()->backend()->request(
        {QStringLiteral("java"), QStringLiteral("--action"), QStringLiteral("scan")}, this,
        [this](const Reply &reply) {
            m_scanning = false;
            m_jdkScanned = true;
            if (!reply.ok) {
                m_jdkHint->setText(QStringLiteral("扫描失败：%1").arg(reply.errorText()));
                return;
            }
            const QJsonArray jdks = reply.data.value(QStringLiteral("jdks")).toArray();
            m_jdkHint->setText(jdks.isEmpty() ? QStringLiteral("没有在本机找到 JDK")
                                              : QStringLiteral("找到 %1 个 JDK").arg(jdks.size()));
            for (const QJsonValue &value : jdks) {
                const QJsonObject jdk = value.toObject();
                auto *item = new QListWidgetItem(m_jdkList);
                auto *widget = new QWidget(m_jdkList);
                auto *layout = new QVBoxLayout(widget);
                layout->setContentsMargins(10, 8, 10, 8);
                layout->setSpacing(3);

                auto *titleRow = new QHBoxLayout();
                titleRow->setSpacing(8);
                auto *chip = new Chip(QStringLiteral("JDK %1").arg(jdk.value(QStringLiteral("major")).toInt()),
                                      widget);
                chip->setTone(Chip::Pink);
                chip->setCompact(true);
                titleRow->addWidget(chip);
                titleRow->addWidget(makeLabel(jdk.value(QStringLiteral("version")).toString(),
                                              QStringLiteral("subtitle"), widget));
                titleRow->addWidget(makeLabel(jdk.value(QStringLiteral("vendor")).toString(),
                                              QStringLiteral("caption"), widget));
                titleRow->addStretch(1);
                titleRow->addWidget(makeLabel(jdk.value(QStringLiteral("sizeText")).toString(),
                                              QStringLiteral("caption"), widget));
                layout->addLayout(titleRow);

                auto *path = makeLabel(jdk.value(QStringLiteral("home")).toString(),
                                       QStringLiteral("hint"), widget);
                path->setWordWrap(false);
                path->setToolTip(jdk.value(QStringLiteral("javaPath")).toString());
                layout->addWidget(path);

                item->setSizeHint(widget->sizeHint());
                m_jdkList->setItemWidget(item, widget);
            }
        });
}

// ------------------------------------------------------------- appearance page --

QWidget *SettingsPage::buildAppearanceCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 14);
    layout->addWidget(makeSectionHeader(QStringLiteral("外观"),
                                        QStringLiteral("马卡龙粉 - 蓝配色，渐变/纯色与字体都可自由切换"), nullptr,
                                        card));

    auto *row = new QHBoxLayout();
    row->setSpacing(24);

    auto *themeColumn = new QVBoxLayout();
    themeColumn->setSpacing(6);
    themeColumn->addWidget(makeLabel(QStringLiteral("主题模式"), QStringLiteral("caption"), card));
    m_themeSwitch = new SegmentedControl(card);
    m_themeSwitch->setItems({QStringLiteral("暗黑主题"), QStringLiteral("亮色主题")});
    m_themeSwitch->setCurrentIndex(ThemeManager::instance()->isDark() ? 0 : 1, false);
    themeColumn->addWidget(m_themeSwitch);
    themeColumn->addStretch(1);
    row->addLayout(themeColumn, 3);

    auto *accentColumn = new QVBoxLayout();
    accentColumn->setSpacing(6);
    accentColumn->addWidget(makeLabel(QStringLiteral("色彩风格"), QStringLiteral("caption"), card));
    m_accentSwitch = new SegmentedControl(card);
    m_accentSwitch->setItems({QStringLiteral("渐变"), QStringLiteral("纯色简洁")});
    m_accentSwitch->setCurrentIndex(
        ThemeManager::instance()->accentStyle() == ThemeManager::AccentStyle::Flat ? 1 : 0, false);
    accentColumn->addWidget(m_accentSwitch);
    accentColumn->addWidget(makeLabel(QStringLiteral("纯色=保留同样的颜色但去掉渐变"), QStringLiteral("hint"),
                                      card));
    accentColumn->addStretch(1);
    row->addLayout(accentColumn, 3);
    layout->addLayout(row);

    auto *animationRow = new QHBoxLayout();
    animationRow->setSpacing(24);
    auto *animationColumn = new QVBoxLayout();
    animationColumn->setSpacing(6);
    auto *animationHeader = new QHBoxLayout();
    animationHeader->setSpacing(10);
    animationHeader->addWidget(makeLabel(QStringLiteral("过渡动画"), QStringLiteral("caption"), card));
    m_animations = new ToggleSwitch(card);
    m_animations->setChecked(AppContext::instance()->animationsEnabled());
    m_animations->setOnText(QStringLiteral("开启"), QStringLiteral("关闭"));
    animationHeader->addWidget(m_animations);
    animationHeader->addStretch(1);
    animationColumn->addLayout(animationHeader);

    auto *speedRow = new QHBoxLayout();
    speedRow->setSpacing(10);
    m_speed = new QSlider(Qt::Horizontal, card);
    m_speed->setRange(50, 200);
    m_speed->setValue(int(AppContext::instance()->animationScale() * 100));
    m_speedLabel = makeLabel(QStringLiteral("标准速度"), QStringLiteral("hint"), card);
    speedRow->addWidget(m_speed, 1);
    speedRow->addWidget(m_speedLabel);
    animationColumn->addLayout(speedRow);
    animationColumn->addWidget(makeLabel(QStringLiteral("所有动画都使用贝塞尔曲线缓动，数值越小越快"),
                                         QStringLiteral("hint"), card));
    animationColumn->addStretch(1);
    animationRow->addLayout(animationColumn, 4);
    animationRow->addStretch(1);
    layout->addLayout(animationRow);

    auto *fontRow = new QHBoxLayout();
    fontRow->setSpacing(10);
    fontRow->addWidget(makeLabel(QStringLiteral("界面字体"), QStringLiteral("caption"), card));
    m_fontPreset = new QComboBox(card);
    m_fontPreset->addItem(QStringLiteral("系统默认"), QString());
    m_fontPreset->addItem(QStringLiteral("HarmonyOS Sans SC"), QStringLiteral("HarmonyOS Sans SC"));
    m_fontPreset->addItem(QStringLiteral("Noto Sans SC"), QStringLiteral("Noto Sans SC"));
    m_fontPreset->addItem(QStringLiteral("思源黑体"), QStringLiteral("Source Han Sans SC"));
    m_fontPreset->addItem(QStringLiteral("微软雅黑 UI"), QStringLiteral("Microsoft YaHei UI"));
    m_fontPreset->addItem(QStringLiteral("等线"), QStringLiteral("DengXian"));
    m_fontPreset->addItem(QStringLiteral("自定义…"), QStringLiteral("__custom__"));
    m_fontPreset->setMinimumWidth(180);
    m_fontCustom = new QFontComboBox(card);
    m_fontCustom->setMinimumWidth(220);
    m_fontCustom->setVisible(false);
    m_fontPreview = makeLabel(QStringLiteral("字体预览：我的世界服务器管理器 Aa Bb 0123"),
                              QStringLiteral("caption"), card);
    fontRow->addWidget(m_fontPreset);
    fontRow->addWidget(m_fontCustom);
    fontRow->addWidget(m_fontPreview, 1);
    layout->addLayout(fontRow);

    connect(m_themeSwitch, &SegmentedControl::currentChanged, this, [this](int index) {
        if (m_syncingAppearance)
            return;
        ThemeManager::instance()->setDark(index == 0);
        AppContext::instance()->logActivity(
            QStringLiteral("已切换到%1").arg(index == 0 ? QStringLiteral("暗黑主题")
                                                      : QStringLiteral("亮色主题")),
            QStringLiteral("info"));
    });
    connect(m_accentSwitch, &SegmentedControl::currentChanged, this, [this](int index) {
        if (m_syncingAppearance)
            return;
        AppContext::instance()->setAccentStyle(index == 1 ? ThemeManager::AccentStyle::Flat
                                                          : ThemeManager::AccentStyle::Gradient);
        AppContext::instance()->logActivity(
            QStringLiteral("色彩风格已切换为%1").arg(index == 1 ? QStringLiteral("纯色")
                                                              : QStringLiteral("渐变")),
            QStringLiteral("info"));
    });
    connect(m_fontPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsPage::applyFontPreset);
    connect(m_fontCustom, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        if (m_syncingAppearance || !m_fontCustom->isVisible())
            return;
        AppContext::instance()->setFontFamily(font.family());
        syncFontControls();
    });
    connect(m_animations, &ToggleSwitch::toggled, this, [this](bool checked) {
        AppContext::instance()->setAnimationsEnabled(checked);
    });
    m_speedSaveTimer = new QTimer(this);
    m_speedSaveTimer->setSingleShot(true);
    m_speedSaveTimer->setInterval(300);
    connect(m_speed, &QSlider::valueChanged, this, [this](int value) {
        ThemeManager::instance()->setAnimationScale(double(value) / 100.0);
        updateSpeedLabel(value);
        m_speedSaveTimer->start();
    });
    connect(m_speedSaveTimer, &QTimer::timeout, this, [this]() {
        AppContext::instance()->setAnimationScale(double(m_speed->value()) / 100.0);
    });
    updateSpeedLabel(m_speed->value());
    syncFontControls();
    return card;
}

void SettingsPage::syncFontControls()
{
    if (!m_fontPreset || !m_fontCustom)
        return;
    const QString current = ThemeManager::instance()->fontFamily();
    m_syncingAppearance = true;

    int index = 0;
    for (int i = 0; i < m_fontPreset->count(); ++i) {
        const QString value = m_fontPreset->itemData(i).toString();
        if (value == QLatin1String("__custom__"))
            continue;
        if (value == current && !current.isEmpty())
            index = i;
    }
    if (index == 0 && !current.isEmpty())
        index = m_fontPreset->count() - 1;
    m_fontPreset->setCurrentIndex(index);
    m_fontCustom->setVisible(m_fontPreset->itemData(index).toString() == QLatin1String("__custom__"));
    if (!current.isEmpty())
        m_fontCustom->setCurrentFont(QFont(current));

    if (m_fontPreview) {
        QFont preview = m_fontPreview->font();
        preview.setFamilies(ThemeManager::instance()->fontFamilies());
        m_fontPreview->setFont(preview);
    }
    m_syncingAppearance = false;
}

void SettingsPage::applyFontPreset(int index)
{
    if (m_syncingAppearance || index < 0 || !m_fontPreset)
        return;
    const QString value = m_fontPreset->itemData(index).toString();
    if (value == QLatin1String("__custom__")) {
        m_fontCustom->setVisible(true);
        AppContext::instance()->setFontFamily(m_fontCustom->currentFont().family());
        syncFontControls();
        return;
    }
    m_fontCustom->setVisible(false);
    AppContext::instance()->setFontFamily(value);
    syncFontControls();
}

void SettingsPage::updateSpeedLabel(int value)
{
    QString text = QStringLiteral("标准速度");
    if (value <= 70)
        text = QStringLiteral("很快");
    else if (value <= 110)
        text = QStringLiteral("标准速度");
    else if (value <= 160)
        text = QStringLiteral("偏慢");
    else
        text = QStringLiteral("最柔和");
    m_speedLabel->setText(QStringLiteral("%1 (%2%)").arg(text).arg(value));
}

// ---------------------------------------------------------------- runtime page --

QWidget *SettingsPage::buildBackendCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 14);
    layout->addWidget(makeSectionHeader(QStringLiteral("后端与数据目录"),
                                        QStringLiteral("界面只负责展示，所有操作都由 mcsm-cli 执行"), nullptr,
                                        card));

    auto *backendRow = new QHBoxLayout();
    backendRow->setSpacing(10);
    m_backendPath = new QLineEdit(card);
    m_backendPath->setText(AppContext::instance()->backend()->executable());
    m_backendPath->setPlaceholderText(QStringLiteral("mcsm-cli 可执行文件路径"));
    auto *browse = new GradientButton(QStringLiteral("浏览…"), card);
    browse->setStyle(GradientButton::Outline);
    browse->setCompact(true);
    auto *autodetect = new GradientButton(QStringLiteral("自动检测"), card);
    autodetect->setStyle(GradientButton::Outline);
    autodetect->setCompact(true);
    auto *applyBackend = new GradientButton(QStringLiteral("应用"), card);
    applyBackend->setCompact(true);
    backendRow->addWidget(makeLabel(QStringLiteral("后端程序"), QStringLiteral("caption"), card));
    backendRow->addWidget(m_backendPath, 1);
    backendRow->addWidget(browse);
    backendRow->addWidget(autodetect);
    backendRow->addWidget(applyBackend);
    layout->addLayout(backendRow);

    auto *homeRow = new QHBoxLayout();
    homeRow->setSpacing(10);
    m_homePath = new QLineEdit(card);
    m_homePath->setText(AppContext::instance()->backend()->dataHome());
    m_homePath->setPlaceholderText(QStringLiteral("留空表示使用默认数据目录"));
    auto *applyHome = new GradientButton(QStringLiteral("应用"), card);
    applyHome->setCompact(true);
    auto *openHome = new GradientButton(QStringLiteral("打开目录"), card);
    openHome->setStyle(GradientButton::Outline);
    openHome->setCompact(true);
    homeRow->addWidget(makeLabel(QStringLiteral("数据目录"), QStringLiteral("caption"), card));
    homeRow->addWidget(m_homePath, 1);
    homeRow->addWidget(applyHome);
    homeRow->addWidget(openHome);
    layout->addLayout(homeRow);

    m_backendStatus = makeLabel(AppContext::instance()->backend()->isConfigured()
                                    ? QStringLiteral("✔ 后端可用")
                                    : QStringLiteral("✕ 未找到 mcsm-cli"),
                                QStringLiteral("hint"), card);
    layout->addWidget(m_backendStatus);

    connect(browse, &QPushButton::clicked, this, &SettingsPage::browseBackend);
    connect(autodetect, &QPushButton::clicked, this, [this]() {
        const QString found = BackendClient::locateBackend();
        if (found.isEmpty()) {
            m_backendStatus->setText(QStringLiteral("✕ 没有在常见位置找到 mcsm-cli"));
            return;
        }
        m_backendPath->setText(found);
        AppContext::instance()->backend()->setExecutable(found);
        m_backendStatus->setText(QStringLiteral("✔ 已使用 %1").arg(found));
    });
    connect(applyBackend, &QPushButton::clicked, this, [this]() {
        AppContext::instance()->backend()->setExecutable(m_backendPath->text().trimmed());
        AppContext::instance()->logActivity(QStringLiteral("后端路径已更新"), QStringLiteral("info"));
    });
    connect(applyHome, &QPushButton::clicked, this, [this]() {
        applyDataHome(m_homePath->text().trimmed());
    });
    connect(openHome, &QPushButton::clicked, this, [this]() {
        openPath(m_homePath->text().isEmpty() ? AppContext::instance()->dataHome()
                                              : m_homePath->text());
    });
    return card;
}

QWidget *SettingsPage::buildEnvironmentCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 14);

    auto *headerRow = new QWidget(card);
    auto *headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    headerLayout->addWidget(makeSectionHeader(QStringLiteral("运行环境"),
                                              QStringLiteral("Docker、JDK 镜像与辅助工具"), nullptr,
                                              headerRow),
                            1);
    auto *detect = new GradientButton(QStringLiteral("重新检测"), headerRow);
    detect->setStyle(GradientButton::Outline);
    detect->setCompact(true);
    headerLayout->addWidget(detect, 0, Qt::AlignVCenter);
    layout->addWidget(headerRow);

    layout->addWidget(makeStatusRow(QStringLiteral("Docker 守护进程"), QStringLiteral("◈"),
                                    &m_dockerValue, &m_dockerHint, card));
    layout->addWidget(makeStatusRow(QStringLiteral("容器化 JDK 镜像"), QStringLiteral("☕"),
                                    &m_javaValue, &m_javaHint, card));
    layout->addWidget(makeStatusRow(QStringLiteral("辅助工具"), QStringLiteral("⛭"),
                                    &m_toolsValue, &m_toolsHint, card));

    connect(detect, &QPushButton::clicked, this, &SettingsPage::detectEnvironment);
    return card;
}

QWidget *SettingsPage::buildAboutCard()
{
    auto *card = new CardFrame(this);
    auto *layout = CardFrame::verticalLayout(card, 20, 10);
    layout->addWidget(makeSectionHeader(QStringLiteral("关于"),
                                        QStringLiteral("Qt 6 + C++ 桌面端，Docker 化的服务端运行环境"),
                                        nullptr, card));
    m_aboutVersion = makeLabel(QStringLiteral("McServerManager 1.0.0"), QStringLiteral("caption"), card);
    m_aboutHome = makeLabel(QStringLiteral("数据目录：--"), QStringLiteral("hint"), card);
    m_aboutLog = makeLabel(QStringLiteral("后端日志：--"), QStringLiteral("hint"), card);
    layout->addWidget(m_aboutVersion);
    layout->addWidget(m_aboutHome);
    layout->addWidget(m_aboutLog);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(8);
    auto *openHome = new GradientButton(QStringLiteral("打开数据目录"), card);
    openHome->setStyle(GradientButton::Outline);
    openHome->setCompact(true);
    auto *openLog = new GradientButton(QStringLiteral("打开日志"), card);
    openLog->setStyle(GradientButton::Outline);
    openLog->setCompact(true);
    actions->addWidget(openHome);
    actions->addWidget(openLog);
    actions->addStretch(1);
    layout->addLayout(actions);

    connect(openHome, &QPushButton::clicked, this, [this]() {
        openPath(AppContext::instance()->dataHome());
    });
    connect(openLog, &QPushButton::clicked, this, [this]() {
        const QString home = AppContext::instance()->dataHome();
        openPath(home.isEmpty() ? QString() : home + QStringLiteral("/logs"));
    });
    return card;
}

void SettingsPage::browseBackend()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 mcsm-cli"), QString(),
#ifdef Q_OS_WIN
                                                     QStringLiteral("可执行文件 (*.exe);;所有文件 (*)")
#else
                                                     QStringLiteral("所有文件 (*)")
#endif
    );
    if (path.isEmpty())
        return;
    m_backendPath->setText(path);
    AppContext::instance()->backend()->setExecutable(path);
    m_backendStatus->setText(QStringLiteral("✔ 已使用 %1").arg(path));
    detectEnvironment();
}

void SettingsPage::applyDataHome(const QString &path)
{
    AppContext::instance()->setDataHome(path);
    m_homePath->setText(path);
    AppContext::instance()->logActivity(
        path.isEmpty() ? QStringLiteral("已恢复默认数据目录")
                       : QStringLiteral("数据目录已切换到 %1").arg(path),
        QStringLiteral("info"));
    detectEnvironment();
}

void SettingsPage::openPath(const QString &path)
{
    if (path.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void SettingsPage::detectEnvironment()
{
    // Rate limited inside AppContext: `doctor` shells out to docker, so running
    // it on every page activation made rapid switching expensive.
    AppContext *context = AppContext::instance();
    context->refreshEnvironment();
    if (!context->environment().isEmpty())
        applyDoctor(context->environment());
}

void SettingsPage::applyDoctor(const QJsonObject &doctor)
{
    const Palette &palette = ThemeManager::instance()->palette();
    const QJsonObject docker = doctor.value(QStringLiteral("docker")).toObject();
    const bool running = docker.value(QStringLiteral("daemonRunning")).toBool();
    m_dockerValue->setText(running ? QStringLiteral("运行中") : QStringLiteral("未运行"));
    m_dockerValue->setStyleSheet(QStringLiteral("color: %1;")
                                     .arg((running ? palette.success : palette.danger).name()));
    m_dockerHint->setText(running ? QStringLiteral("引擎版本 %1")
                                        .arg(docker.value(QStringLiteral("version")).toString())
                                  : docker.value(QStringLiteral("error")).toString());

    const QJsonObject java = doctor.value(QStringLiteral("java")).toObject();
    const QJsonArray images = java.value(QStringLiteral("images")).toArray();
    QStringList labels;
    for (const QJsonValue &value : images)
        labels << value.toString();
    m_javaValue->setText(images.isEmpty() ? QStringLiteral("尚未准备")
                                          : QStringLiteral("%1 个镜像").arg(images.size()));
    m_javaHint->setText(labels.isEmpty() ? QStringLiteral("创建服务器时会自动拉取")
                                         : labels.join(QStringLiteral(" · ")));

    const QJsonObject tools = doctor.value(QStringLiteral("tools")).toObject();
    QStringList available;
    if (tools.value(QStringLiteral("tar")).toBool())
        available << QStringLiteral("tar");
    if (tools.value(QStringLiteral("powershell")).toBool())
        available << QStringLiteral("PowerShell");
    m_toolsValue->setText(available.isEmpty() ? QStringLiteral("未检测到")
                                              : available.join(QStringLiteral(" · ")));
    m_toolsHint->setText(QStringLiteral("用于打包 / 解包存档备份"));

    const QJsonObject host = doctor.value(QStringLiteral("host")).toObject();
    m_aboutHome->setText(
        QStringLiteral("数据目录：%1").arg(host.value(QStringLiteral("dataRoot")).toString()));
    m_aboutLog->setText(QStringLiteral("后端日志：%1/logs/backend.log")
                            .arg(host.value(QStringLiteral("dataRoot")).toString()));
    m_aboutVersion->setText(QStringLiteral("McServerManager %1 · Qt %2 · %3")
                                .arg(QCoreApplication::applicationVersion(),
                                     host.value(QStringLiteral("qtVersion")).toString(),
                                     host.value(QStringLiteral("os")).toString()));
}

void SettingsPage::onActivated()
{
    m_syncingAppearance = true;
    m_themeSwitch->setCurrentIndex(ThemeManager::instance()->isDark() ? 0 : 1, false);
    m_accentSwitch->setCurrentIndex(
        ThemeManager::instance()->accentStyle() == ThemeManager::AccentStyle::Flat ? 1 : 0, false);
    m_syncingAppearance = false;
    m_animations->setChecked(AppContext::instance()->animationsEnabled());
    m_speed->setValue(int(AppContext::instance()->animationScale() * 100));
    updateSpeedLabel(m_speed->value());
    m_backendPath->setText(AppContext::instance()->backend()->executable());
    m_homePath->setText(AppContext::instance()->backend()->dataHome());
    m_experimentalSwitch->setChecked(AppContext::instance()->experimentalEnabled());
    syncFontControls();
    updateNavState();
    if (m_section == 2)
        refreshJdkList();
    else
        detectEnvironment();
}

} // namespace mcsm
