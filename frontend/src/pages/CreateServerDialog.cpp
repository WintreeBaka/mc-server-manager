#include "pages/CreateServerDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "widgets/Chip.h"
#include "widgets/Common.h"

namespace mcsm {

CreateServerDialog::CreateServerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("新建服务器"));
    setMinimumSize(720, 620);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 22);
    root->setSpacing(16);

    root->addWidget(makeSectionHeader(QStringLiteral("新建服务器"),
                                      QStringLiteral("自动模式会根据版本下载服务端并准备对应的 JDK 容器镜像"),
                                      nullptr, this));

    m_modeSwitch = new SegmentedControl(this);
    m_modeSwitch->setItems({QStringLiteral("一键自动配置"), QStringLiteral("手动导入")});
    root->addWidget(m_modeSwitch, 0, Qt::AlignLeft);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildAutoPane());
    m_stack->addWidget(buildManualPane());
    root->addWidget(m_stack, 1);

    root->addWidget(buildProgressArea());

    connect(m_modeSwitch, &SegmentedControl::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
    });
    loadTypes();
}

QWidget *CreateServerDialog::buildAutoPane()
{
    auto *pane = new QWidget(this);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(14);

    auto *basicCard = new CardFrame(pane);
    auto *basicLayout = new QFormLayout(basicCard);
    basicLayout->setContentsMargins(18, 16, 18, 16);
    basicLayout->setSpacing(10);
    basicLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_name = new QLineEdit(basicCard);
    m_name->setPlaceholderText(QStringLiteral("例如 生存服 / 空岛服"));
    m_type = new QComboBox(basicCard);
    m_version = new QComboBox(basicCard);
    m_version->setMinimumWidth(220);
    m_versionHint = makeLabel(QStringLiteral("选择类型后会加载可用版本"), QStringLiteral("hint"), basicCard);
    m_port = new QSpinBox(basicCard);
    m_port->setRange(1024, 65535);
    m_port->setValue(25565);
    m_memory = new QComboBox(basicCard);
    for (const QString &value : {QStringLiteral("2G"), QStringLiteral("4G"), QStringLiteral("6G"),
                                 QStringLiteral("8G"), QStringLiteral("12G"), QStringLiteral("16G")}) {
        m_memory->addItem(value, value);
    }
    m_memory->setCurrentText(QStringLiteral("4G"));

    basicLayout->addRow(QStringLiteral("服务器名称"), m_name);
    basicLayout->addRow(QStringLiteral("服务端类型"), m_type);
    basicLayout->addRow(QStringLiteral("Minecraft 版本"), m_version);
    basicLayout->addRow(QString(), m_versionHint);
    basicLayout->addRow(QStringLiteral("游戏端口"), m_port);
    basicLayout->addRow(QStringLiteral("内存上限"), m_memory);
    basicLayout->addRow(QStringLiteral("世界名称"), m_levelName = new QLineEdit(QStringLiteral("world"), basicCard));

    // experimental: allow running on a JDK cloned from this machine
    if (AppContext::instance()->experimentalEnabled()) {
        m_runtime = new QComboBox(basicCard);
        m_runtime->addItem(QStringLiteral("Docker 容器（推荐）"), QStringLiteral("docker"));
        m_runtime->addItem(QStringLiteral("本机 JDK（实验性）"), QStringLiteral("host"));
        m_jdkSelect = new QComboBox(basicCard);
        m_jdkSelect->setVisible(false);
        basicLayout->addRow(QStringLiteral("运行方式"), m_runtime);
        basicLayout->addRow(QStringLiteral("本机 JDK"), m_jdkSelect);
        connect(m_runtime, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            const bool host = m_runtime->currentData().toString() == QLatin1String("host");
            m_jdkSelect->setVisible(host);
            if (host && m_jdkSelect->count() == 0)
                loadHostJdks();
        });
    }

    auto *gameplayCard = new CardFrame(pane);
    auto *gameplayLayout = new QFormLayout(gameplayCard);
    gameplayLayout->setContentsMargins(18, 16, 18, 16);
    gameplayLayout->setSpacing(10);

    m_gamemode = new QComboBox(gameplayCard);
    m_gamemode->addItems({QStringLiteral("survival"), QStringLiteral("creative"),
                          QStringLiteral("adventure"), QStringLiteral("spectator")});
    m_difficulty = new QComboBox(gameplayCard);
    m_difficulty->addItems({QStringLiteral("peaceful"), QStringLiteral("easy"),
                            QStringLiteral("normal"), QStringLiteral("hard")});
    m_difficulty->setCurrentText(QStringLiteral("easy"));
    m_maxPlayers = new QSpinBox(gameplayCard);
    m_maxPlayers->setRange(1, 1000);
    m_maxPlayers->setValue(20);
    m_autoRestart = new ToggleSwitch(gameplayCard);
    m_autoRestart->setChecked(true);
    m_autoRestart->setOnText(QStringLiteral("自动重启"), QStringLiteral("自动重启"));
    m_eula = new ToggleSwitch(gameplayCard);
    m_eula->setChecked(true);
    m_eula->setOnText(QStringLiteral("已同意"), QStringLiteral("未同意"));

    gameplayLayout->addRow(QStringLiteral("游戏模式"), m_gamemode);
    gameplayLayout->addRow(QStringLiteral("难度"), m_difficulty);
    gameplayLayout->addRow(QStringLiteral("最大玩家数"), m_maxPlayers);
    gameplayLayout->addRow(QStringLiteral("崩溃自动重启"), m_autoRestart);
    gameplayLayout->addRow(QStringLiteral("同意 Minecraft EULA"), m_eula);

    auto *notice = makeLabel(
        QStringLiteral("自动配置会依次完成：解析版本 → 下载服务端 → 拉取 JDK 容器镜像 → 生成配置与启动脚本。"),
        QStringLiteral("hint"), pane);
    layout->addWidget(basicCard);
    layout->addWidget(gameplayCard);
    layout->addWidget(notice);
    layout->addStretch(1);

    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const QString type = m_type->currentData().toString();
        if (!type.isEmpty())
            loadVersions(type);
    });
    connect(m_version, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        // the item data holds the plain version id (see loadVersions)
        const QString version = m_version->currentData().toString();
        const QString java = m_versionMeta.value(version).value(QStringLiteral("javaLabel")).toString();
        m_versionHint->setText(java.isEmpty()
                                   ? QStringLiteral("已选择 %1").arg(version)
                                   : QStringLiteral("%1 需要 %2，容器镜像会自动匹配")
                                         .arg(version, java));
    });
    return pane;
}

QWidget *CreateServerDialog::buildManualPane()
{
    auto *pane = new QWidget(this);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(14);

    auto *card = new CardFrame(pane);
    auto *form = new QFormLayout(card);
    form->setContentsMargins(18, 16, 18, 16);
    form->setSpacing(10);

    m_manualName = new QLineEdit(card);
    m_manualName->setPlaceholderText(QStringLiteral("例如 模组服"));

    auto *jarRow = new QWidget(card);
    auto *jarLayout = new QHBoxLayout(jarRow);
    jarLayout->setContentsMargins(0, 0, 0, 0);
    jarLayout->setSpacing(8);
    m_manualJar = new QLineEdit(jarRow);
    m_manualJar->setPlaceholderText(QStringLiteral("选择本地 server.jar / 整合包服务端"));
    auto *browse = new GradientButton(QStringLiteral("浏览…"), jarRow);
    browse->setStyle(GradientButton::Outline);
    browse->setCompact(true);
    jarLayout->addWidget(m_manualJar, 1);
    jarLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, &CreateServerDialog::browseJar);

    m_manualType = new QComboBox(card);
    m_manualJava = new QComboBox(card);
    for (const QString &image : {QStringLiteral("eclipse-temurin:8-jre"),
                                 QStringLiteral("eclipse-temurin:11-jre"),
                                 QStringLiteral("eclipse-temurin:17-jre"),
                                 QStringLiteral("eclipse-temurin:21-jre")}) {
        m_manualJava->addItem(image, image);
    }
    m_manualJava->setCurrentText(QStringLiteral("eclipse-temurin:17-jre"));
    m_manualPort = new QSpinBox(card);
    m_manualPort->setRange(1024, 65535);
    m_manualPort->setValue(25565);
    m_manualMemory = new QComboBox(card);
    m_manualMemory->addItems({QStringLiteral("2G"), QStringLiteral("4G"), QStringLiteral("6G"),
                              QStringLiteral("8G"), QStringLiteral("12G"), QStringLiteral("16G")});
    m_manualMemory->setCurrentText(QStringLiteral("4G"));
    m_manualLevel = new QLineEdit(QStringLiteral("world"), card);
    m_manualEula = new ToggleSwitch(card);
    m_manualEula->setChecked(true);
    m_manualEula->setOnText(QStringLiteral("已同意"), QStringLiteral("未同意"));

    form->addRow(QStringLiteral("服务器名称"), m_manualName);
    form->addRow(QStringLiteral("服务端文件"), jarRow);
    form->addRow(QStringLiteral("服务端类型"), m_manualType);
    form->addRow(QStringLiteral("JDK 容器镜像"), m_manualJava);
    form->addRow(QStringLiteral("游戏端口"), m_manualPort);
    form->addRow(QStringLiteral("内存上限"), m_manualMemory);
    form->addRow(QStringLiteral("世界名称"), m_manualLevel);
    form->addRow(QStringLiteral("同意 Minecraft EULA"), m_manualEula);

    layout->addWidget(card);
    layout->addWidget(makeLabel(
        QStringLiteral("手动模式会把你提供的服务端文件复制到服务器目录，并生成相同的容器启动脚本；"
                       "类型仅用于判断插件兼容性。"),
        QStringLiteral("hint"), pane));
    layout->addStretch(1);
    return pane;
}

QWidget *CreateServerDialog::buildProgressArea()
{
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_progress = new QProgressBar(container);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_stageLabel = makeLabel(QStringLiteral("准备就绪，点击“开始创建”"), QStringLiteral("hint"), container);
    m_errorLabel = makeLabel(QString(), QStringLiteral("hint"), container);
    m_errorLabel->hide();

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(10);
    m_cancelButton = new GradientButton(QStringLiteral("取消"), container);
    m_cancelButton->setStyle(GradientButton::Outline);
    m_createButton = new GradientButton(QStringLiteral("开始创建"), container);
    m_createButton->setGlyph(QStringLiteral("⚡"));
    buttons->addStretch(1);
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_createButton);

    layout->addWidget(m_progress);
    layout->addWidget(m_stageLabel);
    layout->addWidget(m_errorLabel);
    layout->addLayout(buttons);

    connect(m_createButton, &QPushButton::clicked, this, &CreateServerDialog::startInstall);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        if (m_busy)
            reject();
        else
            close();
    });
    return container;
}

void CreateServerDialog::loadTypes()
{
    AppContext::instance()->backend()->request({QStringLiteral("types")}, this, [this](const Reply &reply) {
        m_types = reply.data.value(QStringLiteral("types")).toArray();
        const QVector<QComboBox *> combos = {m_type, m_manualType};
        for (QComboBox *combo : combos) {
            combo->clear();
            for (const QJsonValue &value : m_types) {
                const QJsonObject type = value.toObject();
                combo->addItem(type.value(QStringLiteral("label")).toString(),
                               type.value(QStringLiteral("id")));
            }
        }
        if (!m_types.isEmpty()) {
            for (QComboBox *combo : combos)
                combo->setToolTip(m_types.first().toObject().value(QStringLiteral("description")).toString());
        }
        if (!m_type->currentData().toString().isEmpty())
            loadVersions(m_type->currentData().toString());
    });
}

void CreateServerDialog::loadVersions(const QString &type)
{
    if (type.isEmpty() || type == m_currentType)
        return;
    m_currentType = type;
    m_version->clear();
    m_version->addItem(QStringLiteral("正在加载版本…"), QString());
    m_versionHint->setText(QStringLiteral("正在从官方接口获取 %1 的版本列表…").arg(type));

    AppContext::instance()->backend()->request({QStringLiteral("versions"), QStringLiteral("--type"), type},
                                               this, [this, type](const Reply &reply) {
        if (m_currentType != type)
            return;
        m_version->clear();
        if (!reply.ok) {
            m_version->addItem(QStringLiteral("加载失败"), QString());
            m_versionHint->setText(QStringLiteral("无法获取版本：%1").arg(reply.errorText()));
            return;
        }
        m_versions = reply.data.value(QStringLiteral("versions")).toArray();
        int index = 0;
        for (const QJsonValue &value : m_versions) {
            const QJsonObject version = value.toObject();
            // store the plain id: currentData().toString() must return it when the
            // dialog validates the selection
            const QString id = version.value(QStringLiteral("id")).toString();
            m_version->addItem(id, id);
            m_versionMeta.insert(id, version);
            if (index == 0) {
                m_versionHint->setText(QStringLiteral("%1 · 自动匹配 %2")
                                           .arg(id,
                                                version.value(QStringLiteral("javaLabel")).toString()));
            }
            ++index;
        }
        m_versionHint->setText(QStringLiteral("已加载 %1 个版本").arg(m_versions.size()));
    });
}

void CreateServerDialog::loadHostJdks()
{
    if (!m_jdkSelect)
        return;
    m_jdkSelect->clear();
    m_jdkSelect->addItem(QStringLiteral("正在扫描本机 JDK…"), QString());
    AppContext::instance()->backend()->request(
        {QStringLiteral("java"), QStringLiteral("--action"), QStringLiteral("scan")}, this,
        [this](const Reply &reply) {
            m_jdkSelect->clear();
            if (!reply.ok) {
                m_jdkSelect->addItem(QStringLiteral("扫描失败"), QString());
                return;
            }
            const QJsonArray jdks = reply.data.value(QStringLiteral("jdks")).toArray();
            for (const QJsonValue &value : jdks) {
                const QJsonObject jdk = value.toObject();
                const QString label = QStringLiteral("JDK %1 · %2 · %3")
                                          .arg(jdk.value(QStringLiteral("major")).toInt())
                                          .arg(jdk.value(QStringLiteral("vendor")).toString())
                                          .arg(jdk.value(QStringLiteral("home")).toString());
                m_jdkSelect->addItem(label, jdk.value(QStringLiteral("home")).toString());
            }
            if (jdks.isEmpty())
                m_jdkSelect->addItem(QStringLiteral("没有找到本机 JDK"), QString());
        });
}

void CreateServerDialog::browseJar()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择服务端文件"), QString(),
                                                     QStringLiteral("服务端 (*.jar);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    m_manualJar->setText(path);
    if (m_manualName->text().trimmed().isEmpty())
        m_manualName->setText(QFileInfo(path).completeBaseName());
}

QString CreateServerDialog::selectedVersion() const
{
    return m_version ? m_version->currentText() : QString();
}

void CreateServerDialog::setProgress(const QString &stage, int percent, const QString &detail)
{
    m_progress->setValue(qBound(0, percent, 100));
    m_stageLabel->setText(detail.isEmpty() ? stage : QStringLiteral("%1 · %2").arg(stage, detail));
}

void CreateServerDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_createButton->setEnabled(!busy);
    m_createButton->setText(busy ? QStringLiteral("创建中…") : QStringLiteral("开始创建"));
    m_modeSwitch->setEnabled(!busy);
    m_stack->setEnabled(!busy);
}

void CreateServerDialog::showError(const QString &message)
{
    m_errorLabel->setText(message);
    m_errorLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(ThemeManager::instance()->palette().danger.name()));
    m_errorLabel->show();
}

void CreateServerDialog::startInstall()
{
    if (m_busy)
        return;
    const bool manual = m_modeSwitch->currentIndex() == 1;

    QStringList arguments {QStringLiteral("install")};
    if (manual) {
        if (m_manualName->text().trimmed().isEmpty()) {
            showError(QStringLiteral("请填写服务器名称"));
            return;
        }
        if (m_manualJar->text().trimmed().isEmpty()) {
            showError(QStringLiteral("请选择本地服务端文件"));
            return;
        }
        arguments << QStringLiteral("--name") << m_manualName->text().trimmed()
                  << QStringLiteral("--type") << m_manualType->currentData().toString()
                  << QStringLiteral("--manual") << QStringLiteral("true")
                  << QStringLiteral("--jar") << m_manualJar->text().trimmed()
                  << QStringLiteral("--image") << m_manualJava->currentData().toString()
                  << QStringLiteral("--port") << QString::number(m_manualPort->value())
                  << QStringLiteral("--memory") << m_manualMemory->currentText()
                  << QStringLiteral("--level-name") << m_manualLevel->text().trimmed()
                  << QStringLiteral("--eula")
                  << (m_manualEula->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    } else {
        if (m_name->text().trimmed().isEmpty()) {
            showError(QStringLiteral("请填写服务器名称"));
            return;
        }
        if (m_version->currentText().isEmpty() || m_version->currentData().toString().isEmpty()) {
            showError(QStringLiteral("请选择 Minecraft 版本（若列表为空请检查网络）"));
            return;
        }
        arguments << QStringLiteral("--name") << m_name->text().trimmed()
                  << QStringLiteral("--type") << m_type->currentData().toString()
                  << QStringLiteral("--version") << m_version->currentText()
                  << QStringLiteral("--port") << QString::number(m_port->value())
                  << QStringLiteral("--memory") << m_memory->currentText()
                  << QStringLiteral("--level-name") << m_levelName->text().trimmed()
                  << QStringLiteral("--gamemode") << m_gamemode->currentText()
                  << QStringLiteral("--difficulty") << m_difficulty->currentText()
                  << QStringLiteral("--max-players") << QString::number(m_maxPlayers->value())
                  << QStringLiteral("--auto-restart")
                  << (m_autoRestart->isChecked() ? QStringLiteral("true") : QStringLiteral("false"))
                  << QStringLiteral("--eula")
                  << (m_eula->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    }
    arguments << QStringLiteral("--progress");

    if (m_runtime && m_runtime->currentData().toString() == QLatin1String("host")) {
        const QString jdkHome = m_jdkSelect->currentData().toString();
        if (jdkHome.isEmpty()) {
            showError(QStringLiteral("请选择要使用的本机 JDK（可在 设置 → 实验性功能 中重新扫描）"));
            return;
        }
        arguments << QStringLiteral("--runtime") << QStringLiteral("host")
                  << QStringLiteral("--jdk-home") << jdkHome;
    }

    m_errorLabel->hide();
    setBusy(true);
    setProgress(QStringLiteral("准备"), 2, QStringLiteral("正在校验参数"));

    AppContext::instance()->backend()->request(
        arguments, this,
        [this](const Reply &reply) {
            setBusy(false);
            if (!reply.ok) {
                setProgress(QStringLiteral("失败"), m_progress->value(), QString());
                showError(reply.errorText());
                AppContext::instance()->logActivity(
                    QStringLiteral("创建服务器失败：%1").arg(reply.message), QStringLiteral("error"));
                return;
            }
            const QString id = reply.data.value(QStringLiteral("id")).toString();
            setProgress(QStringLiteral("完成"), 100, QStringLiteral("服务器已创建"));
            AppContext::instance()->logActivity(
                QStringLiteral("服务器 %1 创建成功").arg(reply.data.value(QStringLiteral("name")).toString()),
                QStringLiteral("success"));
            emit serverCreated(id);
            accept();
        },
        [this](const QString &stage, int percent, const QString &detail) {
            setProgress(stage, percent, detail);
        });
}

} // namespace mcsm
