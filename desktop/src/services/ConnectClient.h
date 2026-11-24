#pragma once
#include "core/Library.h"
#include <QFile>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QSslKey>
#include <QSslSocket>
#include <QTimer>
#include <functional>
namespace velune {
class ConnectHost;
class ConnectMediaProvider;
class ConnectClient : public QObject {
    Q_OBJECT
  public:
    explicit ConnectClient(Library *library, QObject *parent = nullptr);
    ~ConnectClient() override;
    void attach(QString host, quint16 port, QString fingerprint);
    void detach();
    bool connected() const;
    bool hasTarget() const;
    bool hosting() const;
    bool rendersLocally() const;
    quint16 listeningPort() const;
    void chooseThisComputer();
    void publishRendererState(QJsonObject snapshot);
    void rememberLocalPlayback(QJsonObject snapshot);
    void prepared(QString transferId, bool ok);
    void released(QString transferId, QJsonObject snapshot);
    QStringList mediaProviders(bool assets = false) const;
    QJsonArray peerInfo() const;
    QString requestMedia(QString provider, QJsonObject request, std::function<void(QJsonObject)> done);
    void cancelMedia(QString requestId);
    void setProviderCookies(std::function<QByteArray()>);
    bool mediaBusy() const;
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
    void serverChanged();
    void prepareRequested(QString transferId, QJsonObject state);
    void releaseRequested(QString transferId);
    void abortRequested(QString transferId);
    void commandRequested(QJsonObject command);

  private:
    ConnectMediaProvider *provider = nullptr;
    Library *library;
    ConnectHost *hostServer = nullptr;
    QSslSocket *socket = nullptr;
    QSslCertificate certificate;
    QSslKey key;
    QString localId, expected, name, host;
    quint16 port = 0;
    QJsonObject state, publishedLibrary;
    QString libraryExchange;
    QByteArray buffer;
    qint64 outgoing = 0, incoming = 0;
    int peerMediaVersion = 0, peerAssetVersion = 0;
    bool authorized = false, sessionReady = false, explicitDisconnect = true;
    QTimer heartbeat, watchdog, retry;
    int attempts = 0;
    QMap<QString, QTimer *> pendingCommands;
    struct MediaCall {
        QString provider;
        QTimer *timer;
        std::function<void(QJsonObject)> done;
    };
    QMap<QString, MediaCall> mediaCalls;
    QJsonObject lastLocalState;
    QString preparation;
    QSet<QString> rendererCommands;
    QStringList rendererCommandOrder;
    qint64 rendererSequence = 0;
    bool initialSelection = false;
    void mediaResponse(QString peer, QJsonObject reply);
    void cancelPeerMedia(QString peer);
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
