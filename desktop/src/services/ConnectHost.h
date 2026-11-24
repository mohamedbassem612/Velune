#pragma once
#include "core/Library.h"
#include <QPointer>
#include <QSslConfiguration>
#include <QTcpServer>
#include <QTimer>
#include <functional>
namespace velune {
class ConnectPeer;
class ConnectListener : public QTcpServer {
  public:
    using QTcpServer::QTcpServer;
    std::function<void(qintptr)> accepted;

  protected:
    void incomingConnection(qintptr descriptor) override {
        if (accepted)
            accepted(descriptor);
    }
};
// Protocol 4 coordinator. Audio ownership changes only after prepare/release.
class ConnectHost : public QObject {
    Q_OBJECT
  public:
    ConnectHost(Library *, QString id, QSslConfiguration, QObject *parent);
    ~ConnectHost() override;
    void setEnabled(bool);
    quint16 port() const {
        return listener.serverPort();
    }
    bool active() const;
    void disengage();
    QJsonObject playback() const {
        return state;
    }
    QStringList providers(bool assets = false) const;
    QJsonArray peerInfo() const;
    void sendMedia(QString peer, QJsonObject message);
    void command(QJsonObject);
    void transfer(QString target);
    void localState(QJsonObject);
    void prepared(QString transferId, bool ok);
    void released(QString transferId, QJsonObject snapshot);
  signals:
    void changed(QJsonObject);
    void availabilityChanged();
    void error(QString);
    void prepareRequested(QString, QJsonObject);
    void releaseRequested(QString);
    void abortRequested(QString);
    void commandRequested(QJsonObject);
    void mediaReply(QString peer, QJsonObject);
    void mediaRequest(QString peer, QJsonObject);
    void peerLost(QString peer);

  private:
    Library *library;
    QString id;
    QSslConfiguration tls;
    ConnectListener listener;
    QList<ConnectPeer *> peers;
    QJsonObject state, publishedLibrary, before;
    QString transferId, destination, source, stage;
    QTimer tick, transferTimer;
    bool enabled = false, releaseSent = false, engaged = false;
    void accept(qintptr);
    void receive(ConnectPeer *, QJsonObject);
    void send(ConnectPeer *, QJsonObject);
    void broadcast();
    bool persist();
    void publishLibrary(ConnectPeer *);
    void lost(ConnectPeer *);
    void failTransfer();
    void update(QJsonObject);
    QJsonObject envelope(QString type) const;
    ConnectPeer *peer(QString) const;
};
} // namespace velune
