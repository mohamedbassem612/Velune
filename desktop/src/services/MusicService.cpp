#include "MusicService.h"
#include "playback/PlaybackEngineFactory.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrlQuery>
#include <cmath>
namespace velune {
static QString textOf(QJsonObject o) {
    if (o["simpleText"].isString())
        return o["simpleText"].toString();
    QString s;
    for (auto v : o["runs"].toArray())
        s += v.toObject()["text"].toString();
    return s;
}
static QJsonValue findKey(const QJsonValue &v, const QString &key) {
    if (v.isObject()) {
        auto o = v.toObject();
        if (o.contains(key))
            return o[key];
        for (auto x : o) {
            auto r = findKey(x, key);
            if (!r.isUndefined() && !r.isNull())
                return r;
        }
    } else if (v.isArray())
        for (auto x : v.toArray()) {
            auto r = findKey(x, key);
            if (!r.isUndefined() && !r.isNull())
                return r;
        }
    return {};
}
MusicService::MusicService(Library *l, ConnectClient *c, QObject *parent)
    : MusicService(l, c, createPlaybackEngine(), parent) {}
MusicService::MusicService(Library *l, ConnectClient *c, std::unique_ptr<IPlaybackEngine> playback,
                           QObject *parent, QNetworkAccessManager *playbackNetwork)
    : QObject(parent), library(l), remote(c), auth(l->directory(), this),
      searchEngine(
          l->directory(),
          [this](QString endpoint, QJsonObject body, SearchEngine::Done done, bool session) {
              return post(endpoint, body, done, session);
          },
          this),
      catalog(
          l,
          [this](QString endpoint, QJsonObject body, SearchEngine::Done done, bool session) {
              return post(endpoint, body, done, session);
          },
          this),
      resolver(new AudioResolver(this, {}, {}, playbackNetwork)),
      prefetch(new AudioResolver(this, {}, {}, playbackNetwork)),
      fadeResolver(new AudioResolver(this, {}, {}, playbackNetwork)), stream(std::make_unique<AudioStream>()),
      engine(std::move(playback)) {
    Q_ASSERT(engine);
    remote->setProviderCookies([this] { return authenticated ? auth.playbackCookies() : QByteArray(); });
    peerCache = new ConnectMediaCache(l, c, this);
    downloadService = new DownloadService(l, this);
    cacheService = new CacheStorage(l->directory(), this);
    cacheService->setBusyCheck([this] {
        return !engine->source().isEmpty() || bool(preparedStream) || bool(retiringStream) ||
               remote->hasTarget() || remote->mediaBusy() || peerCache->busy() || downloadService->busy();
    });
    connect(cacheService, &CacheStorage::message, this, &MusicService::message);
    downloadService->setCookies([this] { return authenticated ? auth.playbackCookies() : QByteArray(); });
    connect(downloadService, &DownloadService::message, this, &MusicService::message);
    connect(&catalog, &LibraryCatalog::message, this, &MusicService::message);
    connect(this, &MusicService::playbackChanged, &catalog, [this](QJsonObject state) {
        const bool localOutput = !remote->hasTarget() || (connectRenderer && remote->rendersLocally() &&
                                                          preparedTransfer.isEmpty() && !releasingConnect);
        catalog.observePlayback(state, localOutput);
    });
    connect(&catalog, &LibraryCatalog::changed, this, [this] {
        if (catalog.preferences()["crossfadeSeconds"].toInt() <= 0)
            discardCrossfade();
        else if (wantsPlayback)
            prepareNext.start();
    });
    connect(this, &MusicService::authenticationChanged, &catalog, [this] { catalog.cancel(); });
    const auto backendDiagnostic = engine->property("backendDiagnostic").toString();
    if (!backendDiagnostic.isEmpty())
        QTimer::singleShot(0, this, [this, backendDiagnostic] { emit message(backendDiagnostic); });
    connect(engine.get(), &IPlaybackEngine::mediaReady, this, [this](PlaybackRequest id) {
        if (active(id)) {
            trace.mark(id, "backend_playable");
            connectLoaded = true;
            connectMediaReady();
        }
    });
    connect(engine.get(), &IPlaybackEngine::seekableChanged, this, [this](PlaybackRequest id, bool seekable) {
        if (active(id) && seekable)
            connectMediaReady();
    });
    connect(&searchEngine, &SearchEngine::suggestionsReady, this, &MusicService::suggestionsReady);
    connect(&searchEngine, &SearchEngine::failed, this, &MusicService::message);
    connect(this, &MusicService::authenticationChanged, &searchEngine, &SearchEngine::invalidate);
    prepareNext.setSingleShot(true);
    prepareNext.setInterval(250);
    connect(&prepareNext, &QTimer::timeout, this, [this] {
        if (catalog.preferences()["crossfadeSeconds"].toInt() > 0 && engine->supportsCrossfade()) {
            prepareCrossfade();
            return;
        }
        const int next = current["index"].toInt() + 1;
        if (remote->hasTarget() || shuffled || !wantsPlayback || next >= queue.size())
            return;
        const auto cookies = authenticated ? auth.playbackCookies() : QByteArray();
        if (library->mediaPath(queue[next].id).isEmpty() && !prefetch->matches(queue[next].id, cookies))
            prefetch->resolve(queue[next].id, cookies);
    });
    connect(engine.get(), &IPlaybackEngine::positionChanged, this, [this](PlaybackRequest id, qint64 v) {
        if (!active(id))
            return;
        if (v > 0 && wantsPlayback)
            trace.mark(id, "first_position_proxy");
        current["position"] = double(v);
        emit playbackChanged(current);
        tryCrossfade();
    });
    connect(engine.get(), &IPlaybackEngine::durationChanged, this, [this](PlaybackRequest id, qint64 v) {
        if (!active(id))
            return;
        if (v > 0)
            current["duration"] = double(v);
        emit playbackChanged(current);
    });
    connect(engine.get(), &IPlaybackEngine::stateChanged, this,
            [this](PlaybackRequest id, PlaybackState state) {
                if (!active(id))
                    return;
                current["playing"] = state == PlaybackState::Playing;
                current["playbackState"] = state == PlaybackState::Loading ? 2
                                           : state == PlaybackState::Ended ? 4
                                                                           : 3;
                if (state == PlaybackState::Playing)
                    trace.mark(id, "backend_active");
                if (state == PlaybackState::Playing)
                    prepareNext.start();
                emit playbackChanged(current);
            });
    connect(engine.get(), &IPlaybackEngine::errorOccurred, this, [this](PlaybackRequest id, QString error) {
        if (!active(id))
            return;
        if (connectRenderer) {
            connectFailed(error);
            return;
        }
        trace.finish("backend_error");
        cancelPlayback();
        current["playing"] = false;
        emit playbackChanged(current);
        emit message(error);
    });
    connect(engine.get(), &IPlaybackEngine::endOfMedia, this, [this](PlaybackRequest id) {
        if (!active(id) || endedGeneration == id)
            return;
        if (connectRenderer) {
            endedGeneration = id;
            QTimer::singleShot(0, this, [this, id] {
                if (!active(id) || !preparedTransfer.isEmpty())
                    return;
                auto timer = current["sleepTimer"].toObject();
                if (timer["endOfSong"].toBool() && !timer["id"].toString().isEmpty()) {
                    current["sleepTimer"] = QJsonObject{};
                    executeConnectCommand({{"name", "pause"}});
                } else if (repeatMode == 1 && wantsPlayback) {
                    auto s = current;
                    s["position"] = 0;
                    s["playing"] = true;
                    startConnectTrack(s);
                } else
                    executeConnectCommand({{"name", "next"}});
            });
            return;
        }
        endedGeneration = id;
        trace.finish("ended");
        current["playing"] = false;
        emit playbackChanged(current);
        if (!active(id))
            return;
        // Leave the backend's terminal notification before replacing its source.
        QTimer::singleShot(0, this, [this, id] {
            if (!active(id) || !wantsPlayback)
                return;
            if (repeatMode == 1) {
                const auto source = engine->source();
                const auto next = ++playbackGeneration;
                disconnect(resolver, nullptr, this, nullptr);
                disconnect(stream.get(), nullptr, this, nullptr);
                bindSourceCallbacks(next);
                trace.begin(next, engine->backendName(), source.isLocalFile() ? "local" : "relay");
                trace.mark(next, "backend_load");
                engine->load(source, next);
                if (active(next))
                    engine->play();
            } else {
                action("next");
                if (active(id))
                    cancelPlayback(); // Queue ended: release the last file and its cache lease.
            }
        });
    });
    connect(&auth, &EmbeddedSession::busyChanged, this, &MusicService::loginPendingChanged);
    connect(&auth, &EmbeddedSession::status, this, &MusicService::message);
    connect(&auth, &EmbeddedSession::error, this, &MusicService::message);
    connect(&auth, &EmbeddedSession::authenticatedChanged, this, [this](bool value) {
        prefetch->cancel();
        prepareNext.stop();
        NativeAudioResolver::setVisitorData({});
        bool newlyConnected = value && (!authenticated || account != auth.accountName());
        authenticated = value;
        account = value ? auth.accountName() : QString();
        emit authenticationChanged(value);
        if (newlyConnected) {
            ++libraryGeneration;
            loadLibrary();
        }
    });
    QTimer::singleShot(0, &auth, &EmbeddedSession::restore);
    connect(remote, &ConnectClient::error, this, [this] {
        if (connectRenderer && remote->hasTarget() && !remote->connected()) {
            cancelPlayback();
            connectRenderer = false;
            releasingConnect = false;
            current["playing"] = false;
            emit playbackChanged(current);
        }
        updateRoute();
    });
    connect(remote, &ConnectClient::statusChanged, this, [this] { updateRoute(); });
    connect(remote, &ConnectClient::stateChanged, this, [this](QJsonObject state) {
        updateRoute();
        if (remote->hasTarget() && !state.isEmpty()) {
            applyConnectState(state);
        }
    });
    setupConnect();
    connect(engine.get(), &IPlaybackEngine::nextPrepared, this, [this](quint64 token, bool ok) {
        if (token != fadeToken || fadeGeneration != playbackGeneration)
            return;
        fadeReady = ok;
        if (ok)
            tryCrossfade();
        else
            QTimer::singleShot(0, this, [this, token] {
                if (token == fadeToken)
                    discardCrossfade();
            });
    });
    connect(engine.get(), &IPlaybackEngine::crossfadeFinished, this, [this] {
        retiringStream.reset();
        if (wantsPlayback)
            prepareNext.start();
    });
    updateRoute();
    restorePlayback();
    playbackSaveTimer.setInterval(2000);
    connect(&playbackSaveTimer, &QTimer::timeout, this, &MusicService::savePlayback);
    playbackSaveTimer.start();
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &MusicService::savePlayback);
}
void MusicService::discardCrossfade() {
    ++fadeToken;
    fadeReady = false;
    fadeIndex = -1;
    fadeResolver->disconnect(this);
    fadeResolver->cancel();
    engine->discardNext();
    preparedStream.reset();
}
void MusicService::prepareCrossfade() {
    if (retiringStream || remote->hasTarget() || !wantsPlayback || repeatMode == 1 ||
        !engine->supportsCrossfade() || catalog.preferences()["crossfadeSeconds"].toInt() <= 0 ||
        current["sleepTimer"].toObject()["endOfSong"].toBool() || fadeIndex >= 0)
        return;
    int next = current["index"].toInt(-1) + 1;
    if (shuffled && queue.size() > 1) {
        do {
            next = QRandomGenerator::global()->bounded(queue.size());
        } while (next == current["index"].toInt());
    } else if (next >= queue.size() && repeatMode == 2)
        next = 0;
    if (next < 0 || next >= queue.size() || next == current["index"].toInt())
        return;
    discardCrossfade();
    fadeIndex = next;
    fadeGeneration = playbackGeneration;
    const auto token = fadeToken;
    auto local = library->mediaPath(queue[next].id);
    if (local.isEmpty())
        local = peerCache->path(queue[next].id);
    if (!local.isEmpty()) {
        engine->prepareNext(QUrl::fromLocalFile(local), token);
        return;
    }
    preparedStream = std::make_unique<AudioStream>();
    connect(fadeResolver, &AudioResolver::ready, this, [this, token](QUrl url, QJsonObject headers, qint64) {
        if (token != fadeToken || playbackGeneration != fadeGeneration || !preparedStream)
            return;
        auto source = preparedStream->start(url, headers, token);
        if (!source.isEmpty())
            engine->prepareNext(source, token);
    });
    connect(fadeResolver, &AudioResolver::error, this, [this, token](QString) {
        if (token == fadeToken)
            discardCrossfade();
    });
    connect(preparedStream.get(), &AudioStream::requestError, this, [this, token](quint64, QString) {
        // Defer destruction until after the relay's callback has returned.
        QTimer::singleShot(0, this, [this, token] {
            if (token == fadeToken)
                discardCrossfade();
        });
    });
    fadeResolver->resolve(queue[next].id, authenticated ? auth.playbackCookies() : QByteArray());
}
void MusicService::tryCrossfade() {
    const int milliseconds = catalog.preferences()["crossfadeSeconds"].toInt() * 1000;
    if (!fadeReady || fadeGeneration != playbackGeneration || fadeIndex < 0 || fadeIndex >= queue.size() ||
        !wantsPlayback || remote->hasTarget() || milliseconds <= 0 || repeatMode == 1 ||
        current["sleepTimer"].toObject()["endOfSong"].toBool())
        return;
    const auto remaining = current["duration"].toDouble() - current["position"].toDouble();
    const auto speed = current["speed"].toDouble(1);
    if (remaining <= 0 || remaining > milliseconds * speed)
        return;
    const int next = fadeIndex;
    disconnect(resolver, nullptr, this, nullptr);
    disconnect(stream.get(), nullptr, this, nullptr);
    // Keep the old relay alive until its fading audio helper has stopped.
    retiringStream = std::move(stream);
    stream = preparedStream ? std::move(preparedStream) : std::make_unique<AudioStream>();
    stream->disconnect(this);
    fadeResolver->disconnect(this);
    fadeResolver->cancel();
    fadeReady = false;
    fadeIndex = -1;
    ++fadeToken;
    const auto generation = ++playbackGeneration;
    ++streamGeneration;
    current["index"] = next;
    current["song"] = queue[next].wire();
    current["position"] = 0;
    current["duration"] = queue[next].duration * 1000.0;
    current["playing"] = false;
    pendingSeek = -1;
    retriedStream = false;
    trace.finish("crossfade");
    trace.begin(generation, engine->backendName(), "crossfade");
    bindSourceCallbacks(generation);
    emit playbackChanged(current);
    if (!engine->activateNext(generation, qMax(100, int(remaining / speed)))) {
        engine->cancel();
        retiringStream.reset();
        resolveAndPlay(queue[next], generation);
    }
}
void MusicService::clearCache(QString kind) {
    if (kind != "audio" && kind != "images" && kind != "temporary")
        return;
    if (remote->hasTarget() || remote->mediaBusy() || peerCache->busy() || downloadService->busy()) {
        emit message("Wait for downloads and device transfers to finish before clearing cache.");
        return;
    }
    cancelPlayback();
    current["playing"] = false;
    emit playbackChanged(current);
    cacheService->clear(kind);
}
void MusicService::savePlayback() {
    if (current.isEmpty() || !preparedTransfer.isEmpty())
        return;
    // Store metadata only: never persist expiring audio URLs or Connect ownership.
    QJsonArray items;
    for (auto value : current["queue"].toArray())
        items.append(Track::fromWire(value.toObject()).wire());
    QJsonObject snapshot{{"schema", 1},
                         {"queue", items},
                         {"index", current["index"].toInt(-1)},
                         {"position", current["position"]},
                         {"duration", current["duration"]},
                         {"shuffle", shuffled},
                         {"repeat", repeatMode},
                         {"speed", current["speed"].toDouble(1)},
                         {"volume", double(engine->volume())}};
    const auto bytes = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
    if (bytes == savedPlayback && QFileInfo::exists(library->directory() + "/last-playback.json"))
        return;
    QSaveFile file(library->directory() + "/last-playback.json");
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
        savedPlayback = bytes;
}
void MusicService::restorePlayback() {
    QFile file(library->directory() + "/last-playback.json");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
        return;
    const auto saved = QJsonDocument::fromJson(file.readAll()).object();
    const auto items = saved["queue"].toArray();
    const auto index = saved["index"].toInt(-1);
    if (saved["schema"].toInt() != 1 || items.isEmpty() || items.size() > 20000 || index < 0 ||
        index >= items.size())
        return;
    QList<Track> restored;
    QJsonArray sanitized;
    for (auto item : items) {
        auto track = Track::fromWire(item.toObject());
        if (track.id.isEmpty() || track.id.size() > 256 || track.title.size() > 10000)
            return;
        restored.append(track);
        sanitized.append(track.wire());
    }
    const auto bounded = [](QJsonValue v, double fallback, double maximum) {
        const auto number = v.toDouble(fallback);
        return std::isfinite(number) ? qBound(0.0, number, maximum) : fallback;
    };
    const auto duration = bounded(saved["duration"], restored[index].duration * 1000.0, 9e12);
    const auto position = bounded(saved["position"], 0, duration > 0 ? duration : 9e12);
    queue = restored;
    shuffled = saved["shuffle"].toBool();
    repeatMode = qBound(0, saved["repeat"].toInt(), 2);
    const auto speed = qBound(.25, bounded(saved["speed"], 1, 4), 4.0);
    engine->setVolume(float(bounded(saved["volume"], .75, 1)));
    current = {
        {"queue", sanitized},
        {"index", index},
        {"song", queue[index].wire()},
        {"position", position},
        {"duration", duration},
        {"playing", false},
        {"shuffle", shuffled},
        {"repeat", repeatMode},
        {"speed", speed},
        {"deviceVolume", QJsonObject{{"level", qRound(engine->volume() * 100)}, {"min", 0}, {"max", 100}}}};
    localCheckpoint = current;
    // Do not resolve a stream or start audio until the user presses Play.
    remote->rememberLocalPlayback(current);
    emit playbackChanged(current);
}
MusicService::~MusicService() {
    // Catalog flushing can emit changed(); finish it before playback members are destroyed.
    catalog.checkpoint();
    disconnect(&catalog, nullptr, this, nullptr);
    savePlayback();
    remote->setProviderCookies({});
    cancelPlayback();
    delete cacheService;
    cacheService = nullptr;
}
bool MusicService::active(PlaybackRequest id) const {
    return id != 0 && id == playbackGeneration && (connectRenderer || (!remote->hasTarget() && !remoteRoute));
}
void MusicService::cancelPlayback(bool keepPrefetch) {
    trace.finish("cancelled");
    ++playbackGeneration;
    ++streamGeneration;
    wantsPlayback = false;
    pendingSeek = -1;
    disconnect(resolver, nullptr, this, nullptr);
    discardCrossfade();
    disconnect(stream.get(), nullptr, this, nullptr);
    engine->cancel();
    resolver->cancel();
    if (!keepPrefetch)
        prefetch->cancel();
    prepareNext.stop();
    stream->stop();
    retiringStream.reset();
    if (peerCache)
        peerCache->cancel();
    connectLoaded = false;
}
void MusicService::updateRoute() {
    if (remote->hasTarget() == remoteRoute)
        return;
    if (remote->hasTarget()) {
        savePlayback();
        localCheckpoint = current;
        remoteRoute = true;
        cancelPlayback();
        current["playing"] = false;
    } else {
        cancelPlayback();
        connectRenderer = false;
        releasingConnect = false;
        preparedTransfer.clear();
        prepareDeadline.stop();
        renderedSession.clear();
        renderedEpoch = -1;
        remoteRoute = false;
        current = localCheckpoint;
        current["playing"] = false;
    }
    emit playbackChanged(current);
}
QNetworkReply *MusicService::post(const QString &endpoint, QJsonObject body,
                                  std::function<void(QJsonObject)> done, bool useSession) {
    QNetworkRequest req(QUrl("https://music.youtube.com/youtubei/v1/" + endpoint + "?prettyPrint=false"));
    req.setTransferTimeout(20000);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Origin", "https://music.youtube.com");
    req.setRawHeader("X-Origin", "https://music.youtube.com");
    req.setRawHeader("X-Goog-Api-Format-Version", "1");
    req.setRawHeader("Referer", "https://music.youtube.com/");
    req.setRawHeader("X-YouTube-Client-Name", "67");
    req.setRawHeader("X-YouTube-Client-Version", "1.20260114.01.00");
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
                                   "like Gecko) Chrome/141.0.0.0 Safari/537.36");
    if (body.contains("_continuation")) {
        auto url = req.url();
        QUrlQuery parameters(url);
        const auto token = body.take("_continuation").toString();
        parameters.addQueryItem("continuation", token);
        parameters.addQueryItem("ctoken", token);
        url.setQuery(parameters);
        req.setUrl(url);
    }
    const auto searchPreferences = searchEngine.settings();
    body["context"] = QJsonObject{{"client", QJsonObject{{"clientName", "WEB_REMIX"},
                                                         {"clientVersion", "1.20260114.01.00"},
                                                         {"hl", searchPreferences["language"]},
                                                         {"gl", searchPreferences["country"]}}}};
    if (authenticated && useSession)
        auth.authorize(req);
    auto reply = network.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        if (reply->property("veluneSearchCancelled").toBool()) {
            reply->deleteLater();
            return;
        }
        auto raw = reply->readAll();
        auto doc = QJsonDocument::fromJson(raw);
        if (reply->error() != QNetworkReply::NoError || !doc.isObject()) {
            emit message("YouTube Music request failed: " + reply->errorString());
            const auto error = reply->error();
            const bool retryable = error == QNetworkReply::ConnectionRefusedError ||
                                   error == QNetworkReply::RemoteHostClosedError ||
                                   error == QNetworkReply::HostNotFoundError ||
                                   error == QNetworkReply::TimeoutError ||
                                   error == QNetworkReply::TemporaryNetworkFailureError ||
                                   error == QNetworkReply::OperationCanceledError;
            done({{"error", reply->errorString()}, {"retryable", retryable}});
        } else {
            const auto visitor = doc.object()["responseContext"].toObject()["visitorData"].toString();
            if (!visitor.isEmpty())
                NativeAudioResolver::setVisitorData(visitor);
            done(doc.object());
        }
        reply->deleteLater();
    });
    return reply;
}
void MusicService::login() {
    if (auth.busy())
        auth.cancel();
    else
        auth.start();
}
void MusicService::logout() {
    cancelSearch();
    ++libraryGeneration;
    cancelPlayback();
    auth.logout();
    remote->detach();
    current["playing"] = false;
    localCheckpoint = current;
    emit playbackChanged(current);
}
QList<Track> MusicService::parseTracks(const QJsonValue &value) {
    QList<Track> out;
    QSet<QString> seen;
    std::function<void(QJsonValue)> walk = [&](QJsonValue v) {
        if (v.isArray()) {
            for (auto x : v.toArray())
                walk(x);
            return;
        }
        if (!v.isObject())
            return;
        auto o = v.toObject();
        for (const QString renderer :
             {"musicResponsiveListItemRenderer", "musicTwoRowItemRenderer", "playlistPanelVideoRenderer"})
            if (o.contains(renderer)) {
                auto r = o[renderer].toObject();
                auto endpoint = findKey(r, "watchEndpoint").toObject();
                QString id = endpoint["videoId"].toString();
                if (id.isEmpty())
                    id = r["videoId"].toString();
                if (id.isEmpty())
                    id = r["playlistItemData"].toObject()["videoId"].toString();
                if (id.isEmpty() || seen.contains(id))
                    return;
                QString title = textOf(r["title"].toObject()), artist;
                auto cols = r["flexColumns"].toArray();
                if (!cols.isEmpty())
                    title = textOf(cols[0]
                                       .toObject()["musicResponsiveListItemFlexColumnRenderer"]
                                       .toObject()["text"]
                                       .toObject());
                if (cols.size() > 1)
                    artist = textOf(cols[1]
                                        .toObject()["musicResponsiveListItemFlexColumnRenderer"]
                                        .toObject()["text"]
                                        .toObject());
                if (artist.isEmpty())
                    artist = textOf(r["subtitle"].toObject());
                if (artist.isEmpty())
                    artist = textOf(r["longBylineText"].toObject());
                auto images = findKey(r, "thumbnails").toArray();
                QString cover = images.isEmpty() ? QString() : images.last().toObject()["url"].toString();
                Track track{id, title, artist, cover, 0};
                for (auto badge : r["badges"].toArray())
                    if (findKey(badge, "iconType").toString() == "MUSIC_EXPLICIT_BADGE")
                        track.explicitContent = true;
                auto videoType = findKey(r["navigationEndpoint"], "musicVideoType").toString();
                if (videoType.isEmpty() && !cols.isEmpty())
                    videoType = findKey(cols.first(), "musicVideoType").toString();
                if (videoType.isEmpty())
                    videoType = findKey(r["overlay"], "musicVideoType").toString();
                track.musicVideo = videoType == "MUSIC_VIDEO_TYPE_OMV" || videoType == "MUSIC_VIDEO_TYPE_UGC";
                std::function<void(QJsonValue)> album = [&](QJsonValue value) {
                    if (value.isArray()) {
                        for (auto item : value.toArray())
                            album(item);
                    } else if (value.isObject()) {
                        auto node = value.toObject();
                        auto browseId = node["navigationEndpoint"]
                                            .toObject()["browseEndpoint"]
                                            .toObject()["browseId"]
                                            .toString();
                        if (browseId.startsWith("MPRE")) {
                            track.albumId = browseId;
                            track.album = node["text"].toString();
                        }
                        for (auto child : node)
                            album(child);
                    }
                };
                album(r);
                const auto typed = SearchParser::item(r, false);
                if (!typed.isEmpty() && (typed["kind"] == "song" || typed["kind"] == "video"))
                    track.artistRefs = typed["artists"].toArray();
                out.append(track);
                seen.insert(id);
                return;
            }
        for (auto x : o)
            walk(x);
    };
    walk(value);
    return out;
}
void MusicService::loadLibrary(QString continuation, QList<Track> accumulated) {
    const int generation = libraryGeneration;
    QJsonObject body;
    if (continuation.isEmpty())
        body["browseId"] = "VLLM";
    else
        body["continuation"] = continuation;
    post("browse", body, [this, accumulated, generation](QJsonObject object) mutable {
        if (!authenticated || generation != libraryGeneration)
            return;
        if (object.isEmpty() || object.contains("error")) {
            emit message("Your account is connected, but YouTube Music could not load your liked songs.");
            return;
        }
        const auto tracks = parseTracks(object);
        QSet<QString> ids;
        for (const auto &track : accumulated)
            ids.insert(track.id);
        int added = 0;
        for (const auto &track : tracks)
            if (!ids.contains(track.id)) {
                accumulated.append(track);
                ids.insert(track.id);
                ++added;
            }
        auto token = findKey(object, "nextContinuationData").toObject()["continuation"].toString();
        if (token.isEmpty())
            token = findKey(object, "continuationCommand").toObject()["token"].toString();
        if (!token.isEmpty() && added > 0 && accumulated.size() < 20000) {
            loadLibrary(token, accumulated);
            return;
        }
        if (!token.isEmpty()) {
            emit message("YouTube Music pagination stopped before completion. Please retry later.");
            return;
        }
        library->rememberTracks(accumulated, true);
        emit message(QString("Imported %1 liked YouTube Music songs.").arg(accumulated.size()));
    });
}

void MusicService::suggest(QString query) {
    cancelSearch();
    query = query.trimmed();
    searchEngine.suggest(query);
    if (!query.isEmpty())
        emit suggestionsStarted(query);
}
void MusicService::playBrowse(QString browseId, QString playlistId) {
    if (browseId.isEmpty() && playlistId.isEmpty())
        return;
    const auto generation = playbackGeneration;
    const auto request = ++radioGeneration;
    const auto endpoint = playlistId.isEmpty() ? "browse" : "next";
    const QJsonObject body = playlistId.isEmpty()
                                 ? QJsonObject{{"browseId", browseId}}
                                 : QJsonObject{{"playlistId", playlistId}, {"isAudioOnly", true}};
    post(endpoint, body, [this, generation, request](QJsonObject response) {
        if (generation != playbackGeneration || request != radioGeneration)
            return;
        auto tracks = parseTracks(response);
        if (tracks.isEmpty())
            emit message("No playable songs were returned for this collection.");
        else
            play(tracks);
    });
}
void MusicService::cancelSearch() {
    searchEngine.cancel();
    ++requestGeneration;
    auto previous = searchReply;
    searchReply.clear();
    if (previous && !previous->isFinished()) {
        previous->setProperty("veluneSearchCancelled", true);
        previous->abort();
    }
}
void MusicService::refreshCatalog(QStringList kinds) {
    if (!authenticated) {
        emit message("Sign in with Google to refresh your online library.");
        return;
    }
    catalog.refresh(kinds);
}
void MusicService::search(QString query) {
    cancelSearch();
    query = query.trimmed();
    if (query.isEmpty())
        return;
    searchEngine.search(query);
    emit searchStarted(query);
}
QJsonArray MusicService::localSearchItems(bool offlineOnly) const {
    QJsonArray result;
    QHash<QString, QJsonArray> albums, artists;
    QHash<QString, Track> representatives;
    for (const auto &track : library->tracks()) {
        if (offlineOnly && library->mediaPath(track.id).isEmpty())
            continue;
        auto item = track.wire();
        item["kind"] = "song";
        result.append(item);
        if (!track.album.isEmpty()) {
            const auto key = track.albumId.isEmpty() ? track.album : track.albumId;
            albums[key].append(track.wire());
            representatives[key] = track;
        }
        if (!track.artist.isEmpty()) {
            artists[track.artist].append(track.wire());
            representatives[track.artist] = track;
        }
    }
    auto albumKeys = albums.keys();
    albumKeys.sort();
    for (auto key : albumKeys) {
        const auto track = representatives[key];
        auto item = track.wire();
        item["id"] = "local-album:" + key;
        item["title"] = track.album;
        item["kind"] = "album";
        item["localTracks"] = albums[key];
        result.append(item);
    }
    auto artistKeys = artists.keys();
    artistKeys.sort();
    for (auto key : artistKeys) {
        const auto track = representatives[key];
        auto item = track.wire();
        item["id"] = "local-artist:" + key;
        item["title"] = key;
        item["kind"] = "artist";
        item["localTracks"] = artists[key];
        result.append(item);
    }
    for (auto value : library->snapshot()["playlists"].toArray()) {
        auto saved = value.toObject();
        if (saved["removed"].toBool())
            continue;
        QJsonArray tracks;
        for (auto value : saved["songs"].toArray()) {
            auto track = Track::fromWire(value.toObject());
            if (!offlineOnly || !library->mediaPath(track.id).isEmpty())
                tracks.append(track.wire());
        }
        if (tracks.isEmpty())
            continue;
        auto item = tracks.first().toObject();
        item["id"] = "local-playlist:" + saved["id"].toString();
        item["title"] = saved["name"].toString(saved["title"].toString("Playlist"));
        item["kind"] = "playlist";
        item["localTracks"] = tracks;
        result.append(item);
    }
    return result;
}
void MusicService::explore() {
    cancelSearch();
    const int generation = requestGeneration;
    searchReply = post("browse", {{"browseId", "FEmusic_explore"}}, [this, generation](QJsonObject o) {
        if (generation == requestGeneration)
            emit results(parseTracks(o));
    });
}
void MusicService::browseCollection(QString token, const Track &seed, bool radio) {
    const int generation = libraryGeneration;
    const auto done = [this, token, generation](QJsonObject object) {
        if (generation != libraryGeneration)
            return;
        if (object.contains("error"))
            emit collectionFailed(token, "Could not load this collection. Saved tracks are still available.");
        else
            emit collectionLoaded(token, parseTracks(object));
    };
    if (radio)
        post("next", {{"videoId", seed.id}, {"playlistId", "RDAMVM" + seed.id}, {"isAudioOnly", true}}, done);
    else if (!seed.albumId.isEmpty())
        post("browse", {{"browseId", seed.albumId}}, done);
    else
        emit collectionLoaded(token, {});
}
void MusicService::radio(const Track &seed) {
    const auto generation = playbackGeneration;
    const auto radioRequest = ++radioGeneration;
    emit message("Building song radio…");
    post("next", {{"videoId", seed.id}, {"playlistId", "RDAMVM" + seed.id}, {"isAudioOnly", true}},
         [this, generation, radioRequest](QJsonObject o) {
             if (generation != playbackGeneration || radioRequest != radioGeneration)
                 return;
             auto items = parseTracks(o);
             if (items.isEmpty()) {
                 emit message("Song radio is unavailable for this track.");
                 return;
             }
             play(items);
         });
}
void MusicService::play(QList<Track> items, int index) {
    QElapsedTimer received;
    received.start();
    if (remoteUnavailable())
        return;
    if (items.isEmpty() || index < 0 || index >= items.size()) {
        emit message("There are no songs available yet.");
        return;
    }
    library->rememberTracks(items);
    if (remote->connected()) {
        if (!connectRenderer)
            ++playbackGeneration;
        remote->play(items, index);
        return;
    }
    const auto cookies = authenticated ? auth.playbackCookies() : QByteArray();
    const bool adoptPrepared = prefetch->matches(items[index].id, cookies);
    cancelPlayback(adoptPrepared);
    if (adoptPrepared)
        std::swap(resolver, prefetch);
    queue = items;
    current = {
        {"queue", QJsonArray{}},
        {"index", index},
        {"song", items[index].wire()},
        {"position", 0},
        {"duration", items[index].duration * 1000.0},
        {"playing", false},
        {"shuffle", shuffled},
        {"repeat", repeatMode},
        {"speed", 1},
        {"deviceVolume", QJsonObject{{"level", qRound(engine->volume() * 100)}, {"min", 0}, {"max", 100}}}};
    QJsonArray a;
    for (const auto &t : items)
        a.append(t.wire());
    current["queue"] = a;
    const auto generation = playbackGeneration;
    trace.begin(generation, engine->backendName(),
                library->mediaPath(items[index].id).isEmpty() ? "relay" : "local", received);
    emit playbackChanged(current);
    if (active(generation))
        resolveAndPlay(items[index], generation);
}
void MusicService::bindSourceCallbacks(PlaybackRequest generation) {
    connect(resolver, &AudioResolver::stage, this, [this, generation](const QString &name) {
        if (active(generation))
            trace.mark(generation, name);
    });
    connect(resolver, &AudioResolver::cacheHit, this, [this, generation] {
        if (active(generation))
            trace.mark(generation, "resolver_cache_hit");
    });
    connect(resolver, &AudioResolver::ready, this,
            [this, generation](QUrl source, QJsonObject headers, qint64 duration) {
                if (!active(generation))
                    return;
                trace.mark(generation, retriedStream ? "refresh_resolver_completed" : "resolver_completed");
                const auto local = stream->start(source, headers, ++streamGeneration);
                if (local.isEmpty())
                    return;
                trace.mark(generation, retriedStream ? "refresh_relay_ready" : "relay_ready");
                if (duration > 0)
                    current["duration"] = double(duration);
                trace.mark(generation, retriedStream ? "refresh_backend_load" : "backend_load");
                engine->load(local, generation);
                if (!active(generation))
                    return;
                engine->setSpeed(float(current["speed"].toDouble(1)));
                if (pendingSeek >= 0) {
                    engine->seek(pendingSeek);
                    pendingSeek = -1;
                }
                if (wantsPlayback)
                    engine->play();
            });
    connect(resolver, &AudioResolver::error, this, [this, generation](QString error) {
        if (!active(generation))
            return;
        if (connectRenderer) {
            connectFailed(error);
            return;
        }
        trace.finish("resolver_error");
        cancelPlayback();
        current["playing"] = false;
        emit playbackChanged(current);
        emit message(error);
    });
    connect(stream.get(), &AudioStream::requestError, this, [this, generation](quint64 id, QString error) {
        if (id != streamGeneration || !active(generation))
            return;
        if (connectRenderer && !preparedTransfer.isEmpty()) {
            connectFailed(error);
            return;
        }
        if (!retriedStream && !queue.isEmpty()) {
            retriedStream = true;
            pendingSeek = engine->position();
            ++streamGeneration; // Ignore further errors already queued by the failed attempt.
            engine->cancel();
            stream->stop();
            emit message("Refreshing the audio source…");
            if (active(generation))
                trace.mark(generation, "refresh_resolver_started");
            if (active(generation))
                resolver->resolve(queue[current["index"].toInt()].id,
                                  authenticated ? auth.playbackCookies() : QByteArray(), true);
            return;
        }
        if (connectRenderer) {
            connectFailed(error);
            return;
        }
        cancelPlayback();
        current["playing"] = false;
        emit playbackChanged(current);
        emit message(error);
    });
}
void MusicService::resolveAndPlay(const Track &track, PlaybackRequest generation, qint64 position) {
    engine->setSpeed(float(current["speed"].toDouble(1)));
    wantsPlayback = true;
    retriedStream = false;
    pendingSeek = qMax(qint64(0), position);
    bindSourceCallbacks(generation);
    if (!engine->available()) {
        engine->load({}, generation);
        return;
    }
    auto local = library->mediaPath(track.id);
    if (local.isEmpty())
        local = peerCache->path(track.id);
    if (!local.isEmpty()) {
        trace.mark(generation, "backend_load");
        engine->load(QUrl::fromLocalFile(local), generation);
        if (active(generation) && pendingSeek >= 0) {
            engine->seek(pendingSeek);
            pendingSeek = -1;
        }
        if (active(generation))
            engine->play();
        return;
    }
    emit message("Preparing audio…");
    trace.mark(generation, "resolver_started");
    if (active(generation)) {
        const auto cookies = authenticated ? auth.playbackCookies() : QByteArray();
        if (resolver->matches(track.id, cookies))
            trace.mark(generation, "resolver_prefetch_joined");
        else
            resolver->resolve(track.id, cookies);
    }
}
void MusicService::command(QJsonObject c) {
    if (remoteUnavailable())
        return;
    if (remote->connected()) {
        remote->command(c);
        return;
    }
    auto name = c["name"].toString();
    if (name == "speed") {
        auto speed = qBound(.25, c["speed"].toDouble(1), 4.);
        engine->setSpeed(float(speed));
        current["speed"] = speed;
    } else if (name == "sleepTimer") {
        current["sleepTimer"] = c["timer"];
        sleepUpdatedAt = QDateTime::currentMSecsSinceEpoch();
    } else if (name == "shuffle") {
        shuffle(c["enabled"].toBool());
        current["shuffle"] = shuffled;
    } else if (name == "repeat") {
        repeat(c["mode"].toInt());
        current["repeat"] = repeatMode;
    } else if (name == "seek") {
        if (c.contains("index") && c["index"].toInt() != current["index"].toInt())
            play(queue, c["index"].toInt());
        seek(qint64(c["position"].toDouble()));
    } else if (name == "deviceVolume") {
        volume(c["value"].toInt());
        current["deviceVolume"] = QJsonObject{{"level", c["value"]}, {"min", 0}, {"max", 100}};
    } else if (name == "append" || name == "playNext" || name == "insert" || name == "remove" ||
               name == "move" || name == "clear") {
        discardCrossfade();
        auto items = current["queue"].toArray();
        int index = current["index"].toInt(-1);
        int original = index;
        if (name == "clear") {
            items = {};
            index = -1;
        } else if (name == "remove") {
            int from = qBound(0, c["from"].toInt(), int(items.size())),
                to = qBound(from, c["to"].toInt(from + 1), int(items.size()));
            for (int i = to - 1; i >= from; --i)
                items.removeAt(i);
            if (index >= to)
                index -= to - from;
            else if (index >= from)
                index = qMin(from, int(items.size()) - 1);
        } else if (name == "move" && !items.isEmpty()) {
            int from = qBound(0, c["from"].toInt(), int(items.size()) - 1),
                to = qBound(0, c["to"].toInt(), int(items.size()) - 1);
            auto item = items.takeAt(from);
            items.insert(to, item);
            if (index == from)
                index = to;
            else if (from < index && to >= index)
                --index;
            else if (from > index && to <= index)
                ++index;
        } else {
            int at = name == "append"     ? int(items.size())
                     : name == "playNext" ? qMax(0, index + 1)
                                          : qBound(0, c["index"].toInt(), int(items.size()));
            auto added = c["queue"].toArray();
            for (auto item : added)
                items.insert(at++, item);
            if (at - added.size() <= index)
                index += added.size();
        }
        if (items.size() > 20000)
            return;
        bool changed = original < 0 || index < 0 || current["queue"].toArray()[original] != items[index];
        queue.clear();
        for (auto item : items)
            queue.append(Track::fromWire(item.toObject()));
        if (items.isEmpty()) {
            cancelPlayback();
            current["queue"] = items;
            current["index"] = -1;
            current["song"] = QJsonObject{};
            current["playing"] = false;
        } else if (changed) {
            bool playing = wantsPlayback;
            play(queue, index);
            if (!playing)
                action("pause");
        } else {
            current["queue"] = items;
            current["index"] = index;
        }
    } else {
        action(name);
        return;
    }
    emit playbackChanged(current);
}
void MusicService::action(QString name) {
    if (remoteUnavailable())
        return;
    if (remote->connected()) {
        if (!connectRenderer && (name == "next" || name == "previous" || name == "stop"))
            ++playbackGeneration;
        remote->command({{"name", name}});
        return;
    }
    if (name == "stop") {
        cancelPlayback();
        current["playing"] = false;
        current["position"] = 0;
        emit playbackChanged(current);
    } else if (name == "pause") {
        wantsPlayback = false;
        engine->pause();
    } else if (name == "resume") {
        wantsPlayback = true;
        if (!engine->source().isEmpty() && endedGeneration != playbackGeneration)
            engine->resume();
        else if (resolver->busy())
            return;
        else if (!queue.isEmpty()) {
            const auto position = qMax(qint64(0), qint64(current["position"].toDouble()));
            cancelPlayback();
            trace.begin(playbackGeneration, engine->backendName(),
                        library->mediaPath(queue[current["index"].toInt()].id).isEmpty() ? "relay" : "local");
            resolveAndPlay(queue[current["index"].toInt()], playbackGeneration, position);
        } else
            emit message("Choose a song first.");
    } else if ((name == "next" || name == "previous") && !queue.isEmpty()) {
        int i = current["index"].toInt() + (name == "next" ? 1 : -1);
        if (shuffled)
            i = QRandomGenerator::global()->bounded(queue.size());
        if (i >= queue.size() && repeatMode == 2)
            i = 0;
        if (i >= 0 && i < queue.size())
            play(queue, i);
    }
}
void MusicService::seek(qint64 position) {
    discardCrossfade();
    if (wantsPlayback)
        prepareNext.start();
    if (remoteUnavailable())
        return;
    if (remote->connected())
        remote->command({{"name", "seek"}, {"position", double(position)}});
    else if (!engine->source().isEmpty())
        engine->seek(position);
    else {
        pendingSeek = qMax(qint64(0), position);
        current["position"] = double(pendingSeek);
        emit playbackChanged(current);
    }
}
void MusicService::volume(int value) {
    if (remoteUnavailable())
        return;
    if (remote->connected()) {
        auto d = current["deviceVolume"].toObject();
        int min = d["min"].toInt(), max = d["max"].toInt(100);
        remote->command({{"name", "deviceVolume"},
                         {"operation", "set"},
                         {"value", min + qRound(value * (max - min) / 100.0)}});
    } else {
        engine->setVolume(qBound(0, value, 100) / 100.f);
        current["deviceVolume"] = QJsonObject{{"level", value}, {"min", 0}, {"max", 100}};
        emit playbackChanged(current);
    }
}
void MusicService::shuffle(bool v) {
    discardCrossfade();
    if (remoteUnavailable())
        return;
    shuffled = v;
    if (wantsPlayback)
        prepareNext.start();
    if (remote->connected())
        remote->command({{"name", "shuffle"}, {"enabled", v}});
}
void MusicService::repeat(int v) {
    discardCrossfade();
    if (remoteUnavailable())
        return;
    repeatMode = v;
    if (wantsPlayback)
        prepareNext.start();
    if (remote->connected())
        remote->command({{"name", "repeat"}, {"mode", v}});
}
void MusicService::importAudio(const QStringList &paths) {
    for (const auto &path : paths) {
        if (!QFileInfo(path).isFile() || !QMimeDatabase().mimeTypeForFile(path).name().startsWith("audio/"))
            continue;
        auto id =
            QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
        library->registerMedia({"local:" + id, QFileInfo(path).completeBaseName(), "Local audio", {}, 0},
                               path);
    }
}
bool MusicService::remoteUnavailable() {
    if (remote->hasTarget() && !remote->connected()) {
        emit message(
            "The selected device is offline. Choose This computer in Available devices to play locally.");
        return true;
    }
    return false;
}
} // namespace velune
