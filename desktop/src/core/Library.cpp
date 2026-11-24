#include "Library.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QUuid>
namespace velune {
QJsonObject Track::wire() const {
    return {{"id", id},
            {"title", title},
            {"artists", QJsonArray{QJsonObject{{"name", artist}}}},
            {"duration", duration},
            {"thumbnail", thumbnail},
            {"album", QJsonObject{{"id", albumId}, {"name", album}}},
            {"explicit", explicitContent},
            {"isVideo", musicVideo},
            {"artistRefs", artistRefs}};
}
Track Track::fromWire(const QJsonObject &o) {
    QStringList names;
    for (const auto &a : o["artists"].toArray())
        names << (a.isString() ? a.toString() : a.toObject()["name"].toString());
    return {o["id"].toString(),
            o["title"].toString(),
            names.join(", "),
            o["thumbnail"].toString(),
            o["duration"].toInt(),
            o["album"].toObject()["name"].toString(),
            o["album"].toObject()["id"].toString(),
            o["explicit"].toBool(),
            o["isVideo"].toBool(),
            o["artistRefs"].isArray() ? o["artistRefs"].toArray() : o["artists"].toArray()};
}
bool newerVersion(const QJsonObject &a, const QJsonObject &b) {
    auto x = a["version"].toObject(), y = b["version"].toObject();
    return x["counter"].toDouble() > y["counter"].toDouble() ||
           (x["counter"] == y["counter"] && x["device"].toString() > y["device"].toString());
}
Library::Library(QString directory, QObject *parent) : QObject(parent), root(std::move(directory)) {
    QDir().mkpath(root);
    QFile f(root + "/library.json");
    if (f.open(QIODevice::ReadOnly))
        data = QJsonDocument::fromJson(f.readAll()).object();
    if (data["device"].toString().isEmpty())
        data = {{"schema", 1},
                {"device", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                {"counter", 0},
                {"likes", QJsonArray{}},
                {"playlists", QJsonArray{}},
                {"history", QJsonArray{}}};
    QFile m(root + "/media.json");
    if (m.open(QIODevice::ReadOnly))
        files = QJsonDocument::fromJson(m.readAll()).object();
}
bool Library::save() {
    QSaveFile f(root + "/library.json");
    auto bytes = QJsonDocument(data).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        emit error("Could not save your library.");
        return false;
    }
    return true;
}
QList<Track> Library::tracks(bool likedOnly) const {
    QMap<QString, Track> result;
    for (auto v : data["likes"].toArray()) {
        auto o = v.toObject();
        if (!likedOnly || o["liked"].toBool()) {
            auto t = Track::fromWire(o["track"].toObject());
            result[t.id] = t;
        }
    }
    if (!likedOnly)
        for (auto v : data["playlists"].toArray()) {
            auto p = v.toObject();
            if (!p["removed"].toBool())
                for (auto s : p["songs"].toArray()) {
                    auto t = Track::fromWire(s.toObject());
                    result[t.id] = t;
                }
        }
    return result.values();
}
bool Library::liked(const QString &id) const {
    for (auto v : data["likes"].toArray()) {
        auto o = v.toObject();
        if (o["track"].toObject()["id"] == id)
            return o["liked"].toBool();
    }
    return false;
}
void Library::setLiked(const Track &track, bool value) {
    if (track.id.isEmpty())
        return;
    const auto before = data;
    auto a = data["likes"].toArray();
    for (int i = a.size() - 1; i >= 0; --i)
        if (a[i].toObject()["track"].toObject()["id"] == track.id)
            a.removeAt(i);
    double counter = data["counter"].toDouble() + 1;
    data["counter"] = counter;
    auto song = track.wire();
    song["artists"] = QJsonArray{track.artist};
    a.append(QJsonObject{{"track", song},
                         {"liked", value},
                         {"version", QJsonObject{{"counter", counter}, {"device", deviceId()}}}});
    data["likes"] = a;
    if (!save()) {
        data = before;
        return;
    }
    emit changed();
}
void Library::importTracks(const QList<Track> &items, bool value) {
    const auto before = data;
    QMap<QString, QJsonObject> records;
    for (auto item : data["likes"].toArray()) {
        auto record = item.toObject();
        records[record["track"].toObject()["id"].toString()] = record;
    }
    double counter = data["counter"].toDouble();
    bool edited = false;
    for (const auto &track : items) {
        if (track.id.isEmpty() ||
            (records.contains(track.id) && records[track.id]["liked"].toBool() == value))
            continue;
        auto song = track.wire();
        song["artists"] = QJsonArray{track.artist};
        records[track.id] = {{"track", song},
                             {"liked", value},
                             {"version", QJsonObject{{"counter", ++counter}, {"device", deviceId()}}}};
        edited = true;
    }
    if (edited) {
        QJsonArray likes;
        for (const auto &record : records)
            likes.append(record);
        data["likes"] = likes;
        data["counter"] = counter;
        if (!save()) {
            data = before;
            return;
        }
        emit changed();
    }
}
void Library::rememberTracks(const QList<Track> &tracks, bool newTracksLiked) {
    const auto before = data;
    QMap<QString, QJsonObject> records;
    for (auto value : data["likes"].toArray())
        records[value.toObject()["track"].toObject()["id"].toString()] = value.toObject();
    double counter = data["counter"].toDouble();
    bool edited = false;
    for (auto track : tracks) {
        if (track.id.isEmpty())
            continue;
        auto record = records.value(track.id);
        const auto old = Track::fromWire(record.value("track").toObject());
        if (track.title.isEmpty())
            track.title = old.title;
        if (track.artist.isEmpty())
            track.artist = old.artist;
        if (track.thumbnail.isEmpty())
            track.thumbnail = old.thumbnail;
        if (track.album.isEmpty())
            track.album = old.album;
        if (track.albumId.isEmpty())
            track.albumId = old.albumId;
        if (track.artistRefs.isEmpty())
            track.artistRefs = old.artistRefs;
        if (!track.duration)
            track.duration = old.duration;
        track.explicitContent = track.explicitContent || old.explicitContent;
        track.musicVideo = track.musicVideo || old.musicVideo;
        auto wire = track.wire();
        wire["artists"] = QJsonArray{track.artist};
        if (!record.isEmpty() && record["track"].toObject() == wire)
            continue;
        records[track.id] = {{"track", wire},
                             {"liked", record.isEmpty() ? newTracksLiked : record["liked"].toBool()},
                             {"version", QJsonObject{{"counter", ++counter}, {"device", deviceId()}}}};
        edited = true;
    }
    if (!edited)
        return;
    QJsonArray result;
    for (auto record : records)
        result.append(record);
    data["likes"] = result;
    data["counter"] = counter;
    if (!save()) {
        data = before;
        return;
    }
    emit changed();
}
QString Library::putPlaylist(QString id, QString name, const QList<Track> &tracks, QString browseId,
                             QString thumbnail) {
    name = name.trimmed();
    if (name.isEmpty())
        return {};
    const auto before = data;
    auto playlists = data["playlists"].toArray();
    QJsonObject playlist;
    for (int i = playlists.size() - 1; i >= 0; --i)
        if (playlists[i].toObject()["id"].toString() == id) {
            playlist = playlists[i].toObject();
            playlists.removeAt(i);
        }
    if (id.isEmpty())
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    playlist["id"] = id;
    playlist["name"] = name;
    playlist["removed"] = false;
    if (!playlist.contains("createdAt"))
        playlist["createdAt"] = double(QDateTime::currentMSecsSinceEpoch());
    if (!browseId.isEmpty())
        playlist["browseId"] = browseId;
    if (!thumbnail.isEmpty())
        playlist["thumbnail"] = thumbnail;
    QJsonArray songs;
    for (const auto &track : tracks)
        if (!track.id.isEmpty()) {
            auto wire = track.wire();
            wire["artists"] = QJsonArray{track.artist};
            songs.append(wire);
        }
    playlist["songs"] = songs;
    const auto counter = data["counter"].toDouble() + 1;
    playlist["version"] = QJsonObject{{"counter", counter}, {"device", deviceId()}};
    playlists.append(playlist);
    data["playlists"] = playlists;
    data["counter"] = counter;
    if (!save()) {
        data = before;
        return {};
    }
    emit changed();
    return id;
}
bool Library::removePlaylist(QString id) {
    const auto before = data;
    auto playlists = data["playlists"].toArray();
    for (int i = 0; i < playlists.size(); ++i) {
        auto playlist = playlists[i].toObject();
        if (playlist["id"].toString() != id || playlist["removed"].toBool())
            continue;
        const auto counter = data["counter"].toDouble() + 1;
        data["counter"] = counter;
        playlist["removed"] = true;
        playlist["version"] = QJsonObject{{"counter", counter}, {"device", deviceId()}};
        playlists[i] = playlist;
        data["playlists"] = playlists;
        if (!save()) {
            data = before;
            return false;
        }
        emit changed();
        return true;
    }
    return false;
}
void Library::recordListening(const Track &track, qint64 milliseconds, QString timestamp) {
    if (track.id.isEmpty() || milliseconds <= 0)
        return;
    const auto before = data;
    auto history = data["history"].toArray();
    const auto counter = data["counter"].toDouble() + 1;
    data["counter"] = counter;
    auto wire = track.wire();
    wire["artists"] = QJsonArray{track.artist};
    history.append(QJsonObject{{"track", wire},
                               {"timestamp", timestamp},
                               {"playTime", double(milliseconds)},
                               {"removed", false},
                               {"version", QJsonObject{{"counter", counter}, {"device", deviceId()}}}});
    int live = 0;
    for (auto value : history)
        if (!value.toObject()["removed"].toBool())
            ++live;
    for (int i = 0; i < history.size() && live > 5000;) {
        if (!history[i].toObject()["removed"].toBool()) {
            history.removeAt(i);
            --live;
        } else
            ++i;
    }
    data["history"] = history;
    if (!save()) {
        data = before;
        return;
    }
    emit changed();
}
// All three collections retain tombstones. Equal-version history prefers deletion.
bool Library::merge(const QJsonObject &remote) {
    if (remote["schema"].toInt() != 1 || remote["device"].toString().isEmpty())
        return false;
    for (const QString name : {"likes", "playlists", "history"}) {
        if (!remote[name].isArray())
            return false;
        if (remote[name].toArray().size() > 50000)
            return false;
    }
    auto before = data;
    auto key = [](const QJsonObject &o, const QString &kind) {
        if (kind == "playlists")
            return o["id"].toString();
        QString id = o["track"].toObject()["id"].toString();
        return kind == "history" ? id + "|" + o["timestamp"].toString() + "|" +
                                       QString::number(o["playTime"].toDouble(), 'f', 0)
                                 : id;
    };
    for (const QString kind : {"likes", "playlists", "history"}) {
        QMap<QString, QJsonObject> map;
        for (auto v : data[kind].toArray())
            map[key(v.toObject(), kind)] = v.toObject();
        for (auto v : remote[kind].toArray()) {
            auto o = v.toObject();
            auto k = key(o, kind);
            if (k.isEmpty())
                continue;
            auto old = map.value(k);
            if (!map.contains(k) || newerVersion(o, old) ||
                (kind == "history" && o["version"] == old["version"] && o["removed"].toBool()))
                map[k] = o;
        }
        QJsonArray out;
        for (const auto &o : map)
            out.append(o);
        data[kind] = out;
    }
    data["counter"] = qMax(data["counter"].toDouble(), remote["counter"].toDouble());
    if (data != before) {
        if (!save()) {
            data = before;
            return false;
        }
        emit changed();
    }
    return true;
}
void Library::clearListeningHistory() {
    const auto before = data;
    auto history = data["history"].toArray();
    double counter = data["counter"].toDouble();
    for (int i = 0; i < history.size(); ++i) {
        auto event = history[i].toObject();
        if (event["removed"].toBool()) continue;
        event["removed"] = true;
        event["version"] = QJsonObject{{"counter", ++counter}, {"device", deviceId()}};
        history[i] = event;
    }
    data["history"] = history;
    data["counter"] = counter;
    if (!save()) { data = before; return; }
    emit changed();
}
QString Library::mediaPath(const QString &id) const {
    const auto path = files[id].toString();
    const QFileInfo file(path);
    return file.isFile() && file.isReadable() && file.size() > 0 ? path : QString();
}
void Library::registerMedia(const Track &t, const QString &path, bool like) {
    const auto before = files;
    files[t.id] = path;
    QSaveFile f(root + "/media.json");
    auto bytes = QJsonDocument(files).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        files = before;
        emit error("Could not save your local audio index.");
        return;
    }
    if (like)
        setLiked(t, true);
    else {
        rememberTracks({t});
        emit changed();
    }
}
void Library::updateDownloadFiles(const QMap<QString, QString> &previous,
                                  const QList<QPair<Track, QString>> &current) {
    auto next = files;
    for (auto i = previous.begin(); i != previous.end(); ++i)
        if (next[i.key()].toString() == i.value())
            next.remove(i.key());
    QList<Track> tracks;
    for (const auto &entry : current) {
        next[entry.first.id] = entry.second;
        tracks.append(entry.first);
    }
    const bool changedFiles = next != files;
    if (changedFiles) {
        QSaveFile f(root + "/media.json");
        auto bytes = QJsonDocument(next).toJson();
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
            emit error("Could not update your local audio index.");
            return;
        }
        files = next;
    }
    rememberTracks(tracks);
    if (changedFiles)
        emit changed();
}
} // namespace velune
