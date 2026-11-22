#include "services/LibraryCatalog.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
using namespace velune;
namespace {
QJsonObject node(QStringList keys, QJsonValue value) {
    for (int i = keys.size() - 1; i >= 0; --i)
        value = QJsonObject{{keys[i], value}};
    return value.toObject();
}
QJsonObject run(QString name, QString id = {}) {
    QJsonObject result{{"text", name}};
    if (!id.isEmpty())
        result["navigationEndpoint"] = node({"browseEndpoint", "browseId"}, id);
    return result;
}
QJsonObject column(QJsonArray runs) {
    return node({"musicResponsiveListItemFlexColumnRenderer", "text", "runs"}, runs);
}
QJsonObject renderer(QString id, QString kind = "song", bool explicitContent = false, bool video = false) {
    QJsonObject endpoint, object;
    if (kind == "song") {
        object["playlistItemData"] = QJsonObject{{"videoId", id}};
        auto watch =
            node({"watchEndpointMusicSupportedConfigs", "watchEndpointMusicConfig", "musicVideoType"},
                 video ? "MUSIC_VIDEO_TYPE_OMV" : "MUSIC_VIDEO_TYPE_ATV");
        watch["videoId"] = id;
        endpoint["watchEndpoint"] = watch;
    } else {
        const auto type = kind == "album"    ? "MUSIC_PAGE_TYPE_ALBUM"
                          : kind == "artist" ? "MUSIC_PAGE_TYPE_ARTIST"
                                             : "MUSIC_PAGE_TYPE_PLAYLIST";
        auto browse = node(
            {"browseEndpointContextSupportedConfigs", "browseEndpointContextMusicConfig", "pageType"}, type);
        browse["browseId"] = id;
        endpoint["browseEndpoint"] = browse;
    }
    object["navigationEndpoint"] = endpoint;
    object["flexColumns"] = QJsonArray{column({run("Title " + id)}),
                                       column({run("Artist", "UC-author"), run(" • "),
                                               run("Album", "MPRE-album"), run(" • "), run("3:21")})};
    object["thumbnail"] = node({"musicThumbnailRenderer", "thumbnail", "thumbnails"},
                               QJsonArray{QJsonObject{{"url", "https://example.com/art.jpg"}}});
    object["overlay"] = node({"musicItemThumbnailOverlayRenderer", "content", "musicPlayButtonRenderer",
                              "playNavigationEndpoint", "watchPlaylistEndpoint", "playlistId"},
                             "PL-play");
    if (explicitContent)
        object["badges"] =
            QJsonArray{node({"musicInlineBadgeRenderer", "icon", "iconType"}, "MUSIC_EXPLICIT_BADGE")};
    return object;
}
QJsonArray items(QStringList ids) {
    QJsonArray result;
    for (auto id : ids)
        result.append(QJsonObject{{"musicResponsiveListItemRenderer", renderer(id)}});
    return result;
}
QJsonArray tokens(QString token) {
    return token.isEmpty() ? QJsonArray{} : QJsonArray{node({"nextContinuationData", "continuation"}, token)};
}
QJsonObject resultSections(QJsonArray sections) {
    return node({"contents", "tabbedSearchResultsRenderer", "tabs"},
                QJsonArray{node({"tabRenderer", "content", "sectionListRenderer", "contents"}, sections)});
}
QJsonObject shelfPage(QJsonArray entries, QString token = {}, bool continued = false) {
    QJsonObject shelf{{"title", node({"runs"}, QJsonArray{run("Songs")})},
                      {"contents", entries},
                      {"continuations", tokens(token)}};
    if (continued)
        return node({"continuationContents", "musicShelfContinuation"}, shelf);
    return resultSections({QJsonObject{{"musicShelfRenderer", shelf}}});
}

struct Fake {
    struct Request {
        QJsonObject body;
        SearchEngine::Done done;
        bool session;
    };
    QList<Request> calls;
    SearchEngine::Transport transport() {
        return [this](QString endpoint, QJsonObject body, SearchEngine::Done done,
                      bool session) -> QNetworkReply * {
            Q_ASSERT(endpoint == "browse");
            calls.append({body, done, session});
            return nullptr;
        };
    }
    void reply(int at, QJsonObject response) {
        auto done = calls[at].done;
        done(response);
    }
};
} // namespace
class LibraryCatalogTests : public QObject {
    Q_OBJECT
  private slots:
    void membershipOfflineAndSavedEntities() {
        QTemporaryDir dir;
        Library library(dir.path());
        Fake fake;
        LibraryCatalog catalog(&library, fake.transport());
        Track liked{"liked", "Liked", "Artist", {}, 180, "Album", "MPRE-album"};
        Track other{"other", "Other", "Artist", {}, 180};
        library.setLiked(liked, true);
        library.rememberTracks({other});
        QCOMPARE(catalog.entries("songs").size(), 1);
        QCOMPARE(catalog.entries("songs", "library").size(), 2);
        QCOMPARE(catalog.entries().size(), 4); // Pinned lists only; an artist credit is not a bookmark.
        QCOMPARE(catalog.entries("albums", "library").size(), 1);
        QCOMPARE(catalog.entries("artists", "library").size(), 1);
        auto album = catalog.entries("albums", "library")[0].toObject();
        catalog.bookmark(album, true);
        QCOMPARE(catalog.entries().size(), 5);
        QCOMPARE(catalog.entries("albums").size(), 1);
        QCOMPARE(catalog.entries("albums", "fullOffline").size(), 0);
        catalog.bookmark(album, false);
        QCOMPARE(catalog.entries("albums").size(), 0);
        QFile file(dir.filePath("song.flac"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("fixture");
        file.close();
        library.registerMedia(other, file.fileName());
        QCOMPARE(catalog.entries("songs", "offline").size(), 1);
        QVERIFY(QFile::remove(file.fileName()));
        QCOMPARE(catalog.entries("songs", "offline").size(), 0);
        library.setLiked(liked, false);
        liked.thumbnail = "updated";
        library.rememberTracks({liked}, true);
        QVERIFY(!library.liked(liked.id)); // Refresh must preserve a deliberate unlike.
        QCOMPARE(library.snapshot()["likes"]
                     .toArray()[0]
                     .toObject()["track"]
                     .toObject()["artists"]
                     .toArray()[0]
                     .toString(),
                 "Artist");
    }
    void playlistsTagsCustomOrderAndPersistence() {
        QTemporaryDir dir;
        QString first, second;
        {
            Library library(dir.path());
            Fake fake;
            LibraryCatalog catalog(&library, fake.transport());
            Track track{"one", "One", "Artist"};
            first = library.putPlaylist({}, "First", {track});
            second = library.putPlaylist({}, "Second", {});
            QVERIFY(!first.isEmpty());
            QVERIFY(!second.isEmpty());
            QVERIFY(catalog.bookmarked(first));
            catalog.setTags(first, {" Rock ", "Rock", "Night"});
            QCOMPARE(catalog.tags(), QStringList({"Night", "Rock"}));
            catalog.setPlaylistOrder({second, first});
            auto entries = catalog.entries("playlists", "library", "custom");
            QCOMPARE(entries.size(), 6);
            QCOMPARE(entries[4].toObject()["id"].toString(), second);
            QCOMPARE(entries[5].toObject()["songCount"].toInt(), 1);
            catalog.setPreferences(
                {{"view", "grid"}, {"showCached", false}, {"selectedTags", QJsonArray{"Rock"}}});
            QCOMPARE(catalog.entries("playlists").size(), 4); // Three pins plus tagged playlist.
            QCOMPARE(library.putPlaylist(first, "Renamed", {track}), first);
        }
        Library library(dir.path());
        Fake fake;
        LibraryCatalog catalog(&library, fake.transport());
        QCOMPARE(catalog.preferences()["view"].toString(), "grid");
        QCOMPARE(catalog.entries("playlists")[3].toObject()["title"].toString(), "Renamed");
        QVERIFY(library.removePlaylist(first));
        QCOMPARE(catalog.entries("playlists").size(), 3);
        QVERIFY(!library.removePlaylist(first));
        QVERIFY(!catalog.bookmarked(first));
        bool tombstone = false;
        for (auto value : library.snapshot()["playlists"].toArray())
            if (value.toObject()["id"] == first)
                tombstone = value.toObject()["removed"].toBool();
        QVERIFY(tombstone);
    }
    void historyUsesActualListeningAndSurvivesReload() {
        QTemporaryDir dir;
        {
            Library library(dir.path());
            Fake fake;
            LibraryCatalog catalog(&library, fake.transport());
            Track first{"one", "One", "Artist"}, second{"two", "Two", "Artist"};
            library.recordListening(first, 120000, "2026-11-22T10:00:00.000");
            library.recordListening(second, 60000, "2026-11-22T11:00:00.000");
            library.recordListening(first, 1000, "2026-11-22T12:00:00.000");
            QCOMPARE(catalog.statistics()["listeningMs"].toInt(), 181000);
            QCOMPARE(catalog.entries("top")[0].toObject()["id"].toString(), "one");
            QCOMPARE(catalog.entries("history")[0].toObject()["timestamp"].toString(),
                     "2026-11-22T12:00:00.000");
            catalog.observePlayback({{"song", second.wire()}, {"playing", true}, {"position", 0}}, true);
            QTest::qWait(25);
            catalog.observePlayback({{"song", second.wire()}, {"playing", false}, {"position", 3599}}, true);
            const auto total = catalog.statistics()["listeningMs"].toInt();
            QVERIFY(total >= 181020 && total < 182000); // Seeking does not create hours of listening.
            catalog.observePlayback({{"song", second.wire()}, {"playing", true}}, false);
            QTest::qWait(25);
            catalog.observePlayback({{"song", second.wire()}, {"playing", false}}, false);
            QCOMPARE(catalog.statistics()["listeningMs"].toInt(), total);
        }
        Library library(dir.path());
        Fake fake;
        LibraryCatalog catalog(&library, fake.transport());
        QCOMPARE(catalog.statistics()["songs"].toInt(), 2);
        QCOMPARE(catalog.statistics()["events"].toInt(), 4);
    }
    void syncPaginatesPreservesDataAndRejectsStaleReplies() {
        QTemporaryDir dir;
        Library library(dir.path());
        Fake fake;
        LibraryCatalog catalog(&library, fake.transport());
        catalog.refresh({"liked", "songs", "artists", "playlists"});
        QCOMPARE(fake.calls[0].body["browseId"].toString(), "VLLM");
        QVERIFY(fake.calls[0].session);
        fake.reply(0, shelfPage(items({"one"}), "next"));
        QCOMPARE(library.tracks().size(), 0); // Commit only complete pages.
        QCOMPARE(fake.calls[1].body["_continuation"].toString(), "next");
        fake.reply(1, shelfPage(items({"two"}), {}, true));
        QVERIFY(library.liked("one"));
        QVERIFY(library.liked("two"));
        QCOMPARE(fake.calls[2].body["browseId"].toString(), "FEmusic_liked_videos");
        fake.reply(2, shelfPage(items({"other"})));
        QVERIFY(!library.liked("other"));
        QCOMPARE(fake.calls[3].body["browseId"].toString(), "FEmusic_library_corpus_artists");
        fake.reply(
            3,
            shelfPage({QJsonObject{{"musicResponsiveListItemRenderer", renderer("UC-author", "artist")}}}));
        QCOMPARE(fake.calls[4].body["browseId"].toString(), "FEmusic_liked_playlists");
        fake.reply(
            4,
            shelfPage({QJsonObject{{"musicResponsiveListItemRenderer", renderer("VLsaved", "playlist")}}}));
        QVERIFY(!catalog.busy());
        QCOMPARE(catalog.entries("artists").size(), 1);
        QCOMPARE(catalog.entries("playlists").size(), 5);
        catalog.refresh({"artists"});
        fake.reply(5, {{"error", "network"}});
        QCOMPARE(catalog.entries("artists").size(), 1);
        catalog.refresh({"artists"});
        catalog.cancel();
        fake.reply(6, shelfPage({}));
        QCOMPARE(catalog.entries("artists").size(), 1);
        catalog.refresh({"songs"});
        fake.reply(7, shelfPage(items({"partial"}), "loop"));
        fake.reply(8, shelfPage(items({"partial"}), "loop", true));
        QVERIFY(!catalog.busy());
        QCOMPARE(library.tracks().size(), 3);
    }
    void completeAlbumsAreRequiredForFullyOffline() {
        QTemporaryDir dir;
        Library library(dir.path());
        Fake fake;
        LibraryCatalog catalog(&library, fake.transport());
        catalog.refresh({"albums"});
        QCOMPARE(fake.calls[0].body["browseId"].toString(), "FEmusic_liked_albums");
        fake.reply(
            0,
            shelfPage({QJsonObject{{"musicResponsiveListItemRenderer", renderer("MPRE-album", "album")}}}));
        QCOMPARE(fake.calls[1].body["browseId"].toString(), "VLPL-play");
        fake.reply(1, shelfPage(items({"one"}), "album-next"));
        QCOMPARE(catalog.entries("albums")[0].toObject()["totalSongs"].toInt(), 0);
        fake.reply(2, node({"continuationContents", "musicPlaylistShelfContinuation"},
                           QJsonObject{{"contents", items({"two"})}}));
        QCOMPARE(catalog.entries("albums")[0].toObject()["totalSongs"].toInt(), 2);
        QCOMPARE(catalog.entries("albums", "fullOffline").size(), 0);
        QFile file(dir.filePath("audio.flac"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("fixture");
        file.close();
        library.registerMedia({"one", "One", "Artist"}, file.fileName());
        QCOMPARE(catalog.entries("albums", "offline").size(), 1);
        QCOMPARE(catalog.entries("albums", "fullOffline").size(), 0);
        library.registerMedia({"two", "Two", "Artist"}, file.fileName());
        QCOMPARE(catalog.entries("albums", "fullOffline").size(), 1);
        QCOMPARE(catalog.entries("albums", "fullOffline")[0].toObject()["songCount"].toInt(), 2);
        QVERIFY(!catalog.busy());
    }
};
QTEST_GUILESS_MAIN(LibraryCatalogTests)
#include "LibraryCatalogTests.moc"
