#pragma once
#include "ConnectClient.h"
#include "GoogleAuth.h"
#include "core/Library.h"
#include <QNetworkAccessManager>

#include <functional>
#ifdef VELUNE_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#endif
class QWidget;
namespace velune {
class MusicService : public QObject {
    Q_OBJECT
  public:
    MusicService(Library *library, ConnectClient *connect, QObject *parent = nullptr);
    bool loggedIn() const {
        return authenticated;
    }
    QString accountName() const {
        return account;
    }
    QString accountEmail() const {
        return auth.accountEmail();
    }
    bool loginPending() const {
        return auth.busy();
    }
    void login(QWidget *parent);
    void logout();
    void search(QString query);
    void explore();
    void radio(const Track &seed);
    void play(QList<Track> tracks, int index = 0);
    void action(QString name);
    void seek(qint64 position);
    void volume(int volume);
    void shuffle(bool enabled);
    void repeat(int mode);
    void importAudio(QWidget *parent);
    QJsonObject playback() const {
        return current;
    }
  signals:
    void authenticationChanged(bool loggedIn);
    void loginPendingChanged(bool pending);
    void results(QList<velune::Track> tracks);
    void playbackChanged(QJsonObject state);
    void message(QString message);

  private:
    Library *library;
    ConnectClient *remote;
    QNetworkAccessManager network;
    GoogleAuth auth;
    QJsonObject current, localCheckpoint;
    QList<Track> queue;
    QString account;
    bool authenticated = false, shuffled = false, remoteRoute = false;
    int repeatMode = 0, requestGeneration = 0, playbackGeneration = 0, libraryGeneration = 0;
#ifdef VELUNE_MULTIMEDIA
    QMediaPlayer player;
    QAudioOutput output;
#endif
    void post(const QString &endpoint, QJsonObject body, std::function<void(QJsonObject)> done);
    void loadLibrary(QString continuation = {}, QList<Track> accumulated = {});
    void resolveAndPlay(const Track &track, int generation);
    bool remoteUnavailable();
    static QList<Track> parseTracks(const QJsonValue &value);
};
} // namespace velune
Q_DECLARE_METATYPE(velune::Track)
