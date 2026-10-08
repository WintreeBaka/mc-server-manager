#include "pages/PluginImportDialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "widgets/Chip.h"

namespace mcsm {

PluginImportDialog::PluginImportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("添加插件"));
    setMinimumSize(660, 560);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(14);

    root->addWidget(makeSectionHeader(
        QStringLiteral("添加插件"),
        QStringLiteral("支持本地 zip 压缩包或 URL 地址；导入时会自动识别插件作用域（前端 / 后端 / 全局）"),
        nullptr, this));

    root->addWidget(buildSourcePane());

    m_status = makeLabel(QStringLiteral("选择来源后点击「校验并导入」"), QStringLiteral("hint"), this);
    root->addWidget(m_status);

    root->addWidget(buildResultPane(), 1);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(10);
    buttons->addStretch(1);
    m_closeButton = new GradientButton(QStringLiteral("关闭"), this);
    m_closeButton->setStyle(GradientButton::Outline);
    m_closeButton->setCompact(true);
    m_importButton = new GradientButton(QStringLiteral("校验并导入"), this);
    m_importButton->setCompact(true);
    buttons->addWidget(m_closeButton);
    buttons->addWidget(m_importButton);
    root->addLayout(buttons);

    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_importButton, &QPushButton::clicked, this, &PluginImportDialog::startImport);
    connect(m_mode, &SegmentedControl::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
        m_status->setText(QStringLiteral("选择来源后点击「校验并导入」"));
    });
}

QWidget *PluginImportDialog::buildSourcePane()
{
    auto *card = new CardFrame(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 18);
    layout->setSpacing(12);

    m_mode = new SegmentedControl(card);
    m_mode->setItems({QStringLiteral("本地 zip 文件"), QStringLiteral("从 URL 导入")});
    layout->addWidget(m_mode, 0, Qt::AlignLeft);

    m_stack = new QStackedWidget(card);
    m_stack->addWidget(buildLocalPane());
    m_stack->addWidget(buildUrlPane());
    layout->addWidget(m_stack);

    auto *options = new QFormLayout();
    options->setContentsMargins(0, 4, 0, 0);
    options->setSpacing(10);
    m_expectedId = new QLineEdit(card);
    m_expectedId->setPlaceholderText(QStringLiteral("可选：只允许安装该 id，防止导入错误插件"));
    options->addRow(QStringLiteral("限定插件 ID"), m_expectedId);

    auto *switches = new QHBoxLayout();
    switches->setSpacing(18);
    m_force = new ToggleSwitch(card);
    m_force->setOnText(QStringLiteral("是"), QStringLiteral("否"));
    m_enable = new ToggleSwitch(card);
    m_enable->setChecked(true);
    m_enable->setOnText(QStringLiteral("启用"), QStringLiteral("停用"));
    auto *forceRow = new QWidget(card);
    auto *forceLayout = new QHBoxLayout(forceRow);
    forceLayout->setContentsMargins(0, 0, 0, 0);
    forceLayout->setSpacing(8);
    forceLayout->addWidget(makeLabel(QStringLiteral("覆盖已安装的同名插件"), QStringLiteral("caption"), forceRow));
    forceLayout->addWidget(m_force);
    auto *enableRow = new QWidget(card);
    auto *enableLayout = new QHBoxLayout(enableRow);
    enableLayout->setContentsMargins(0, 0, 0, 0);
    enableLayout->setSpacing(8);
    enableLayout->addWidget(makeLabel(QStringLiteral("导入后立即启用"), QStringLiteral("caption"), enableRow));
    enableLayout->addWidget(m_enable);
    switches->addWidget(forceRow);
    switches->addWidget(enableRow);
    switches->addStretch(1);
    options->addRow(QString(), switches);
    layout->addLayout(options);
    return card;
}

QWidget *PluginImportDialog::buildLocalPane()
{
    auto *pane = new QWidget(this);
    auto *layout = new QHBoxLayout(pane);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(10);
    m_zipPath = new QLineEdit(pane);
    m_zipPath->setPlaceholderText(QStringLiteral("例如 D:\\下载\\my-plugin-1.0.0.zip"));
    auto *browse = new GradientButton(QStringLiteral("浏览…"), pane);
    browse->setStyle(GradientButton::Outline);
    browse->setCompact(true);
    layout->addWidget(m_zipPath, 1);
    layout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, &PluginImportDialog::browse);
    return pane;
}

QWidget *PluginImportDialog::buildUrlPane()
{
    auto *pane = new QWidget(this);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(6);
    m_url = new QLineEdit(pane);
    m_url->setPlaceholderText(QStringLiteral("https://example.com/plugins/my-plugin-1.0.0.zip"));
    layout->addWidget(m_url);
    layout->addWidget(makeLabel(
        QStringLiteral("支持 http / https 直链；下载完成后会用内置 7-Zip 解压并校验 plugin.json。"),
        QStringLiteral("hint"), pane));
    return pane;
}

QWidget *PluginImportDialog::buildResultPane()
{
    auto *card = new CardFrame(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    m_scope = makeLabel(QStringLiteral("尚未识别"), QStringLiteral("subtitle"), card);
    layout->addWidget(m_scope);
    m_details = new QPlainTextEdit(card);
    m_details->setReadOnly(true);
    m_details->setMinimumHeight(160);
    m_details->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_details, 1);
    return card;
}

void PluginImportDialog::browse()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择插件包"),
                                                      QString(), QStringLiteral("插件包 (*.zip)"));
    if (!path.isEmpty())
        m_zipPath->setText(path);
}

bool PluginImportDialog::isUrlMode() const
{
    return m_mode->currentIndex() == 1;
}

QString PluginImportDialog::sourceArgument() const
{
    return isUrlMode() ? m_url->text().trimmed() : m_zipPath->text().trimmed();
}

void PluginImportDialog::setBusy(bool busy, const QString &status)
{
    m_busy = busy;
    m_importButton->setEnabled(!busy);
    m_importButton->setText(busy ? QStringLiteral("处理中…") : QStringLiteral("校验并导入"));
    if (!status.isEmpty())
        m_status->setText(status);
}

void PluginImportDialog::startImport()
{
    if (m_busy)
        return;
    const QString source = sourceArgument();
    if (source.isEmpty()) {
        m_status->setText(isUrlMode() ? QStringLiteral("请先填写插件包 URL")
                                      : QStringLiteral("请先选择本地插件包"));
        return;
    }
    if (isUrlMode() && !source.startsWith(QLatin1String("http"), Qt::CaseInsensitive)) {
        m_status->setText(QStringLiteral("URL 必须以 http:// 或 https:// 开头"));
        return;
    }
    if (!isUrlMode() && !QFileInfo::exists(source)) {
        m_status->setText(QStringLiteral("找不到该文件：%1").arg(source));
        return;
    }
    inspect();
}

void PluginImportDialog::inspect()
{
    setBusy(true, QStringLiteral("正在下载并校验插件包…"));
    m_details->setPlainText(QString());
    QStringList arguments {QStringLiteral("plugin"), QStringLiteral("package"),
                           QStringLiteral("inspect")};
    arguments << (isUrlMode() ? QStringLiteral("--url") : QStringLiteral("--zip")) << sourceArgument();

    AppContext::instance()->backend()->request(
        arguments, this, [this](const Reply &reply) {
            const QJsonObject data = reply.data;
            if (!reply.ok) {
                setBusy(false, QStringLiteral("校验未通过：%1").arg(reply.message.isEmpty()
                                                                       ? reply.errorText()
                                                                       : reply.message));
                m_scope->setText(QStringLiteral("✕ 不是有效的插件包"));
                m_details->setPlainText(QStringLiteral("%1\n%2").arg(reply.errorText(), reply.detail));
                return;
            }

            m_details->setPlainText(
                QStringLiteral("id          : %1\n名称        : %2\n版本        : %3\n"
                               "作用域      : %4\n包含部分    : %5\n作者        : %6\n描述        : %7\n"
                               "解压工具    : %8")
                    .arg(data.value(QStringLiteral("id")).toString(),
                         data.value(QStringLiteral("name")).toString(),
                         data.value(QStringLiteral("version")).toString(),
                         data.value(QStringLiteral("scopeLabel")).toString(),
                         [&data]() {
                             QStringList parts;
                             for (const QJsonValue &value : data.value(QStringLiteral("parts")).toArray())
                                 parts << value.toString();
                             return parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral(" + "));
                         }(),
                         data.value(QStringLiteral("author")).toString(),
                         data.value(QStringLiteral("description")).toString(),
                         data.value(QStringLiteral("extractor")).toString()));

            const bool already = data.value(QStringLiteral("alreadyInstalled")).toBool();
            m_scope->setText(QStringLiteral("✔ 识别为：%1%2")
                                 .arg(data.value(QStringLiteral("scopeLabel")).toString(),
                                      already ? QStringLiteral("（已安装，覆盖需打开下方开关）")
                                              : QString()));
            if (already && !m_force->isChecked()) {
                setBusy(false, QStringLiteral("该插件已安装，如需重新导入请打开「覆盖」开关"));
                return;
            }
            install();
        });
}

void PluginImportDialog::install()
{
    setBusy(true, QStringLiteral("正在安装…"));
    QStringList arguments {QStringLiteral("plugin"), QStringLiteral("package"),
                           QStringLiteral("install")};
    arguments << (isUrlMode() ? QStringLiteral("--url") : QStringLiteral("--zip")) << sourceArgument();
    if (m_force->isChecked())
        arguments << QStringLiteral("--force");
    if (!m_enable->isChecked())
        arguments << QStringLiteral("--disabled");
    const QString expected = m_expectedId->text().trimmed();
    if (!expected.isEmpty())
        arguments << QStringLiteral("--expect") << expected;

    AppContext::instance()->backend()->request(
        arguments, this, [this](const Reply &reply) {
            if (!reply.ok) {
                setBusy(false, QStringLiteral("安装失败：%1").arg(reply.message));
                m_details->setPlainText(QStringLiteral("%1\n%2").arg(reply.errorText(), reply.detail));
                AppContext::instance()->logActivity(
                    QStringLiteral("插件安装失败：%1").arg(reply.errorText()), QStringLiteral("error"));
                return;
            }
            m_installed = true;
            setBusy(false, QStringLiteral("✔ 安装完成"));
            const QJsonObject data = reply.data;
            m_scope->setText(QStringLiteral("✔ 已安装：%1（%2）")
                                 .arg(data.value(QStringLiteral("name")).toString(),
                                      data.value(QStringLiteral("scopeLabel")).toString()));
            QString text = m_details->toPlainText();
            text += QStringLiteral("\n\n安装位置    : %1\n文件数量    : %2")
                        .arg(data.value(QStringLiteral("installedPath")).toString())
                        .arg(data.value(QStringLiteral("fileCount")).toInt());
            if (!reply.warnings.isEmpty())
                text += QStringLiteral("\n提示        : %1").arg(reply.warnings.join(QStringLiteral("；")));
            m_details->setPlainText(text);
            m_closeButton->setText(QStringLiteral("完成"));

            const QString id = data.value(QStringLiteral("id")).toString();
            AppContext::instance()->logActivity(
                QStringLiteral("已添加插件 %1").arg(data.value(QStringLiteral("name")).toString()),
                QStringLiteral("success"));
            emit installed(id);
        });
}

} // namespace mcsm
