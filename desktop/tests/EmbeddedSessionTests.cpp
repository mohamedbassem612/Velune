#include "services/EmbeddedSession.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QtTest>
using namespace velune;
// Controlled replies verify account acceptance without contacting Google.
class AccountReply : public QNetworkReply {
  public:
    QByteArray data;
    AccountReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
    }
    void complete(QByteArray body) {
        data = body;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    void abort() override {}
    qint64 bytesAvailable() const override {
        return data.size() + QNetworkReply::bytesAvailable();
    }
    qint64 readData(char *out, qint64 maximum) override {
        const auto size = qMin(maximum, qint64(data.size()));
        if (!size)
            return -1;
        memcpy(out, data.constData(), size_t(size));
        data.remove(0, size);
        return size;
    }
};
class AccountNetwork : public QNetworkAccessManager {
  public:
    QPointer<AccountReply> latest;

  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        latest = new AccountReply(request, this);
        return latest;
    }
};
class EmbeddedSessionTests : public QObject {
    Q_OBJECT
  private slots:
    void originAndExpiryIsolation() {
        const auto now = QDateTime::currentDateTimeUtc();
        QNetworkCookie valid("SAPISID", "secret");
        valid.setDomain(".youtube.com");
        valid.setPath("/");
        valid.setSecure(true);
        auto google = valid;
        google.setDomain(".google.com");
        auto suffix = valid;
        suffix.setDomain(".youtube.com.attacker.test");
        auto sibling = valid;
        sibling.setDomain("www.youtube.com");
        auto hostOnly = valid;
        hostOnly.setDomain("youtube.com");
        auto expired = valid;
        expired.setExpirationDate(now.addSecs(-1));
        auto path = valid;
        path.setPath("/restricted");
        auto injection = valid;
        injection.setValue("secret\r\nInjected: value");
        auto semicolon = valid;
        semicolon.setValue("secret; Other=1");
        const QList<QNetworkCookie> rejected{google,  suffix, sibling,   hostOnly,
                                             expired, path,   injection, semicolon};
        QVERIFY(musicCookieHeader(rejected, now).isEmpty());
        QVERIFY(musicAuthorization(rejected, now.toSecsSinceEpoch()).isEmpty());
        auto all = rejected;
        all.append(valid);
        QCOMPARE(musicCookieHeader(all, now), QByteArray("SAPISID=secret"));
        const auto stamp = QByteArray::number(now.toSecsSinceEpoch());
        const auto hash =
            QCryptographicHash::hash(stamp + " secret https://music.youtube.com", QCryptographicHash::Sha1)
                .toHex();
        QCOMPARE(musicAuthorization(all, now.toSecsSinceEpoch()), "SAPISIDHASH " + stamp + '_' + hash);
    }
    void embeddedWindowAndCancellation() {
        QTemporaryDir directory;
        QWidget parent;
        EmbeddedSession session(directory.path());
        QSignalSpy authenticated(&session, &EmbeddedSession::authenticatedChanged);
        session.start(&parent);
        auto dialog = parent.findChild<QDialog *>("embeddedSignIn");
        QVERIFY(dialog);
        auto view = dialog->findChild<QWebEngineView *>();
        QVERIFY(view);
        view->stop();
        QVERIFY(view->page()->profile()->isOffTheRecord());
        QCOMPARE(view->page()->profile()->persistentCookiesPolicy(), QWebEngineProfile::NoPersistentCookies);
        QVERIFY(session.busy());
        QVERIFY(session.accountName().isEmpty());
        QCOMPARE(authenticated.size(), 0);
        session.cancel();
        QVERIFY(!session.busy());
        QVERIFY(!parent.findChild<QDialog *>("embeddedSignIn"));
        QCOMPARE(authenticated.size(), 0);
        // Repeated open/close uses the same isolated profile, without creating an account.
        session.start(&parent);
        auto second = parent.findChild<QDialog *>("embeddedSignIn");
        QVERIFY(second);
        second->findChild<QWebEngineView *>()->stop();
        session.cancel();
        QCOMPARE(authenticated.size(), 0);
    }
    void accountMustBeConfirmedAndCancellationRejectsLateReply() {
        QTemporaryDir directory;
        QWidget parent;
        AccountNetwork transport;
        EmbeddedSession session(directory.path(), nullptr, &transport, false);
        QSignalSpy authenticated(&session, &EmbeddedSession::authenticatedChanged);
        auto prepare = [&] {
            transport.latest.clear();
            session.start(&parent);
            auto view = parent.findChild<QWebEngineView *>();
            QVERIFY(view);
            view->stop();
            QSignalSpy loaded(view, &QWebEngineView::loadFinished);
            view->setHtml("<html><body>Local test</body></html>", QUrl("https://music.youtube.com/"));
            QTRY_VERIFY_WITH_TIMEOUT(loaded.size() > 0, 5000);
            QNetworkCookie cookie("SAPISID", "test-only-not-a-real-account");
            cookie.setDomain(".youtube.com");
            cookie.setPath("/");
            cookie.setSecure(true);
            view->page()->profile()->cookieStore()->setCookie(cookie, QUrl("https://music.youtube.com/"));
            // loadFinished provides a normal verification trigger; wait for Chromium's cookie event first.
            QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                         QMetaObject::invokeMethod(view, "loadFinished", Q_ARG(bool, true));
                                         return !transport.latest.isNull();
                                     })(),
                                     5000);
        };
        prepare();
        QVERIFY(transport.latest);
        QVERIFY(session.busy());
        QCOMPARE(authenticated.size(), 0); // A cookie alone has not opened the account.
        QCOMPARE(transport.latest->url().host(), QString("music.youtube.com"));
        transport.latest->complete(
            R"({"actions":[{"activeAccountHeaderRenderer":{"accountName":{"runs":[{"text":"Test account"}]},"email":{"runs":[{"text":"test@example.invalid"}]}}}]})");
        QCOMPARE(authenticated.size(), 1);
        QCOMPARE(authenticated[0][0].toBool(), true);
        QCOMPARE(session.accountName(), QString("Test account"));
        QVERIFY(!session.busy());
        QVERIFY(!parent.findChild<QDialog *>("embeddedSignIn"));
        session.logout();
        authenticated.clear();
        prepare();
        QVERIFY(transport.latest);
        transport.latest->complete(R"({"actions":[]})");
        QVERIFY(session.busy());
        QCOMPARE(authenticated.size(), 0);
        QVERIFY(session.accountName().isEmpty());
        session.cancel();
        prepare();
        QVERIFY(transport.latest);
        auto lateReply = transport.latest;
        session.cancel();
        lateReply->complete(
            R"({"activeAccountHeaderRenderer":{"accountName":{"simpleText":"Late account"}}})");
        QCOMPARE(authenticated.size(), 0);
        QVERIFY(session.accountName().isEmpty());
        QVERIFY(!session.busy());
    }
    void credentialsNeverLeaveMusicApi() {
        QTemporaryDir directory;
        EmbeddedSession session(directory.path());
        for (const auto &url :
             {"http://music.youtube.com/youtubei/v1/browse",
              "https://music.youtube.com.attacker.test/youtubei/v1/browse",
              "https://music.youtube.com:444/youtubei/v1/browse",
              "https://accounts.google.com/youtubei/v1/browse", "https://music.youtube.com/watch"}) {
            QNetworkRequest request{QUrl(url)};
            session.authorize(request);
            QVERIFY(!request.hasRawHeader("Authorization"));
            QVERIFY(!request.hasRawHeader("Cookie"));
        }
        QSignalSpy connected(&session, &EmbeddedSession::authenticatedChanged);
        session.cancel();
        QVERIFY(!session.busy());
        QVERIFY(session.accountName().isEmpty());
        QCOMPARE(connected.size(), 0);
    }
};
QTEST_MAIN(EmbeddedSessionTests)
#include "EmbeddedSessionTests.moc"
