#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QStyleFactory>
#include <QTimer>

#include "app/AppContext.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("McServerManager"));
    QApplication::setOrganizationName(QStringLiteral("McServerManager"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    // Draw every control ourselves: the native Windows style ignores parts of
    // the style sheet (combo box popups follow the *system* light/dark theme,
    // which produced black-on-black popups) and varies between Windows versions.
    // Fusion honours both the palette and the style sheet completely, while
    // native file dialogs stay native.
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);

    QFont appFont;
    appFont.setFamilies({QStringLiteral("HarmonyOS Sans SC"), QStringLiteral("MiSans"),
                         QStringLiteral("Noto Sans SC"), QStringLiteral("Source Han Sans SC"),
                         QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI")});
    appFont.setPointSizeF(10.0);
    QApplication::setFont(appFont);

    mcsm::AppContext *context = mcsm::AppContext::instance();
    context->bootstrap();

    mcsm::MainWindow window;
    window.show();

    // `--page servers` opens a specific page (handy for shortcuts and tests).
    const QStringList arguments = QCoreApplication::arguments();
    const int pageIndex = arguments.indexOf(QStringLiteral("--page"));
    if (pageIndex >= 0 && pageIndex + 1 < arguments.size())
        QTimer::singleShot(0, &window, [&window, arguments, pageIndex]() {
            window.showPage(arguments.at(pageIndex + 1));
        });

    return app.exec();
}
