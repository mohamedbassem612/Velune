#include "services/GoogleAuth.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
using namespace velune;
namespace {
const QString scope = "openid email profile https://www.googleapis.com/auth/youtube.readonly";
class MockGoogle : public QObject {
  public:
    QTcpServer server;
    QJsonObject token{{"access_token", "test-access"},
                      {"refresh_token", "test-refresh"},
                      {"token_type", "Bearer"},
                      {"expires_in", 3600},
                      {"scope", scope}};
    QJsonObject identity{{"sub", "test-subject"},
                         {"email", "listener@example.test"},
                         {"email_verified", true},
                         {"name", "Listener"}};
    QList<QUrlQuery> tokenRequests;
    QList<QByteArray> userInfoHeaders;
    int tokenStatus = 200;
    MockGoogle() {
        server.listen(QHostAddress::LocalHost);
        QObject::connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    auto bytes = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", bytes);
                    auto end = bytes.indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    int length = 0;
                    for (auto line : bytes.left(end).split('\n'))
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(15).trimmed().toInt();
                    if (bytes.size() < end + 4 + length)
                        return;
                    auto path = bytes.split(' ').value(1);
                    QJsonObject result;
                    int status = 200;
                    if (path == "/token") {
                        tokenRequests.append(QUrlQuery(QString::fromUtf8(bytes.mid(end + 4, length))));
                        result = token;
                        status = tokenStatus;
                    } else if (path == "/userinfo") {
                        userInfoHeaders.append(bytes.left(end));
                        result = identity;
                    } else
                        status = 404;
                    auto body = QJsonDocument(result).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 " + QByteArray::number(status) +
                                  " Response\r\nContent-Type: application/json\r\nConnection: "
                                  "close\r\nContent-Length: " +
                                  QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    GoogleOAuthOptions options(QUrl &opened) {
        GoogleOAuthOptions opts;
        opts.persistSession = false;
        opts.timeoutMs = 5000;
        opts.openBrowser = [&opened](const QUrl &url) {
            opened = url;
            return true;
        };
        opts.tokenEndpoint = QUrl(QString("http://127.0.0.1:%1/token").arg(server.serverPort()));
        opts.userInfoEndpoint = QUrl(QString("http://127.0.0.1:%1/userinfo").arg(server.serverPort()));
        return opts;
    }
};
QUrl callbackUrl(const QUrl &opened, QString code = "test-code", QString state = {}) {
    const QUrlQuery auth(opened);
    QUrl url(auth.queryItemValue("redirect_uri"));
    QUrlQuery params;
    params.addQueryItem("code", code);
    params.addQueryItem("state", state.isEmpty() ? auth.queryItemValue("state") : state);
    url.setQuery(params);
    return url;
}
GoogleOAuthConfig config() {
    return {"velune-test.apps.googleusercontent.com", {}};
}
} // namespace
class BrowserCapture : public QObject {
    Q_OBJECT
  public:
    QUrl opened;
  public slots:
    void open(const QUrl &url) {
        opened = url;
    }
};
class GoogleAuthTests : public QObject {
    Q_OBJECT
  private slots:
    void usesQtSystemBrowserHandoff() {
        BrowserCapture browser;
        QDesktopServices::setUrlHandler("https", &browser, "open");
        GoogleOAuthOptions options;
        options.persistSession = false;
        GoogleAuth auth(config(), "test", options);
        auth.start();
        QDesktopServices::unsetUrlHandler("https");
        QCOMPARE(browser.opened.host(), "accounts.google.com");
        QCOMPARE(QUrlQuery(browser.opened).queryItemValue("prompt"), "select_account consent");
        QVERIFY(!auth.authenticated());
        auth.cancel();
    }
    void missingRegistrationDoesNotFakeLogin() {
        QUrl opened;
        GoogleOAuthOptions opts;
        opts.persistSession = false;
        opts.openBrowser = [&](auto url) {
            opened = url;
            return true;
        };
        GoogleAuth auth({}, "test", opts);
        QSignalSpy errors(&auth, &GoogleAuth::error);
        auth.start();
        QVERIFY(!opened.isValid());
        QVERIFY(!auth.authenticated());
        QVERIFY(!auth.busy());
        QCOMPARE(errors.size(), 1);
    }
    void opensSystemBrowserWithAccountChoiceAndPkce() {
        MockGoogle mock;
        QUrl opened;
        GoogleAuth auth(config(), "test", mock.options(opened));
        auth.start();
        QVERIFY(auth.busy());
        QVERIFY(!auth.authenticated());
        QCOMPARE(opened.scheme(), "https");
        QCOMPARE(opened.host(), "accounts.google.com");
        QUrlQuery query(opened);
        QCOMPARE(query.queryItemValue("prompt"), "select_account consent");
        QCOMPARE(query.queryItemValue("response_type"), "code");
        QCOMPARE(query.queryItemValue("code_challenge_method"), "S256");
        QVERIFY(query.queryItemValue("state").size() >= 43);
        QVERIFY(query.queryItemValue("code_challenge").size() >= 43);
        QUrl callback(query.queryItemValue("redirect_uri"));
        QCOMPARE(callback.host(), "127.0.0.1");
        QVERIFY(callback.port() > 0);
        QVERIFY(!query.hasQueryItem("client_secret"));
        auth.cancel();
        QVERIFY(!auth.busy());
    }
    void validatesCallbackThenAutomaticallyConnectsAccount() {
        MockGoogle mock;
        QUrl opened;
        GoogleAuth auth(config(), "test", mock.options(opened));
        QSignalSpy errors(&auth, &GoogleAuth::error);
        auth.start();
        QNetworkAccessManager browser;
        auto wrong = browser.get(QNetworkRequest(callbackUrl(opened, "not-a-valid-code", "wrong-state")));
        QSignalSpy wrongDone(wrong, &QNetworkReply::finished);
        QTRY_COMPARE(wrongDone.size(), 1);
        QCOMPARE(wrong->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 400);
        wrong->deleteLater();
        QVERIFY(auth.busy());
        QVERIFY(mock.tokenRequests.isEmpty());
        QVERIFY(!auth.authenticated());
        auto accepted = browser.get(QNetworkRequest(callbackUrl(opened, "test+code/with space")));
        QSignalSpy acceptedDone(accepted, &QNetworkReply::finished);
        QTRY_COMPARE(acceptedDone.size(), 1);
        QCOMPARE(accepted->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        accepted->deleteLater();
        QTRY_VERIFY(auth.authenticated());
        QVERIFY(!auth.busy());
        QCOMPARE(auth.accountEmail(), "listener@example.test");
        QCOMPARE(auth.accountName(), "Listener");
        QCOMPARE(mock.tokenRequests.size(), 1);
        auto form = mock.tokenRequests[0];
        QCOMPARE(form.queryItemValue("grant_type"), "authorization_code");
        QCOMPARE(form.queryItemValue("code", QUrl::FullyDecoded), "test+code/with space");
        auto verifier = form.queryItemValue("code_verifier");
        QVERIFY(verifier.size() >= 43);
        auto digest = QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
                          .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        QCOMPARE(QString::fromLatin1(digest), QUrlQuery(opened).queryItemValue("code_challenge"));
        QCOMPARE(form.queryItemValue("redirect_uri", QUrl::FullyDecoded),
                 QUrlQuery(opened).queryItemValue("redirect_uri"));
        QCOMPARE(mock.userInfoHeaders.size(), 1);
        QVERIFY(mock.userInfoHeaders[0].contains("Authorization: Bearer test-access"));
        QVERIFY(errors.isEmpty());
        mock.token["access_token"] = "refreshed-access";
        auth.refresh();
        QTRY_COMPARE(mock.tokenRequests.size(), 2);
        QTRY_VERIFY(!auth.busy());
        QVERIFY(auth.authenticated());
        QCOMPARE(auth.accessToken(), "refreshed-access");
        QCOMPARE(mock.tokenRequests[1].queryItemValue("grant_type"), "refresh_token");
        QCOMPARE(mock.tokenRequests[1].queryItemValue("refresh_token"), "test-refresh");
        mock.tokenStatus = 400;
        auth.refresh();
        QTRY_VERIFY(!auth.busy());
        QVERIFY(!auth.authenticated());
        QVERIFY(auth.accessToken().isEmpty());
        QCOMPARE(errors.size(), 1);
        auth.logout();
        QVERIFY(!auth.authenticated());
        QVERIFY(auth.accessToken().isEmpty());
    }
    void deniedConsentAndReplayDoNotConnect() {
        MockGoogle mock;
        QUrl opened;
        GoogleAuth auth(config(), "test", mock.options(opened));
        QSignalSpy errors(&auth, &GoogleAuth::error);
        auth.start();
        QUrl url = callbackUrl(opened);
        QUrlQuery query(url);
        query.removeAllQueryItems("code");
        query.addQueryItem("error", "access_denied");
        url.setQuery(query);
        QNetworkAccessManager browser;
        auto reply = browser.get(QNetworkRequest(url));
        QSignalSpy done(reply, &QNetworkReply::finished);
        QTRY_COMPARE(done.size(), 1);
        QTRY_COMPARE(errors.size(), 1);
        QVERIFY(!auth.authenticated());
        QVERIFY(!auth.busy());
        QVERIFY(mock.tokenRequests.isEmpty());
        reply->deleteLater();
        auto replay = browser.get(QNetworkRequest(callbackUrl(opened)));
        QSignalSpy replayDone(replay, &QNetworkReply::finished);
        QTRY_COMPARE(replayDone.size(), 1);
        QVERIFY(replay->error() != QNetworkReply::NoError);
        replay->deleteLater();
    }
    void rejectedTokenAndUnverifiedIdentityDoNotConnect() {
        for (bool rejectToken : {true, false}) {
            MockGoogle mock;
            QUrl opened;
            if (rejectToken)
                mock.tokenStatus = 400;
            else
                mock.identity["email_verified"] = false;
            GoogleAuth auth(config(), "test", mock.options(opened));
            QSignalSpy errors(&auth, &GoogleAuth::error);
            auth.start();
            QNetworkAccessManager browser;
            auto reply = browser.get(QNetworkRequest(callbackUrl(opened)));
            QTRY_VERIFY(!errors.isEmpty());
            QVERIFY(!auth.authenticated());
            QVERIFY(auth.accessToken().isEmpty());
            reply->deleteLater();
        }
    }
    void missingYouTubePermissionDoesNotConnect() {
        MockGoogle mock;
        mock.token["scope"] = "openid email profile";
        QUrl opened;
        GoogleAuth auth(config(), "test", mock.options(opened));
        QSignalSpy errors(&auth, &GoogleAuth::error);
        auth.start();
        QNetworkAccessManager browser;
        auto reply = browser.get(QNetworkRequest(callbackUrl(opened)));
        QTRY_VERIFY(!errors.isEmpty());
        QVERIFY(!auth.authenticated());
        QVERIFY(mock.userInfoHeaders.isEmpty());
        reply->deleteLater();
    }
    void browserFailureAndCancellationDoNotLeaveCallbackOpen() {
        MockGoogle mock;
        QUrl opened;
        auto options = mock.options(opened);
        options.openBrowser = [](auto) { return false; };
        GoogleAuth auth(config(), "test", options);
        QSignalSpy errors(&auth, &GoogleAuth::error);
        auth.start();
        QCOMPARE(errors.size(), 1);
        QVERIFY(!auth.busy());
        auto normal = mock.options(opened);
        GoogleAuth second(config(), "test", normal);
        second.start();
        const auto old = opened;
        second.cancel();
        QNetworkAccessManager browser;
        auto reply = browser.get(QNetworkRequest(callbackUrl(old)));
        QSignalSpy done(reply, &QNetworkReply::finished);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(reply->error() != QNetworkReply::NoError);
        QVERIFY(!second.authenticated());
        reply->deleteLater();
        second.start();
        QVERIFY(QUrlQuery(opened).queryItemValue("state") != QUrlQuery(old).queryItemValue("state"));
        second.cancel();
    }
};
QTEST_MAIN(GoogleAuthTests)
#include "GoogleAuthTests.moc"
