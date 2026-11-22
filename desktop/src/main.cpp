#include "ui/MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
using namespace velune;
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Velune");
    app.setOrganizationName("Velune");
    app.setApplicationVersion("0.1.4");
    app.setDesktopFileName("velune");
    QPixmap icon(128, 128);
    icon.fill(Qt::black);
    QPainter painter(&icon);
    drawIcon(painter, Icon::Logo, QRectF(27, 27, 74, 74));
    painter.end();
    app.setWindowIcon(QIcon(icon));
    QCommandLineParser parser;
    parser.setApplicationDescription("Velune Desktop — monochrome music player");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(
        {"preview", "Show the signed-in layout for visual review; no account session is created."});
    parser.addOption({"screenshot", "Save a UI review screenshot, then exit.", "path"});
    parser.addOption({"data-dir", "Use a separate library directory.", "path"});
    parser.process(app);
    QString dir = parser.value("data-dir");
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    Library library(dir);
    ConnectClient connect(&library);
    MusicService music(&library, &connect);
    Discovery discovery;
    MainWindow window(&library, &music, &connect, &discovery, parser.isSet("preview"));
    if (!parser.isSet("screenshot") && app.primaryScreen()) {
        const auto available = app.primaryScreen()->availableGeometry().size();
        window.resize(qMin(1660, available.width()), qMin(938, available.height()));
    }
    window.show();
    if (parser.isSet("screenshot"))
        QTimer::singleShot(900, &window, [&] {
            window.grab().save(parser.value("screenshot"));
            app.quit();
        });
    return app.exec();
}
