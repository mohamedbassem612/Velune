#include "DesktopController.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QFontDatabase>
#include <QUrl>
#include <QUuid>
namespace velune {
DesktopController::DesktopController(Library *l, MusicService *m, ConnectClient *c, Discovery *d, QObject *p)
    : QObject(p), library(l), music(m), connectClient(c), discovery(d) {
    backup = new BackupService(l->directory(), this);
    auto update = [this] { refresh(); };
    connect(l, &Library::changed, this, update);
    connect(l, &Library::error, this, &DesktopController::notification);
    connect(m->libraryCatalog(), &LibraryCatalog::changed, this, update);
    connect(m->libraryCatalog(), &LibraryCatalog::busyChanged, this, update);
    connect(m, &MusicService::authenticationChanged, this, update);
    connect(m, &MusicService::loginPendingChanged, this, update);
    connect(m, &MusicService::playbackChanged, this, [this](QJsonObject state) {
        view["playback"] = state;
        auto song = state["song"].toObject();
        song["artwork"] = artwork(song.toVariantMap());
        song["artist"] = Track::fromWire(song).artist;
        song["liked"] = library->liked(song["id"].toString());
        view["song"] = song;
        emit changed();
    });
    connect(m, &MusicService::message, this, &DesktopController::notification);
    connect(m->downloads(), &DownloadService::changed, this, update);
    connect(m->cacheStorage(), &CacheStorage::changed, this, update);
    connect(backup, &BackupService::message, this, &DesktopController::notification);
    connect(backup, &BackupService::previewReady, this, [this] {
        view["restorePreview"] = backup->preview();
        emit changed();
        emit restorePreviewReady();
    });
    connect(m->downloads(), &DownloadService::folderRequired, this,
            &DesktopController::downloadFolderRequired);
    connect(c, &ConnectClient::error, this, &DesktopController::notification);
    connect(c, &ConnectClient::statusChanged, this, [this](QString text) {
        view["connection"] = text;
        refresh();
    });
    connect(c, &ConnectClient::stateChanged, this, update);
    connect(d, &Discovery::changed, this, update);
    connect(m->searchSystem(), &SearchEngine::pageReady, this, [this](QJsonObject page, bool) {
        auto items = page["items"].toArray();
        if (items.isEmpty())
            for (auto group : page["groups"].toArray())
                for (auto item : group.toObject()["items"].toArray())
                    items.append(item);
        display = items;
        view["results"] = decorate(display);
        view["continuation"] = page["continuation"];
        emit changed();
    });
    connect(m->searchSystem(), &SearchEngine::busyChanged, this, [this](bool busy) {
        view["searchBusy"] = busy;
        emit changed();
    });
    connect(m->searchSystem(), &SearchEngine::settingsChanged, this, update);
    connect(m->searchSystem(), &SearchEngine::historyChanged, this, update);
    connect(m, &MusicService::suggestionsReady, this, [this](QJsonObject suggestions) {
        view["suggestions"] = suggestions;
        emit changed();
    });
    connect(m, &MusicService::results, this, [this](QList<Track> list) {
        display = {};
        for (auto t : list)
            display.append(t.wire());
        view["results"] = decorate(display);
        emit changed();
    });
    connect(m, &MusicService::collectionLoaded, this, [this](QString token, QList<Track> list) {
        if (view["collectionId"] != token)
            return;
        QJsonArray items;
        for (auto track : list)
            items.append(track.wire());
        view["collection"] = decorate(items);
        view["collectionBusy"] = false;
        emit changed();
    });
    connect(m, &MusicService::collectionFailed, this, [this](QString, QString reason) {
        view["collectionBusy"] = false;
        emit changed();
        emit notification(reason);
    });
    refresh();
}
QString DesktopController::artwork(QVariantMap map) const {
    auto id = map["id"].toString();
    const auto downloadedCover = music->downloads()->cover(id);
    if (!downloadedCover.isEmpty())
        return QUrl::fromLocalFile(downloadedCover).toString();
    auto local = music->connectedCover(id);
    if (!local.isEmpty())
        return QUrl::fromLocalFile(local).toString();
    QUrl url(map["thumbnail"].toString());
    auto host = url.host().toLower();
    if (url.scheme() == "https" && url.userInfo().isEmpty() && url.port(443) == 443 &&
        (host == "i.ytimg.com" || host == "yt3.ggpht.com" || host.endsWith(".googleusercontent.com") ||
         host.endsWith(".ggpht.com")))
        return url.toString();
    return {};
}
QJsonArray DesktopController::decorate(QJsonArray entries) const {
    QJsonArray result;
    for (auto v : entries) {
        auto o = v.toObject();
        o["artwork"] = artwork(o.toVariantMap());
        o["artist"] = Track::fromWire(o).artist;
        o["liked"] = library->liked(o["id"].toString());
        o["downloaded"] = music->downloads()->contains(o["id"].toString());
        result.append(o);
    }
    return result;
}
QList<Track> DesktopController::tracks(QJsonArray items) const {
    QList<Track> list;
    for (auto v : items) {
        auto o = v.toObject();
        if (o["kind"].toString("song") == "song" && !o["id"].toString().isEmpty())
            list.append(Track::fromWire(o));
    }
    return list;
}
void DesktopController::refresh() {
    auto catalog = music->libraryCatalog();
    view["library"] =
        decorate(catalog->entries(section, scope, sort, descending, music->searchSystem()->settings()));
    auto home = catalog->entries("songs", "all", "added", true);
    while (home.size() > 9)
        home.removeLast();
    view["home"] = decorate(home);
    view["playlists"] = decorate(catalog->entries("playlists", "all", "added", true));
    view["stats"] = catalog->statistics();
    auto downloads = music->downloads()->state();
    downloads["items"] = decorate(downloads["items"].toArray());
    view["downloads"] = downloads;
    view["cache"] = music->cacheStorage()->state();
    view["results"] = decorate(display);
    view["collection"] = decorate(view["collection"].toArray());
    view["account"] = music->accountName();
    view["email"] = music->accountEmail();
    view["loggedIn"] = music->loggedIn();
    view["loginPending"] = music->loginPending();
    view["refreshing"] = catalog->busy();
    view["settings"] = music->searchSystem()->settings();
    view["preferences"] = catalog->preferences();
    view["systemFont"] = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    view["version"] = QCoreApplication::applicationVersion();
    view["history"] = QJsonArray::fromStringList(music->searchSystem()->history());
    view["controllers"] = connectClient->peerInfo();
    view["connected"] = connectClient->connected();
    view["hasTarget"] = connectClient->hasTarget();
    view["local"] = !connectClient->hasTarget() || connectClient->rendersLocally();
    view["identity"] = connectClient->identity();
    QJsonArray devices;
    for (auto device : discovery->devices())
        devices.append(QJsonObject{
            {"id", device.id}, {"host", device.host}, {"port", device.port}, {"name", device.name}});
    view["devices"] = devices;
    view["playback"] = music->playback();
    auto song = music->playback()["song"].toObject();
    song["artwork"] = artwork(song.toVariantMap());
    song["artist"] = Track::fromWire(song).artist;
    song["liked"] = library->liked(song["id"].toString());
    view["song"] = song;
    emit changed();
}
void DesktopController::search() {
    if (source == "online")
        music->searchSystem()->search(query, filter);
    else {
        music->cancelSearch();
        auto page = SearchEngine::localPage(query, filter, music->localSearchItems(source == "offline"));
        display = page["items"].toArray();
        if (display.isEmpty())
            for (auto group : page["groups"].toArray())
                for (auto item : group.toObject()["items"].toArray())
                    display.append(item);
        view["results"] = decorate(display);
        emit changed();
    }
}
void DesktopController::open(QJsonObject item) {
    auto kind = item["kind"].toString("song");
    if (kind == "song") {
        music->play({Track::fromWire(item)});
        return;
    }
    view["collectionTitle"] = item["title"];
    view["collectionId"] = item["id"];
    view["collectionBusy"] = false;
    auto local = item["localTracks"].toArray();
    if (local.isEmpty())
        local = item["songs"].toArray();
    view["collection"] = decorate(local);
    if (local.isEmpty()) {
        view["collectionBusy"] = true;
        music->browseCollection(item["id"].toString(), Track::fromWire(item), kind == "artist");
    }
    emit changed();
}
void DesktopController::action(QString name, QVariantMap args) {
    auto a = QJsonObject::fromVariantMap(args);
    auto item = a["item"].toObject();
    if (name == "login")
        music->login();
    else if (name == "logout")
        music->logout();
    else if (name == "import") {
        QStringList paths;
        for (auto v : a["urls"].toArray()) {
            auto url = QUrl(v.toString());
            if (url.isLocalFile())
                paths.append(url.toLocalFile());
        }
        music->importAudio(paths);
    } else if (name == "downloadFolder") {
        const auto url = QUrl(a["url"].toString());
        if (url.isLocalFile() && music->downloads()->setFolder(url.toLocalFile()))
            music->downloads()->enqueue(tracks(a["items"].toArray()));
    } else if (name == "downloadScan")
        music->downloads()->scan();
    else if (name == "download") {
        auto items = a["items"].toArray();
        if (items.isEmpty() && !item.isEmpty())
            items.append(item);
        music->downloads()->enqueue(tracks(items));
    } else if (name == "cacheRefresh")
        music->cacheStorage()->refresh();
    else if (name == "cacheLimits")
        music->cacheStorage()->setLimits(a["audioMiB"].toInt(), a["imageMiB"].toInt());
    else if (name == "cacheClear")
        music->clearCache(a["kind"].toString());
    else if (name == "backupExport") {
        const QUrl url(a["url"].toString());
        music->checkpoint();
        if (url.isLocalFile() && library->checkpoint())
            backup->exportTo(url.toLocalFile());
    } else if (name == "backupInspect") {
        const QUrl url(a["url"].toString());
        if (url.isLocalFile())
            backup->inspect(url.toLocalFile());
    } else if (name == "backupRestore") {
        if (!music->canRestoreBackup()) {
            emit notification(
                "Finish or cancel downloads and disconnect active device transfers before restoring.");
        } else if (backup->stageRestore())
            QCoreApplication::exit(42);
    } else if (name == "downloadCancel")
        music->downloads()->cancel(a["id"].toString());
    else if (name == "refresh")
        music->refreshCatalog({"songs", "albums", "artists", "playlists"});
    else if (name == "library") {
        section = a["section"].toString(section);
        scope = a["scope"].toString(scope);
        sort = a["sort"].toString(sort);
        descending = a["descending"].toBool(descending);
        refresh();
    } else if (name == "search") {
        query = a["query"].toString(query);
        filter = a["filter"].toString(filter);
        source = a["source"].toString(source);
        search();
    } else if (name == "suggest")
        music->suggest(a["query"].toString());
    else if (name == "more")
        music->searchSystem()->loadMore();
    else if (name == "historyRemove")
        music->searchSystem()->removeHistory(a["query"].toString());
    else if (name == "historyClear")
        music->searchSystem()->clearHistory();
    else if (name == "listeningHistoryClear")
        music->libraryCatalog()->clearListeningHistory();
    else if (name == "explore")
        music->explore();
    else if (name == "open")
        open(item);
    else if (name == "play") {
        auto items = a["items"].toArray();
        auto list = tracks(items);
        if (list.isEmpty()) {
            if (!item.isEmpty())
                open(item);
            else
                emit notification("Choose a song first.");
        } else
            music->play(list, qBound(0, a["index"].toInt(), int(list.size()) - 1));
    } else if (name == "radio")
        music->radio(Track::fromWire(item));
    else if (name == "like") {
        auto id = item["id"].toString();
        if (!id.isEmpty())
            library->setLiked(Track::fromWire(item), !library->liked(id));
    } else if (name == "bookmark")
        music->libraryCatalog()->bookmark(item, !music->libraryCatalog()->bookmarked(item["id"].toString()));
    else if (name == "command")
        music->command(a);
    else if (name == "seek")
        music->seek(qint64(a["position"].toDouble()));
    else if (name == "volume")
        music->volume(a["value"].toInt());
    else if (name == "settings")
        music->searchSystem()->setSettings(a);
    else if (name == "preferences")
        music->libraryCatalog()->setPreferences(a);
    else if (name == "tags")
        music->libraryCatalog()->setTags(a["id"].toString(), a["tags"].toVariant().toStringList());
    else if (name == "playlistOrder")
        music->libraryCatalog()->setPlaylistOrder(a["ids"].toVariant().toStringList());
    else if (name == "playlistDelete")
        library->removePlaylist(a["id"].toString());
    else if (name == "playlistSave") {
        auto id = a["id"].toString();
        auto list = tracks(a["items"].toArray());
        library->putPlaylist(id, a["title"].toString().trimmed().left(200), list);
    } else if (name == "playlistAdd") {
        for (auto v : library->snapshot()["playlists"].toArray()) {
            auto playlist = v.toObject();
            if (playlist["id"] != a["id"])
                continue;
            auto songs = playlist["songs"].toArray();
            songs.append(item);
            library->putPlaylist(a["id"].toString(), playlist["name"].toString(), tracks(songs));
            break;
        }
    } else if (name == "connect") {
        auto id = a["id"].toString();
        for (auto device : discovery->devices())
            if (device.id == id) {
                connectClient->attach(device.host, device.port, device.id);
                break;
            }
    } else if (name == "local")
        connectClient->chooseThisComputer();
    else if (name == "disconnect")
        connectClient->detach();
    else if (name == "discover")
        discovery->refresh();
    else if (name == "lyrics") {
        view["lyrics"] = music->connectedLyrics(music->playback()["song"].toObject()["id"].toString());
        emit changed();
    } else if (name == "pause" || name == "resume" || name == "next" || name == "previous" || name == "stop")
        music->action(name);
}
} // namespace velune
