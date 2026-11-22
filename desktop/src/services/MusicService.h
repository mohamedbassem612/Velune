#pragma once
#include "AudioResolver.h"
#include "AudioStream.h"
#include "ConnectClient.h"
#include "EmbeddedSession.h"
#include "LibraryCatalog.h"
#include "SearchEngine.h"
#include "core/Library.h"
#include "playback/IPlaybackEngine.h"
#include "playback/PlaybackTrace.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <memory>

#include <functional>
class QWidget;
namespace velune {
class MusicService : public QObject {
    Q_OBJECT
  public:
    MusicService(Library *library, ConnectClient *connect, QObject *parent = nullptr);
    MusicService(Library *library, ConnectClient *connect, std::unique_ptr<IPlaybackEngine> engine,
                 QObject *parent = nullptr);
    ~MusicService() override;
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
    LibraryCatalog *libraryCatalog() {
        return &catalog;
    }
    void refreshCatalog(QStringList kinds);
    void search(QString query);
    SearchEngine *searchSystem() {
        return &searchEngine;
    }
    QJsonArray localSearchItems(bool offlineOnly = false) const;
    void suggest(QString query);
    static QJsonObject parseSuggestions(const QJsonObject &response);
    void playBrowse(QString browseId, QString playlistId = {});
    void cancelSearch();
    void explore();
    void radio(const Track &seed);
    void browseCollection(QString token, const Track &seed, bool radio);
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
    void searchStarted(QString query);
    void suggestionsStarted(QString query);
    void suggestionsReady(QJsonObject suggestions);
    void collectionLoaded(QString token, QList<velune::Track> tracks);
    void collectionFailed(QString token, QString reason);
    void playbackChanged(QJsonObject state);
    void message(QString message);

  private:
    Library *library;
    ConnectClient *remote;
    QNetworkAccessManager network;
    QPointer<QNetworkReply> searchReply;
    EmbeddedSession auth;
    SearchEngine searchEngine;
    LibraryCatalog catalog;
    AudioResolver resolver, prefetch;
    QTimer prepareNext;
    AudioStream stream;
    bool wantsPlayback = false, retriedStream = false;
    qint64 pendingSeek = -1;
    QJsonObject current, localCheckpoint;
    QList<Track> queue;
    QString account;
    bool authenticated = false, shuffled = false, remoteRoute = false;
    int repeatMode = 0, requestGeneration = 0, libraryGeneration = 0;
    PlaybackRequest playbackGeneration = 0, endedGeneration = 0;
    quint64 streamGeneration = 0, radioGeneration = 0;
    std::unique_ptr<IPlaybackEngine> engine;
    PlaybackTrace trace;
    void cancelPlayback();
    void updateRoute();
    bool active(PlaybackRequest request) const;
    QNetworkReply *post(const QString &endpoint, QJsonObject body, std::function<void(QJsonObject)> done,
                        bool useSession = true);
    void loadLibrary(QString continuation = {}, QList<Track> accumulated = {});
    void bindSourceCallbacks(PlaybackRequest generation);
    void resolveAndPlay(const Track &track, PlaybackRequest generation);
    bool remoteUnavailable();
    static QList<Track> parseTracks(const QJsonValue &value);
};
} // namespace velune
Q_DECLARE_METATYPE(velune::Track)
