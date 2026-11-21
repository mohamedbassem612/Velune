#pragma once
#include "core/Library.h"
#include <QFile>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSslKey>
#include <QSslSocket>
#include <QTimer>
namespace velune {
class ConnectClient : public QObject {
    Q_OBJECT
  public:
    explicit ConnectClient(Library *library, QObject *parent = nullptr);
    ~ConnectClient() override;
    void attach(QString host, quint16 port, QString fingerprint);
    void detach();
    bool connected() const {
        return authorized && sessionReady;
    }
    bool hasTarget() const {
        return !explicitDisconnect;
    }
    QJsonObject playback() const {
        return state;
    }
    QString peerName() const {
        return name;
    }
    QString identity() const {
        return localId;
    }
    void command(QJsonObject command);
    void play(const QList<Track> &tracks, int index = 0);
    static bool localAddress(const QHostAddress &address);
    static QByteArray fingerprint(const QSslCertificate &certificate);
    static bool decodeFrame(QByteArray &buffer, qint64 &sequence, QJsonObject &message, QString &error);
  signals:
    void stateChanged(QJsonObject state);
    void statusChanged(QString status);
    void error(QString message);

  private:
    Library *library;
    QSslSocket *socket = nullptr;
    QSslCertificate certificate;
    QSslKey key;
    QString localId, expected, name, host;
    quint16 port = 0;
    QJsonObject state, publishedLibrary;
    QString libraryExchange;
    QByteArray buffer;
    qint64 outgoing = 0, incoming = 0;
    bool authorized = false, sessionReady = false, explicitDisconnect = true;
    QTimer heartbeat, watchdog, retry;
    int attempts = 0;
    QMap<QString, QFile *> readers;
    QMap<QString, QString> openedByRequest;
    QMap<QString, QTimer *> pendingCommands;
    void send(QJsonObject message);
    void publishLibrary(bool retryPending = false);
    void receive(QJsonObject message);
    void read();
    void open();
    void fail(const QString &message);
    void media(const QJsonObject &message);
    bool createIdentity();
};
} // namespace velune
