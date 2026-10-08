#pragma once

#include <QDialog>
#include <QJsonObject>

#include "widgets/Common.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;

namespace mcsm {

class SegmentedControl;

/// Import a plugin package either from a local zip or from a URL.
///
/// The dialog always inspects the package first (`plugin package inspect`), so
/// the detected scope (frontend / backend / global / web) is shown before
/// anything is written to the plugin directory.
class PluginImportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PluginImportDialog(QWidget *parent = nullptr);

signals:
    void installed(const QString &pluginId);

private:
    QWidget *buildSourcePane();
    QWidget *buildLocalPane();
    QWidget *buildUrlPane();
    QWidget *buildResultPane();

    void browse();
    void startImport();
    void inspect();
    void install();
    void setBusy(bool busy, const QString &status);
    QString sourceArgument() const;
    bool isUrlMode() const;

    SegmentedControl *m_mode = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLineEdit *m_zipPath = nullptr;
    QLineEdit *m_url = nullptr;
    QLineEdit *m_expectedId = nullptr;
    ToggleSwitch *m_force = nullptr;
    ToggleSwitch *m_enable = nullptr;

    QLabel *m_status = nullptr;
    QLabel *m_scope = nullptr;
    QPlainTextEdit *m_details = nullptr;
    GradientButton *m_importButton = nullptr;
    GradientButton *m_closeButton = nullptr;

    bool m_busy = false;
    bool m_installed = false;
};

} // namespace mcsm
