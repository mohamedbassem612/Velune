#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QSet>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <functional>
namespace velune {
struct GoogleOAuthConfig {
    QString clientId, clientSecret;
    bool valid() const;
    static GoogleOAuthConfig load(const QString &directory);
};
struct GoogleOAuthOptions {
    QUrl authorizationEndpoint{"https://accounts.google.com/o/oauth2/v2/auth"};
    QUrl tokenEndpoint{"https://oauth2.googleapis.com/token"};
    QUrl userInfoEndpoint{"https://openidconnect.googleapis.com/v1/userinfo"};
    std::function<bool(const QUrl &)> openBrowser;
    bool persistSession = true;
    int timeoutMs = 180000;
};
class GoogleAuth : public QObject {
    Q_OBJECT
  public:
    GoogleAuth(GoogleOAuthConfig config, QString storageKey, QObject *parent = nullptr);
    GoogleAuth(GoogleOAuthConfig config, QString storageKey, GoogleOAuthOptions options,
               QObject *parent = nullptr);
    ~GoogleAuth() override;
    bool authenticated() const {
        return verified;
    }
    bool busy() const {
        return pending;
    }
    QString accountName() const {
        return profile.value("name").toString();
    }
    QString accountEmail() const {
        return profile.value("email").toString();
    }
    QString accessToken() const {
        return bearer;
    }
    void start();
    void cancel();
    void logout();
    void restore();
    void refresh();
  signals:
    void authenticatedChanged(bool authenticated);
    void busyChanged(bool busy);
    void status(QString message);
    void error(QString message);

  private:
    GoogleOAuthConfig config;
    QString storageKey;
    GoogleOAuthOptions options;
    QTcpServer callback;
    QNetworkAccessManager network;
    QTimer deadline, refreshTimer;
    QSet<QNetworkReply *> requests;
    QSet<QTcpSocket *> clients;
    QJsonObject profile;
    QString state, verifier, redirect, bearer, refreshToken, scopes;
    bool verified = false, pending = false, refreshingSession = false;
    int generation = 0;
    void acceptCallback();
    void consumeCallback(QTcpSocket *socket, const QByteArray &header);
    void respond(QTcpSocket *socket, int code, QString title, QString text);
    void exchange(const QString &code);
    void requestToken(QUrlQuery form, bool refreshing);
    void verifyToken(const QJsonObject &tokens, bool refreshing);
    void request(const QUrl &url, const QByteArray &body, const QByteArray &access,
                 std::function<void(QJsonObject)> done);
    void setBusy(bool busy);
    void resetFlow();
    void fail(QString message);
    void saveSession();
    void clearSession();
};
} // namespace velune
