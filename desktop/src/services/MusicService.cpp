#include "MusicService.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
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
    : QObject(parent), library(l), remote(c),
      auth(GoogleOAuthConfig::load(l->directory()),
           "google/" +
               QString::fromLatin1(
                   QCryptographicHash::hash(
                       (l->directory() + "|" + GoogleOAuthConfig::load(l->directory()).clientId).toUtf8(),
                       QCryptographicHash::Sha256)
                       .toHex()),
           this) {
    connect(&auth, &GoogleAuth::busyChanged, this, &MusicService::loginPendingChanged);
    connect(&auth, &GoogleAuth::status, this, &MusicService::message);
    connect(&auth, &GoogleAuth::error, this, &MusicService::message);
    connect(&auth, &GoogleAuth::authenticatedChanged, this, [this](bool value) {
        bool newlyConnected = value && (!authenticated || account != auth.accountEmail());
        authenticated = value;
        account = value ? auth.accountEmail() : QString();
        emit authenticationChanged(value);
        if (newlyConnected) {
            ++libraryGeneration;
            loadLibrary();
        }
    });
    QTimer::singleShot(0, &auth, &GoogleAuth::restore);
    connect(remote, &ConnectClient::stateChanged, this, [this](QJsonObject state) {
        if (state.isEmpty()) {
            if (remoteRoute) {
                current = localCheckpoint;
                current["playing"] = false;
                remoteRoute = false;
                emit playbackChanged(current);
            }
            return;
        }
        if (!remoteRoute)
            localCheckpoint = current;
        remoteRoute = true;
#ifdef VELUNE_MULTIMEDIA
        if (remote->connected())
            player.pause();
#endif
        if (!state.isEmpty()) {
            current = state;
            emit playbackChanged(current);
        }
    });
#ifdef VELUNE_MULTIMEDIA
    player.setAudioOutput(&output);
    output.setVolume(.75);
    connect(&player, &QMediaPlayer::positionChanged, this, [this](qint64 v) {
        if (remoteRoute)
            return;
        current["position"] = double(v);
        emit playbackChanged(current);
    });
    connect(&player, &QMediaPlayer::durationChanged, this, [this](qint64 v) {
        if (remoteRoute)
            return;
        current["duration"] = double(v);
        emit playbackChanged(current);
    });
    connect(&player, &QMediaPlayer::playbackStateChanged, this, [this] {
        if (remoteRoute)
            return;
        current["playing"] = player.playbackState() == QMediaPlayer::PlayingState;
        emit playbackChanged(current);
    });
    connect(&player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &e) { emit message(e); });
    connect(&player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (remoteRoute)
            return;
        if (s == QMediaPlayer::EndOfMedia) {
            if (repeatMode == 1) {
                player.setPosition(0);
                player.play();
            } else
                action("next");
        }
    });
#endif
}
void MusicService::post(const QString &endpoint, QJsonObject body, std::function<void(QJsonObject)> done) {
    QNetworkRequest req(QUrl("https://music.youtube.com/youtubei/v1/" + endpoint + "?prettyPrint=false"));
    req.setTransferTimeout(20000);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Origin", "https://music.youtube.com");
    req.setRawHeader("Referer", "https://music.youtube.com/");
    req.setRawHeader("X-YouTube-Client-Name", "67");
    req.setRawHeader("X-YouTube-Client-Version", "1.20260114.01.00");
    req.setRawHeader(
        "User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/141.0.0.0 Safari/537.36");
    body["context"] = QJsonObject{{"client", QJsonObject{{"clientName", "WEB_REMIX"},
                                                         {"clientVersion", "1.20260114.01.00"},
                                                         {"hl", "en"},
                                                         {"gl", "US"}}}};
    auto reply = network.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        auto raw = reply->readAll();
        auto doc = QJsonDocument::fromJson(raw);
        if (reply->error() != QNetworkReply::NoError || !doc.isObject()) {
            emit message("YouTube Music request failed: " + reply->errorString());
            done({});
        } else
            done(doc.object());
        reply->deleteLater();
    });
}
void MusicService::login(QWidget *parent) {
    Q_UNUSED(parent);
    if (auth.busy())
        auth.cancel();
    else
        auth.start();
}
void MusicService::logout() {
    ++requestGeneration;
    ++playbackGeneration;
    ++libraryGeneration;
    auth.logout();
    remote->detach();
#ifdef VELUNE_MULTIMEDIA
    player.stop();
#endif
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
                out.append({id, title, artist, cover, 0});
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
    QUrl url("https://www.googleapis.com/youtube/v3/videos");
    QUrlQuery query;
    query.addQueryItem("part", "snippet,contentDetails");
    query.addQueryItem("myRating", "like");
    query.addQueryItem("maxResults", "50");
    if (!continuation.isEmpty())
        query.addQueryItem("pageToken", continuation);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Authorization", "Bearer " + auth.accessToken().toUtf8());
    auto reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, accumulated, generation]() mutable {
        reply->deleteLater();
        if (!authenticated || generation != libraryGeneration)
            return;
        auto document = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError || !document.isObject()) {
            emit message("Your Google account is connected, but the YouTube library could not be imported. "
                         "Check that YouTube Data API v3 is enabled for Velune.");
            return;
        }
        const auto object = document.object();
        static const QRegularExpression durationPattern(
            R"(^P(?:(\d+)D)?T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+(?:\.\d+)?)S)?$)");
        for (auto value : object["items"].toArray()) {
            auto item = value.toObject(), snippet = item["snippet"].toObject();
            auto match = durationPattern.match(item["contentDetails"].toObject()["duration"].toString());
            int seconds = match.hasMatch()
                              ? match.captured(1).toInt() * 86400 + match.captured(2).toInt() * 3600 +
                                    match.captured(3).toInt() * 60 + int(match.captured(4).toDouble())
                              : 0;
            auto thumbnails = snippet["thumbnails"].toObject();
            auto image = thumbnails["high"].toObject()["url"].toString();
            if (image.isEmpty())
                image = thumbnails["default"].toObject()["url"].toString();
            const auto id = item["id"].toString();
            if (!id.isEmpty())
                accumulated.append(
                    {id, snippet["title"].toString(), snippet["channelTitle"].toString(), image, seconds});
        }
        const auto token = object["nextPageToken"].toString();
        if (!token.isEmpty() && accumulated.size() < 1000) {
            loadLibrary(token, accumulated);
            return;
        }
        library->importTracks(accumulated);
        emit message(
            QString(
                "Imported %1 liked YouTube items. Google limits this list to the most recent 1,000 likes.")
                .arg(accumulated.size()));
    });
}
void MusicService::search(QString query) {
    int generation = ++requestGeneration;
    post("search", {{"query", query}}, [this, generation](QJsonObject o) {
        if (generation == requestGeneration)
            emit results(parseTracks(o));
    });
}
void MusicService::explore() {
    int generation = ++requestGeneration;
    post("browse", {{"browseId", "FEmusic_explore"}}, [this, generation](QJsonObject o) {
        if (generation == requestGeneration)
            emit results(parseTracks(o));
    });
}
void MusicService::radio(const Track &seed) {
    emit message("Building song radio…");
    post("next", {{"videoId", seed.id}, {"playlistId", "RDAMVM" + seed.id}, {"isAudioOnly", true}},
         [this, seed](QJsonObject o) {
             auto items = parseTracks(o);
             if (items.isEmpty()) {
                 emit message("Song radio is unavailable for this track.");
                 return;
             }
             play(items);
         });
}
void MusicService::play(QList<Track> items, int index) {
    if (remoteUnavailable())
        return;
    if (items.isEmpty() || index < 0 || index >= items.size()) {
        emit message("There are no songs available yet.");
        return;
    }
    if (remote->connected()) {
        remote->play(items, index);
        return;
    }
    queue = items;
    current = {{"queue", QJsonArray{}},
               {"index", index},
               {"song", items[index].wire()},
               {"position", 0},
               {"duration", items[index].duration * 1000.0},
               {"playing", false}};
    QJsonArray a;
    for (const auto &t : items)
        a.append(t.wire());
    current["queue"] = a;
    emit playbackChanged(current);
    resolveAndPlay(items[index], ++playbackGeneration);
}
void MusicService::resolveAndPlay(const Track &track, int generation) {
#ifdef VELUNE_MULTIMEDIA
    auto local = library->mediaPath(track.id);
    if (!local.isEmpty()) {
        player.setSource(QUrl::fromLocalFile(local));
        player.play();
        return;
    }
    emit message("Resolving audio…");
    post("player", {{"videoId", track.id}}, [this, generation](QJsonObject o) {
        if (generation != playbackGeneration || remote->hasTarget())
            return;
        auto streaming = o["streamingData"].toObject();
        auto formats = streaming["adaptiveFormats"].toArray();
        for (auto v : streaming["formats"].toArray())
            formats.append(v);
        QString url;
        for (auto f : formats) {
            auto x = f.toObject();
            if (x["mimeType"].toString().startsWith("audio/") && !x["url"].toString().isEmpty()) {
                url = x["url"].toString();
                break;
            }
        }
        if (url.isEmpty()) {
            emit message("This song requires a YouTube playback resolver that is not available in this "
                         "desktop build.");
            return;
        }
        player.setSource(QUrl(url));
        player.play();
    });
#else
    Q_UNUSED(track);
    Q_UNUSED(generation);
    emit message(
        "Local playback needs Qt Multimedia. You can control playback on a connected Velune device.");
#endif
}
void MusicService::action(QString name) {
    if (remoteUnavailable())
        return;
    if (remote->connected()) {
        remote->command({{"name", name}});
        return;
    }
#ifdef VELUNE_MULTIMEDIA
    if (name == "pause")
        player.pause();
    else if (name == "resume") {
        if (!player.source().isEmpty())
            player.play();
        else
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
#else
    emit message("Choose a connected output or enable Qt Multimedia for local playback.");
#endif
}
void MusicService::seek(qint64 position) {
    if (remoteUnavailable())
        return;
    if (remote->connected())
        remote->command({{"name", "seek"}, {"position", double(position)}});
#ifdef VELUNE_MULTIMEDIA
    else
        player.setPosition(position);
#endif
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
    }
#ifdef VELUNE_MULTIMEDIA
    else
        output.setVolume(value / 100.f);
#endif
}
void MusicService::shuffle(bool v) {
    if (remoteUnavailable())
        return;
    shuffled = v;
    if (remote->connected())
        remote->command({{"name", "shuffle"}, {"enabled", v}});
}
void MusicService::repeat(int v) {
    if (remoteUnavailable())
        return;
    repeatMode = v;
    if (remote->connected())
        remote->command({{"name", "repeat"}, {"mode", v}});
}
void MusicService::importAudio(QWidget *parent) {
    auto paths = QFileDialog::getOpenFileNames(parent, "Import local audio", {},
                                               "Audio (*.mp3 *.m4a *.flac *.ogg *.wav *.opus)");
    for (const auto &path : paths) {
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
