#include "Library.h"
#include <QDir>
#include <QFile>
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
            {"thumbnail", thumbnail}};
}
Track Track::fromWire(const QJsonObject &o) {
    QStringList names;
    for (const auto &a : o["artists"].toArray())
        names << (a.isString() ? a.toString() : a.toObject()["name"].toString());
    return {o["id"].toString(), o["title"].toString(), names.join(", "), o["thumbnail"].toString(),
            o["duration"].toInt()};
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
QString Library::mediaPath(const QString &id) const {
    return files[id].toString();
}
void Library::registerMedia(const Track &t, const QString &path) {
    const auto before = files;
    files[t.id] = path;
    QSaveFile f(root + "/media.json");
    auto bytes = QJsonDocument(files).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        files = before;
        emit error("Could not save your local audio index.");
        return;
    }
    setLiked(t, true);
}
} // namespace velune
