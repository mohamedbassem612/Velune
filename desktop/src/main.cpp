#include "DesktopController.h"
#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QSysInfo>
using namespace velune;
int main(int argc, char **argv) {
#ifdef Q_OS_LINUX
    // QML file/folder dialogs use the platform theme's native picker. Without
    // a theme on GTK desktops Qt falls back to its own Quick file browser.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")) {
        const auto desktop = qgetenv("XDG_CURRENT_DESKTOP").toLower();
        if (desktop.contains("gnome") || desktop.contains("xfce") || desktop.contains("cinnamon") ||
            desktop.contains("mate"))
            qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
    }
#endif
    QGuiApplication app(argc, argv);
    app.setApplicationName("Velune");
    app.setOrganizationName("Velune");
    app.setApplicationVersion("0.3.0");
    app.setDesktopFileName("velune");
    app.setWindowIcon(QIcon(":/assets/velune.svg"));
    for (const auto &file :
         QDir(QCoreApplication::applicationDirPath() + "/fonts").entryList({"*.otf", "*.ttf"}, QDir::Files))
        QFontDatabase::addApplicationFont(QCoreApplication::applicationDirPath() + "/fonts/" + file);
    QQuickStyle::setStyle("Basic");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data-dir", "Use a separate library directory.", "path"});
    parser.addOption({"preview", "Use a separate preview library."});
    parser.process(app);
    auto dir = parser.value("data-dir");
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
              (parser.isSet("preview") ? "-preview" : "");
    QDir().mkpath(dir);
    QString restoreError;
    if (!BackupService::applyPending(dir, &restoreError)) {
        qCritical().noquote() << restoreError;
        return 1;
    }
    int result;
    {
        Library library(dir);
        ConnectClient client(&library);
        MusicService music(&library, &client);
        Discovery discovery;
        auto advertise = [&] {
            discovery.advertise(client.identity(), client.listeningPort(), QSysInfo::machineHostName());
        };
        QObject::connect(&client, &ConnectClient::serverChanged, &discovery, advertise);
        advertise();
        DesktopController backend(&library, &music, &client, &discovery);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Velune/qml/Main.qml"));
        if (engine.rootObjects().isEmpty())
            return 1;
        result = app.exec();
    }
    if (result == 42) {
        if (!BackupService::applyPending(dir, &restoreError)) {
            qCritical().noquote() << restoreError;
            return 1;
        }
        return QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                       QCoreApplication::arguments().mid(1))
                   ? 0
                   : 1;
    }
    return result;
}
