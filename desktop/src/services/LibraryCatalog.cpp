#include "LibraryCatalog.h"
#include <QCollator>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrlQuery>
#include <algorithm>
namespace velune {
namespace {
QJsonValue find(const QJsonValue &value, QString key) {
    if (value.isObject()) {
        const auto object = value.toObject();
        if (object.contains(key))
            return object[key];
        for (auto child : object) {
            auto result = find(child, key);
            if (!result.isNull() && !result.isUndefined())
                return result;
        }
    } else if (value.isArray())
        for (auto child : value.toArray()) {
            auto result = find(child, key);
            if (!result.isNull() && !result.isUndefined())
                return result;
        }
    return {};
}
QJsonArray tracksArray(const QList<Track> &tracks) {
    QJsonArray result;
    for (const auto &track : tracks)
        result.append(track.wire());
    return result;
}
QString displayArtists(QJsonObject entry) {
    return Track::fromWire(entry).artist;
}
QString timestamp() {
    return QDateTime::currentDateTime().toString("yyyy-MM-dd'T'HH:mm:ss.zzz");
}
} // namespace
LibraryCatalog::LibraryCatalog(Library *source, SearchEngine::Transport send, QObject *parent)
    : QObject(parent), library(source), transport(std::move(send)) {
    prefs = {{"section", "all"},
             {"scope", "liked"},
             {"sort", "added"},
             {"descending", true},
             {"view", "list"},
             {"gridSize", "big"},
             {"showLiked", true},
             {"showOffline", true},
             {"showTop", true},
             {"showCached", true},
             {"topSize", 50},
             {"selectedTags", QJsonArray{}},
             {"playlistOrder", QJsonArray{}},
             {"sectionOptions", QJsonObject{}}};
    prefs["pauseListeningHistory"] = false;
    prefs["defaultPage"] = "Home";
    prefs["systemFont"] = false;
    prefs["hidePlayerArtwork"] = false;
    prefs["lyricsFontSize"] = 18;
    prefs["lyricsLineHeight"] = 1.4;
    prefs["crossfadeSeconds"] = 0;
    QFile file(library->directory() + "/catalog.json");
    if (file.open(QIODevice::ReadOnly)) {
        auto data = QJsonDocument::fromJson(file.readAll()).object();
        const auto settings = data["preferences"].toObject();
        for (auto key : prefs.keys())
            if (settings.contains(key))
                prefs[key] = settings[key];
        remoteEntries = data["remoteEntries"].toArray();
        overrides = data["bookmarks"].toObject();
        playlistTags = data["playlistTags"].toObject();
    }
    connect(library, &Library::changed, this, &LibraryCatalog::changed);
}
LibraryCatalog::~LibraryCatalog() {
    flushListen();
}
bool LibraryCatalog::save() {
    QSaveFile file(library->directory() + "/catalog.json");
    auto bytes = QJsonDocument(QJsonObject{{"preferences", prefs},
                                           {"remoteEntries", remoteEntries},
                                           {"bookmarks", overrides},
                                           {"playlistTags", playlistTags}})
                     .toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        emit message("Could not save library preferences.");
        return false;
    }
    QFile::setPermissions(library->directory() + "/catalog.json", QFile::ReadOwner | QFile::WriteOwner);
    return true;
}
void LibraryCatalog::setPreferences(QJsonObject patch) {
    auto next = prefs;
    for (auto key : prefs.keys())
        if (patch.contains(key))
            next[key] = patch[key];
    next["topSize"] = qBound(1, next["topSize"].toInt(50), 500);
    for (auto key : {"pauseListeningHistory",
                     "systemFont", "hidePlayerArtwork", "showLiked", "showOffline", "showTop", "showCached"})
        if (!next[key].isBool())
            next[key] = prefs[key];
    const QStringList pages{"Home", "Search", "Explore", "Library", "Downloads"};
    if (!pages.contains(next["defaultPage"].toString()))
        next["defaultPage"] = prefs["defaultPage"];
    if (next["view"] != "list" && next["view"] != "grid")
        next["view"] = prefs["view"];
    next["lyricsFontSize"] = qBound(12, next["lyricsFontSize"].toInt(18), 40);
    next["lyricsLineHeight"] = qBound(1.0, next["lyricsLineHeight"].toDouble(1.4), 2.0);
    next["crossfadeSeconds"] = qBound(0,next["crossfadeSeconds"].toInt(),12);
    if (next == prefs)
        return;
    if (next["pauseListeningHistory"] != prefs["pauseListeningHistory"]) {
        flushListen();
        pendingTime = 0;
        wasPlaying = false;
        clock.restart();
    }
    const auto previous = prefs;
    prefs = next;
    if (!save()) {
        prefs = previous;
        return;
    }
    emit changed();
}
bool LibraryCatalog::bookmarked(QString id) const {
    if (overrides.contains(id))
        return overrides[id].toObject()["bookmarked"].toBool();
    for (auto value : library->snapshot()["playlists"].toArray()) {
        const auto playlist = value.toObject();
        if (playlist["id"].toString() == id && !playlist["removed"].toBool())
            return true;
    }
    for (auto value : remoteEntries)
        if (value.toObject()["id"].toString() == id)
            return value.toObject()["bookmarked"].toBool();
    return false;
}
void LibraryCatalog::bookmark(QJsonObject entry, bool value) {
    const auto id = entry["id"].toString();
    if (id.isEmpty())
        return;
    entry["bookmarked"] = value;
    entry["added"] = QDateTime::currentMSecsSinceEpoch();
    entry["updated"] = entry["added"];
    overrides[id] = entry;
    save();
    emit changed();
}
void LibraryCatalog::setTags(QString id, QStringList tags) {
    QJsonArray result;
    QSet<QString> seen;
    for (auto tag : tags) {
        tag = tag.trimmed();
        if (!tag.isEmpty() && !seen.contains(tag)) {
            seen.insert(tag);
            result.append(tag);
        }
    }
    playlistTags[id] = result;
    save();
    emit changed();
}
QStringList LibraryCatalog::tags() const {
    QSet<QString> result;
    for (auto value : playlistTags)
        for (auto tag : value.toArray())
            result.insert(tag.toString());
    auto list = result.values();
    list.sort(Qt::CaseInsensitive);
    return list;
}
void LibraryCatalog::setPlaylistOrder(QStringList ids) {
    QJsonArray order;
    for (auto id : ids)
        if (!order.contains(id))
            order.append(id);
    setPreferences({{"playlistOrder", order}});
}
QJsonArray LibraryCatalog::history() const {
    QJsonArray result;
    for (auto value : library->snapshot()["history"].toArray()) {
        auto event = value.toObject();
        if (event["removed"].toBool() || event["track"].toObject()["id"].toString().isEmpty())
            continue;
        auto entry = event["track"].toObject();
        entry["kind"] = "song";
        entry["timestamp"] = event["timestamp"];
        entry["playTime"] = event["playTime"];
        result.append(entry);
    }
    return result;
}
QJsonArray LibraryCatalog::songs() const {
    QHash<QString, QJsonObject> records;
    for (auto value : library->snapshot()["likes"].toArray())
        records[value.toObject()["track"].toObject()["id"].toString()] = value.toObject();
    QHash<QString, qint64> playTimes;
    for (auto value : history())
        playTimes[value.toObject()["id"].toString()] += qint64(value.toObject()["playTime"].toDouble());
    QJsonArray result;
    for (const auto &track : library->tracks()) {
        auto entry = track.wire();
        entry["kind"] = "song";
        entry["liked"] = library->liked(track.id);
        const auto path = library->mediaPath(track.id);
        auto stem = library->directory() + "/connect/media/" +
                    QString::fromLatin1(
                        QCryptographicHash::hash(track.id.toUtf8(), QCryptographicHash::Sha256).toHex());
        QFile meta(stem + ".json");
        QJsonObject info;
        if (meta.open(QIODevice::ReadOnly))
            info = QJsonDocument::fromJson(meta.read(8192)).object();
        entry["cached"] = info["id"] == track.id && info["length"].toDouble() > 0 &&
                          info["length"].toDouble() == QFileInfo(stem + ".audio").size() &&
                          QFileInfo(stem + ".audio").isReadable();
        entry["offline"] = !path.isEmpty() && QFileInfo(path).isFile() && QFileInfo(path).isReadable();
        const auto counter = records.value(track.id)["version"].toObject()["counter"].toDouble();
        entry["added"] = counter;
        entry["updated"] = counter;
        entry["playTime"] = double(playTimes.value(track.id));
        result.append(entry);
    }
    return result;
}
QJsonArray LibraryCatalog::entities() const {
    QHash<QString, QJsonObject> entities;
    QHash<QString, QString> artistIds;
    for (auto value : remoteEntries)
        if (value.toObject()["kind"] == "artist")
            artistIds[value.toObject()["title"].toString().toCaseFolded()] =
                value.toObject()["id"].toString();
    for (auto value : songs()) {
        auto track = value.toObject();
        const auto album = track["album"].toObject();
        const auto name = album["name"].toString();
        QList<QPair<QString, QString>> credits;
        if (!name.isEmpty())
            credits.append({"album", album["id"].toString().isEmpty()
                                         ? "local-album:" + name + ":" + displayArtists(track)
                                         : album["id"].toString()});
        auto refs = track["artistRefs"].toArray();
        if (refs.isEmpty())
            refs = track["artists"].toArray();
        QHash<QString, QString> artistNames;
        for (auto value : refs) {
            const auto ref = value.toObject();
            const auto artist = value.isString() ? value.toString() : ref["name"].toString();
            if (artist.isEmpty())
                continue;
            const auto id =
                ref["id"].toString(artistIds.value(artist.toCaseFolded(), "local-artist:" + artist));
            if (artistNames.contains(id))
                continue;
            artistNames[id] = artist;
            credits.append({"artist", id});
        }
        for (auto credit : credits) {
            const auto kind = credit.first, id = credit.second;
            auto entry = entities.value(id, track);
            entry["id"] = id;
            entry["title"] = kind == "album" ? name : artistNames[id];
            entry["kind"] = kind;
            entry["browseId"] = kind == "album"       ? album["id"]
                                : id.startsWith("UC") ? QJsonValue(id)
                                                      : QJsonValue{};
            auto list = entry["localTracks"].toArray();
            list.append(track);
            entry["localTracks"] = list;
            entry["songCount"] = list.size();
            entry["length"] = entry["length"].toDouble() + track["duration"].toDouble();
            entry["playTime"] =
                entry["playTime"].toDouble() + (list.size() == 1 ? 0 : track["playTime"].toDouble());
            entry["added"] = qMax(entry["added"].toDouble(), track["added"].toDouble());
            entities[id] = entry;
        }
    }
    for (auto value : remoteEntries) {
        auto entry = value.toObject();
        const auto id = entry["id"].toString();
        if (id.isEmpty())
            continue;
        if (entities.contains(id)) {
            const auto old = entities[id];
            if (entry["localTracks"].toArray().isEmpty())
                entry["localTracks"] = old["localTracks"];
            if (!entry.contains("songCount"))
                entry["songCount"] = old["songCount"];
            entry["playTime"] = old["playTime"];
        }
        entities[id] = entry;
    }
    for (auto value : library->snapshot()["playlists"].toArray()) {
        auto playlist = value.toObject();
        if (playlist["removed"].toBool())
            continue;
        const auto id = playlist["id"].toString();
        if (id.isEmpty())
            continue;
        auto entry = entities.value(id);
        entry["id"] = id;
        entry["title"] = playlist["name"];
        entry["kind"] = "playlist";
        entry["localTracks"] = playlist["songs"].toArray();
        entry["editable"] = playlist["browseId"].toString().isEmpty();
        entry["browseId"] = playlist["browseId"];
        entry["thumbnail"] = playlist["thumbnail"];
        entry["songCount"] = playlist["songs"].toArray().size();
        entry["bookmarked"] = true;
        entry["added"] = playlist["createdAt"].toDouble(playlist["version"].toObject()["counter"].toDouble());
        entry["updated"] = playlist["version"].toObject()["counter"].toDouble();
        entities[id] = entry;
    }
    for (auto key : overrides.keys()) {
        auto saved = overrides[key].toObject();
        auto entry = entities.value(key, saved);
        entry["bookmarked"] = saved["bookmarked"];
        entry["added"] = saved["added"];
        entities[key] = entry;
    }
    QJsonArray result;
    for (auto entry : entities) {
        entry["tags"] = playlistTags[entry["id"].toString()].toArray();
        int offline = 0;
        for (auto value : entry["localTracks"].toArray()) {
            const auto id = value.toObject()["id"].toString();
            const auto path = library->mediaPath(id);
            if (!path.isEmpty() && QFileInfo(path).isFile() && QFileInfo(path).isReadable())
                ++offline;
        }
        entry["offlineCount"] = offline;
        entry["fullyOffline"] = entry["totalSongs"].toInt() > 0 && offline >= entry["totalSongs"].toInt();
        result.append(entry);
    }
    return result;
}
QJsonArray LibraryCatalog::entries(QString section, QString scope, QString sort, bool descending,
                                   QJsonObject contentSettings) const {
    QJsonArray result;
    QList<QJsonObject> sortable;
    const auto allSongs = songs();
    if (section == "history") {
        for (auto value : history()) {
            auto entry = value.toObject();
            entry["sortDate"] = entry["timestamp"];
            sortable.append(entry);
        }
    } else if (section == "stats" || section == "top") {
        QHash<QString, QJsonObject> heard;
        for (auto value : history()) {
            auto event = value.toObject();
            const auto id = event["id"].toString();
            auto entry = heard.value(id, event);
            if (!heard.contains(id))
                entry["playTime"] = 0;
            entry["playTime"] = entry["playTime"].toDouble() + event["playTime"].toDouble();
            heard[id] = entry;
        }
        for (auto entry : heard)
            sortable.append(entry);
        sort = "playTime";
        descending = true;
    } else if (section == "songs" || section == "liked" || section == "offline" || section == "cached") {
        for (auto value : allSongs) {
            auto entry = value.toObject();
            const auto effective = section == "cached"    ? "cached"
                                   : section == "liked"   ? "liked"
                                   : section == "offline" ? "offline"
                                                          : scope;
            if (effective == "liked" && !entry["liked"].toBool())
                continue;
            if (effective == "offline" && !entry["offline"].toBool())
                continue;
            if (effective == "cached" && !entry["cached"].toBool())
                continue;
            sortable.append(entry);
        }
    } else {
        for (auto value : entities()) {
            auto entry = value.toObject();
            const auto kind = entry["kind"].toString();
            if (section == "albums" && kind != "album")
                continue;
            if (section == "artists" && kind != "artist")
                continue;
            if (section == "playlists" && kind != "playlist")
                continue;
            if ((section == "all" || scope == "liked") && kind != "playlist" && !entry["bookmarked"].toBool())
                continue;
            if (kind == "playlist" && entry.contains("bookmarked") && !entry["bookmarked"].toBool())
                continue;
            if (scope == "offline" && entry["offlineCount"].toInt() == 0)
                continue;
            if (scope == "fullOffline" && !entry["fullyOffline"].toBool())
                continue;
            const auto selected = prefs["selectedTags"].toArray();
            if (kind == "playlist" && !selected.isEmpty()) {
                bool matched = true;
                for (auto tag : selected)
                    if (!entry["tags"].toArray().contains(tag))
                        matched = false;
                if (!matched)
                    continue;
            }
            sortable.append(entry);
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    const auto order = prefs["playlistOrder"].toArray();
    const auto rank = [order](QJsonValue id) {
        for (int i = 0; i < order.size(); ++i)
            if (order[i] == id)
                return i;
        return 100000;
    };
    std::stable_sort(sortable.begin(), sortable.end(), [&](const QJsonObject &a, const QJsonObject &b) {
        int compare = 0;
        if (section == "history")
            compare = QString::compare(a["timestamp"].toString(), b["timestamp"].toString());
        else if (sort == "name" || sort == "artist")
            compare = collator.compare(sort == "name" ? a["title"].toString() : displayArtists(a),
                                       sort == "name" ? b["title"].toString() : displayArtists(b));
        else if (sort == "custom") {
            const bool ap = a["kind"] == "playlist", bp = b["kind"] == "playlist";
            if (ap != bp)
                return ap;
            const int ai = rank(a["id"]);
            const int bi = rank(b["id"]);
            compare = ai < bi   ? -1
                      : ai > bi ? 1
                                : collator.compare(a["title"].toString(), b["title"].toString());
        } else {
            const auto av = a[sort].toDouble(), bv = b[sort].toDouble();
            compare = av < bv ? -1 : av > bv ? 1 : 0;
        }
        if (compare == 0)
            compare = collator.compare(a["title"].toString(), b["title"].toString());
        return sort == "custom" ? compare < 0 : descending ? compare > 0 : compare < 0;
    });
    int limit = section == "top" ? prefs["topSize"].toInt(50) : 1000000;
    for (auto entry : sortable) {
        if (result.size() >= limit)
            break;
        if (!SearchParser::visible({entry}, contentSettings).isEmpty())
            result.append(entry);
    }
    if (section == "all" || section == "playlists") {
        QJsonArray pinned;
        for (auto pair : {qMakePair("showLiked", "liked"), qMakePair("showOffline", "offline"),
                          qMakePair("showTop", "top"), qMakePair("showCached", "cached")}) {
            if (!prefs[pair.first].toBool())
                continue;
            const QString id = pair.second;
            const auto tracks =
                entries(id, "library", id == "top" ? "playTime" : "added", true, contentSettings);
            const QString title = id == "liked"     ? "Liked Songs"
                                  : id == "offline" ? "Offline"
                                  : id == "top"     ? QString("My Top %1").arg(prefs["topSize"].toInt())
                                                    : "Cached";
            pinned.append(QJsonObject{{"id", "auto:" + id},
                                      {"title", title},
                                      {"kind", "auto"},
                                      {"section", id},
                                      {"localTracks", tracks},
                                      {"songCount", tracks.size()},
                                      {"note", ""}});
        }
        for (auto value : result)
            pinned.append(value);
        return pinned;
    }
    return result;
}
QJsonObject LibraryCatalog::statistics() const {
    qint64 total = 0;
    QSet<QString> ids;
    const auto events = history();
    for (auto value : events) {
        total += qMax<qint64>(0, qint64(value.toObject()["playTime"].toDouble()));
        ids.insert(value.toObject()["id"].toString());
    }
    return {{"listeningMs", double(total)}, {"songs", ids.size()}, {"events", events.size()}};
}
void LibraryCatalog::flushListen() {
    if (!listening.id.isEmpty() && pendingTime > 0)
        library->recordListening(listening, pendingTime, timestamp());
    pendingTime = 0;
}
void LibraryCatalog::observePlayback(QJsonObject state, bool localOutput) {
    if (prefs["pauseListeningHistory"].toBool()) {
        pendingTime = 0;
        wasPlaying = false;
        clock.restart();
        return;
    }
    const auto track = Track::fromWire(state["song"].toObject());
    if (clock.isValid() && wasPlaying)
        pendingTime += qBound<qint64>(0LL, clock.elapsed(), 10000LL);
    clock.restart();
    if (track.id != listening.id || !state["playing"].toBool() || !localOutput || pendingTime >= 30000)
        flushListen();
    listening = track;
    wasPlaying = localOutput && state["playing"].toBool() && !track.id.isEmpty();
}
void LibraryCatalog::clearListeningHistory() {
    pendingTime = 0;
    wasPlaying = false;
    clock.restart();
    library->clearListeningHistory();
}
QJsonObject LibraryCatalog::parsePage(QJsonObject response) {
    QJsonObject container;
    for (auto key : {"musicPlaylistShelfContinuation", "gridContinuation", "musicShelfContinuation",
                     "musicPlaylistShelfRenderer", "gridRenderer", "musicShelfRenderer"}) {
        container = find(response, key).toObject();
        if (!container.isEmpty())
            break;
    }
    QJsonArray result;
    const auto list =
        container.contains("items") ? container["items"].toArray() : container["contents"].toArray();
    for (auto value : list) {
        const auto object = value.toObject();
        for (auto key : {"musicResponsiveListItemRenderer", "musicTwoRowItemRenderer"})
            if (object.contains(key)) {
                auto entry = SearchParser::item(object[key].toObject(), true);
                if (!entry.isEmpty())
                    result.append(entry);
            }
    }
    auto token =
        find(container["continuations"], "nextContinuationData").toObject()["continuation"].toString();
    if (token.isEmpty())
        token = find(container["continuations"], "continuationCommand").toObject()["token"].toString();
    return {{"items", result}, {"continuation", token}, {"recognized", !container.isEmpty()}};
}
void LibraryCatalog::cancel() {
    ++generation;
    for (auto reply : requests)
        if (reply && !reply->isFinished()) {
            reply->setProperty("veluneSearchCancelled", true);
            reply->abort();
        }
    requests.clear();
    if (refreshing) {
        refreshing = false;
        emit busyChanged(false);
    }
}
void LibraryCatalog::refresh(QStringList kinds) {
    cancel();
    refreshing = true;
    emit busyChanged(true);
    syncNext(kinds, 0, generation);
}
void LibraryCatalog::syncNext(QStringList kinds, int index, quint64 ticket) {
    if (ticket != generation)
        return;
    if (index >= kinds.size()) {
        refreshing = false;
        emit busyChanged(false);
        emit message("Library refreshed.");
        return;
    }
    syncPage(kinds[index], {}, {}, {}, kinds, index, ticket);
}
void LibraryCatalog::syncPage(QString kind, QString token, QJsonArray accumulated, QSet<QString> seen,
                              QStringList kinds, int index, quint64 ticket) {
    static const QHash<QString, QString> endpoints{{"liked", "VLLM"},
                                                   {"songs", "FEmusic_liked_videos"},
                                                   {"albums", "FEmusic_liked_albums"},
                                                   {"artists", "FEmusic_library_corpus_artists"},
                                                   {"playlists", "FEmusic_liked_playlists"}};
    if (!endpoints.contains(kind)) {
        syncNext(kinds, index + 1, ticket);
        return;
    }
    QJsonObject body =
        token.isEmpty() ? QJsonObject{{"browseId", endpoints[kind]}} : QJsonObject{{"_continuation", token}};
    requests.append(transport(
        "browse", body,
        [this, kind, accumulated, seen, kinds, index, ticket](QJsonObject response) mutable {
            if (ticket != generation)
                return;
            auto page = parsePage(response);
            if (response.contains("error") || !page["recognized"].toBool()) {
                refreshing = false;
                emit busyChanged(false);
                emit message("Could not refresh " + kind + ". Saved library data was kept.");
                return;
            }
            for (auto item : page["items"].toArray())
                accumulated.append(item);
            const auto next = page["continuation"].toString();
            if (!next.isEmpty()) {
                if (seen.contains(next) || accumulated.size() > 50000) {
                    refreshing = false;
                    emit busyChanged(false);
                    emit message("Library pagination stopped without replacing saved data.");
                    return;
                }
                seen.insert(next);
                syncPage(kind, next, accumulated, seen, kinds, index, ticket);
                return;
            }
            if (kind == "liked" || kind == "songs") {
                QList<Track> tracks;
                for (auto value : accumulated)
                    if (value.toObject()["kind"] == "song" || value.toObject()["kind"] == "video")
                        tracks << Track::fromWire(value.toObject());
                library->rememberTracks(tracks, kind == "liked");
            } else {
                QJsonArray next;
                for (auto value : remoteEntries)
                    if (value.toObject()["sourceKind"].toString() != kind)
                        next.append(value);
                for (auto value : accumulated) {
                    auto entry = value.toObject();
                    const auto type = entry["kind"].toString();
                    if ((kind == "albums" && type != "album") || (kind == "artists" && type != "artist") ||
                        (kind == "playlists" && type != "playlist"))
                        continue;
                    if (entry["id"] == "LM" || entry["id"] == "SE")
                        continue;
                    for (auto oldValue : remoteEntries) {
                        const auto old = oldValue.toObject();
                        if (old["id"] != entry["id"])
                            continue;
                        for (auto key :
                             {"localTracks", "totalSongs", "songCount", "detailsUpdated", "length", "added"})
                            if (old.contains(key))
                                entry[key] = old[key];
                        break;
                    }
                    entry["sourceKind"] = kind;
                    entry["bookmarked"] = true;
                    if (!entry.contains("added"))
                        entry["added"] = QDateTime::currentMSecsSinceEpoch();
                    entry["updated"] = entry["added"];
                    next.append(entry);
                }
                remoteEntries = next;
                save();
                emit changed();
            }
            if (kind == "albums")
                hydrateAlbums(kinds, index, 0, ticket);
            else
                syncNext(kinds, index + 1, ticket);
        },
        true));
}
void LibraryCatalog::hydrateAlbums(QStringList kinds, int index, int cursor, quint64 ticket) {
    if (ticket != generation)
        return;
    while (cursor < remoteEntries.size()) {
        const auto entry = remoteEntries[cursor].toObject();
        if (entry["sourceKind"] == "albums" &&
            !(entry["totalSongs"].toInt() > 0 &&
              QDateTime::currentMSecsSinceEpoch() - qint64(entry["detailsUpdated"].toDouble()) <
                  10LL * 24 * 3600 * 1000))
            break;
        ++cursor;
    }
    if (cursor >= remoteEntries.size()) {
        syncNext(kinds, index + 1, ticket);
        return;
    }
    auto album = remoteEntries[cursor].toObject();
    const auto playlist = album["playlistId"].toString();
    if (!playlist.isEmpty()) {
        albumTracks(playlist, album, {}, {}, {}, kinds, index, cursor, ticket);
        return;
    }
    requests.append(transport(
        "browse", {{"browseId", album["browseId"]}},
        [this, album, kinds, index, cursor, ticket](QJsonObject response) mutable {
            if (ticket != generation)
                return;
            const auto url = QUrl(find(response["microformat"], "urlCanonical").toString());
            auto playlist = QUrlQuery(url).queryItemValue("list");
            if (playlist.isEmpty())
                playlist = find(response["contents"], "playlistId").toString();
            if (response.contains("error") || playlist.isEmpty()) {
                hydrateAlbums(kinds, index, cursor + 1, ticket);
                return;
            }
            album["playlistId"] = playlist;
            albumTracks(playlist, album, {}, {}, {}, kinds, index, cursor, ticket);
        },
        true));
}
void LibraryCatalog::albumTracks(QString playlistId, QJsonObject album, QString token, QJsonArray tracks,
                                 QSet<QString> seen, QStringList kinds, int index, int cursor,
                                 quint64 ticket) {
    if (ticket != generation)
        return;
    const auto body = token.isEmpty() ? QJsonObject{{"browseId", "VL" + playlistId}}
                                      : QJsonObject{{"_continuation", token}};
    requests.append(transport(
        "browse", body,
        [this, playlistId, album, tracks, seen, kinds, index, cursor, ticket](QJsonObject response) mutable {
            if (ticket != generation)
                return;
            const auto page = parsePage(response);
            if (response.contains("error") || !page["recognized"].toBool()) {
                hydrateAlbums(kinds, index, cursor + 1, ticket);
                return;
            }
            for (auto value : page["items"].toArray()) {
                auto entry = value.toObject();
                if (entry["kind"] != "song" && entry["kind"] != "video")
                    continue;
                entry["album"] = QJsonObject{{"id", album["id"]}, {"name", album["title"]}};
                if (entry["artists"].toArray().isEmpty())
                    entry["artists"] = album["artists"];
                tracks.append(entry);
            }
            const auto next = page["continuation"].toString();
            if (!next.isEmpty()) {
                if (seen.contains(next) || seen.size() >= 50) {
                    hydrateAlbums(kinds, index, cursor + 1, ticket);
                    return;
                }
                seen.insert(next);
                albumTracks(playlistId, album, next, tracks, seen, kinds, index, cursor, ticket);
                return;
            }
            qint64 length = 0;
            for (auto value : tracks)
                length += value.toObject()["duration"].toInt();
            album["localTracks"] = tracks;
            album["totalSongs"] = tracks.size();
            album["songCount"] = tracks.size();
            album["length"] = double(length);
            album["detailsUpdated"] = double(QDateTime::currentMSecsSinceEpoch());
            if (cursor < remoteEntries.size() && remoteEntries[cursor].toObject()["id"] == album["id"])
                remoteEntries[cursor] = album;
            save();
            emit changed();
            hydrateAlbums(kinds, index, cursor + 1, ticket);
        },
        true));
}
} // namespace velune
