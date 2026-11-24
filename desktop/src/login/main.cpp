#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkCookie>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWebEngineProfile>
#include <QSocketNotifier>
#include <QTimer>
#include <QWebEngineCookieStore>
#include <QWebEngineDownloadRequest>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <cstdio>
int main(int argc, char **argv) {
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    app.setApplicationName("Velune sign-in");
    QSocketNotifier control(fileno(stdin), QSocketNotifier::Read);
    QObject::connect(&control, &QSocketNotifier::activated, &app, [&] {
        char command[32];
        if (!fgets(command, sizeof(command), stdin) || QByteArray(command).trimmed() == "quit")
            app.quit();
    });
    QQuickStyle::setStyle("Basic");
    QQuickWebEngineProfile profile;
    profile.setOffTheRecord(true);
    profile.setHttpCacheType(QQuickWebEngineProfile::MemoryHttpCache);
    profile.setPersistentCookiesPolicy(QQuickWebEngineProfile::NoPersistentCookies);
    QList<QNetworkCookie> cookies;
    QTimer publish;
    publish.setSingleShot(true);
    publish.setInterval(350);
    QObject::connect(&publish, &QTimer::timeout, &app, [&] {
        QJsonArray list;
        for (const auto &cookie : cookies)
            list.append(QString::fromLatin1(cookie.toRawForm().toBase64()));
        auto bytes = QJsonDocument(QJsonObject{{"type", "cookies"}, {"cookies", list}})
                         .toJson(QJsonDocument::Compact) +
                     '\n';
        fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
        fflush(stdout);
    });
    auto store = profile.cookieStore();
    QObject::connect(store, &QWebEngineCookieStore::cookieAdded, &app, [&](const QNetworkCookie &cookie) {
        auto domain = cookie.domain().toLower();
        if (domain.startsWith('.'))
            domain.remove(0, 1);
        if (domain != "youtube.com" && domain != "music.youtube.com")
            return;
        for (auto it = cookies.begin(); it != cookies.end();)
            if (it->hasSameIdentifier(cookie))
                it = cookies.erase(it);
            else
                ++it;
        cookies.append(cookie);
        publish.start();
    });
    QObject::connect(store, &QWebEngineCookieStore::cookieRemoved, &app, [&](const QNetworkCookie &cookie) {
        for (auto it = cookies.begin(); it != cookies.end();)
            if (it->hasSameIdentifier(cookie))
                it = cookies.erase(it);
            else
                ++it;
        publish.start();
    });
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("loginProfile", &profile);
    engine.load(QUrl("qrc:/VeluneLogin/qml/Login.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
