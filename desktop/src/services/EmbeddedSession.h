#pragma once
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUrl>
namespace velune {
// Only cookies belonging to the YouTube Music origin may reach InnerTube.
QByteArray musicCookieHeader(const QList<QNetworkCookie> &cookies, const QDateTime &now);
QByteArray musicAuthorization(const QList<QNetworkCookie> &cookies, qint64 seconds);
class EmbeddedSession : public QObject {
    Q_OBJECT
  public:
    explicit EmbeddedSession(QString directory, QObject *parent = nullptr,
                             QNetworkAccessManager *verificationNetwork = nullptr,
                             bool persistSession = true);
    ~EmbeddedSession() override;
    bool busy() const {
        return pending;
    }
    QString accountName() const {
        return name;
    }
    QString accountEmail() const {
        return email;
    }
    void start();
    void cancel();
    void logout();
    void restore();
    void authorize(QNetworkRequest &request) const;
    QByteArray playbackCookies() const;
  signals:
    void busyChanged(bool pending);
    void authenticatedChanged(bool authenticated);
    void status(QString message);
    void error(QString message);

  private:
    QString directory, storageKey, name, email;
    QList<QNetworkCookie> cookies;
    QPointer<QProcess> helper;
    QByteArray helperOutput;
    QNetworkAccessManager network;
    QNetworkAccessManager *verificationNetwork;
    QTimer retry, saveTimer;
    bool pending = false, verified = false, checking = false, persistSession;
    quint64 generation = 0, cookieRevision = 0;
    void verify();
    void save();
    void setBusy(bool value);
    void closeDialog();
};
} // namespace velune
