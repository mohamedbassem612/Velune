#pragma once
#include "AudioResolver.h"
#include "AudioStream.h"
#include "CacheStorage.h"
#include "ConnectClient.h"
#include "ConnectMediaCache.h"
#include "DownloadService.h"
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

namespace velune {
class MusicService : public QObject {
    Q_OBJECT
  public:
    MusicService(Library *library, ConnectClient *connect, QObject *parent = nullptr);
    MusicService(Library *library, ConnectClient *connect, std::unique_ptr<IPlaybackEngine> engine,
                 QObject *parent = nullptr, QNetworkAccessManager *playbackNetwork = nullptr);
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
    void login();
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
    void playBrowse(QString browseId, QString playlistId = {});
    void cancelSearch();
    void explore();
    void radio(const Track &seed);
    void browseCollection(QString token, const Track &seed, bool radio);
    void play(QList<Track> tracks, int index = 0);
    void action(QString name);
    void command(QJsonObject command);
    void seek(qint64 position);
    void volume(int volume);
    void shuffle(bool enabled);
    void repeat(int mode);
    void importAudio(const QStringList &paths);
    QJsonObject playback() const {
        return current;
    }
    QString connectedCover(QString song) const {
        return peerCache->cover(song);
    }
    QString connectedLyrics(QString song) const {
        return peerCache->lyrics(song);
    }
    DownloadService *downloads() const {
        return downloadService;
    }
    CacheStorage *cacheStorage() const {
        return cacheService;
    }
    void clearCache(QString kind);
    void checkpoint() {
        catalog.checkpoint();
        savePlayback();
    }
    bool canRestoreBackup() const {
        return !remote->hasTarget() && !remote->mediaBusy() && !peerCache->busy() && !downloadService->busy();
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
    AudioResolver *resolver, *prefetch, *fadeResolver;
    ConnectMediaCache *peerCache = nullptr;
    DownloadService *downloadService = nullptr;
    CacheStorage *cacheService = nullptr;
    QTimer connectTicker, prepareDeadline;
    QString preparedTransfer, renderedSession;
    QJsonObject preparedBackup, preparedSnapshot;
    qint64 renderedEpoch = -1;
    bool connectRenderer = false, connectLoaded = false, releasingConnect = false;
    qint64 sleepUpdatedAt = 0;
    QTimer prepareNext;
    QTimer playbackSaveTimer;
    QByteArray savedPlayback;
    std::unique_ptr<AudioStream> stream, preparedStream, retiringStream;
    quint64 fadeToken = 0;
    int fadeIndex = -1;
    bool fadeReady = false;
    PlaybackRequest fadeGeneration = 0;
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
    void cancelPlayback(bool keepPrefetch = false);
    void updateRoute();
    bool active(PlaybackRequest request) const;
    QNetworkReply *post(const QString &endpoint, QJsonObject body, std::function<void(QJsonObject)> done,
                        bool useSession = true);
    void loadLibrary(QString continuation = {}, QList<Track> accumulated = {});
    void bindSourceCallbacks(PlaybackRequest generation);
    void resolveAndPlay(const Track &track, PlaybackRequest generation, qint64 position = 0);
    void savePlayback();
    void restorePlayback();
    void prepareCrossfade();
    void discardCrossfade();
    void tryCrossfade();
    bool remoteUnavailable();
    void setupConnect();
    QJsonObject connectSnapshot() const;
    void applyConnectState(QJsonObject);
    void prepareConnect(QString, QJsonObject);
    void abortConnect(QString);
    void startConnectTrack(QJsonObject);
    void executeConnectCommand(QJsonObject);
    void connectFailed(QString);
    void connectMediaReady();
    static QList<Track> parseTracks(const QJsonValue &value);
};
} // namespace velune
Q_DECLARE_METATYPE(velune::Track)
