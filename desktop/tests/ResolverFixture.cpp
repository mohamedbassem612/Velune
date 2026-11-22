#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>
#include <QtEndian>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto args = app.arguments();
    auto url = args.last();
    if (url.endsWith("FAILFAILFAI")) {
        QTextStream(stderr) << "ERROR: HTTP 403 secret-cookie-value-do-not-log\n";
        return 1;
    }
    if (url.endsWith("SLOWSLOWSLO"))
        QThread::msleep(2000);
    auto cookieIndex = args.indexOf("--cookies");
    if (cookieIndex >= 0) {
        QFileInfo file(args[cookieIndex + 1]);
        if (!file.exists() || (file.permissions() & (QFile::ReadGroup | QFile::ReadOther)))
            return 2;
    }
    QTextStream(stdout)
        << (url.endsWith("PATHPATHPAT")
                ? R"({"url":"https://attacker.test/audio"})"
                : R"({"url":"https://fixture.googlevideo.com/videoplayback","duration":180,"http_headers":{"User-Agent":"VeluneTest"}})")
        << '\n';
}
