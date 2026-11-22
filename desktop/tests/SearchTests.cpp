#include "services/SearchEngine.h"
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
QJsonObject summaryPage() {
    auto top = renderer("top");
    top["title"] = node({"runs"}, QJsonArray{run("Top")});
    top["onTap"] = top.take("navigationEndpoint");
    top.remove("flexColumns");
    top["contents"] = items({"top", "sub"});
    QJsonObject albums{{"title", node({"runs"}, QJsonArray{run("Albums")})},
                       {"contents", QJsonArray{QJsonObject{{"musicResponsiveListItemRenderer",
                                                            renderer("MPRE-result", "album")}}}}};
    return resultSections(
        {QJsonObject{{"musicCardShelfRenderer", top}}, QJsonObject{{"musicShelfRenderer", albums}}});
}
struct Fake {
    struct Request {
        QString endpoint;
        QJsonObject body;
        SearchEngine::Done done;
        bool session;
    };
    QList<Request> calls;
    SearchEngine::Transport transport() {
        return [this](QString endpoint, QJsonObject body, SearchEngine::Done done,
                      bool session) -> QNetworkReply * {
            calls.append({endpoint, body, done, session});
            return nullptr;
        };
    }
    void reply(int at, QJsonObject response) {
        const auto done = calls[at].done;
        done(response);
    }
};
} // namespace
class SearchTests : public QObject {
    Q_OBJECT
  private slots:
    void parserPreservesTypesMetadataAndSections() {
        for (auto kind : {"song", "album", "artist", "playlist"}) {
            const auto parsed = SearchParser::item(renderer(kind == QString("song") ? "video" : kind, kind));
            QCOMPARE(parsed["kind"].toString(), QString(kind));
            QCOMPARE(parsed["playlistId"].toString(), "PL-play");
            QVERIFY(!parsed["thumbnail"].toString().isEmpty());
        }
        auto videoWithoutRoot = renderer("video", "song", false, true);
        const auto endpoint = videoWithoutRoot.take("navigationEndpoint");
        auto columns = videoWithoutRoot["flexColumns"].toArray();
        auto titleRun = run("Video title");
        titleRun["navigationEndpoint"] = endpoint;
        columns[0] = column({titleRun});
        videoWithoutRoot["flexColumns"] = columns;
        QCOMPARE(SearchParser::item(videoWithoutRoot)["kind"].toString(), "video");
        auto song = SearchParser::item(renderer("one"));
        QCOMPARE(song["duration"].toInt(), 201);
        QCOMPARE(song["album"].toObject()["id"].toString(), "MPRE-album");
        auto page = SearchParser::page(summaryPage(), true);
        QCOMPARE(page["groups"].toArray().size(), 2);
        QCOMPARE(page["groups"].toArray()[0].toObject()["items"].toArray().size(), 2);
        QCOMPARE(page["groups"].toArray()[1].toObject()["items"].toArray()[0].toObject()["kind"].toString(),
                 "album");
        QVERIFY(SearchParser::page({}, true)["groups"].toArray().isEmpty());
        const auto wrapped = resultSections(
            {node({"itemSectionRenderer", "contents"}, items({"one", "one"})),
             node({"itemSectionRenderer", "contents"},
                  QJsonArray{QJsonObject{{"musicResponsiveListItemRenderer", renderer("MPRE", "album")}}})});
        QCOMPARE(SearchParser::page(wrapped, true)["groups"].toArray().size(), 2);
        QCOMPARE(
            SearchParser::page(wrapped, true)["groups"].toArray()[0].toObject()["items"].toArray().size(), 1);
        QJsonArray mixed{song, SearchParser::item(renderer("explicit", "song", true)),
                         SearchParser::item(renderer("video", "song", false, true)),
                         SearchParser::item(renderer("album", "album"))};
        QCOMPARE(SearchParser::visible(mixed, {{"hideExplicit", true}, {"hideVideos", true}}).size(), 2);
        QCOMPARE(SearchParser::filterParams("songs"), "EgWKAQIIAWoKEAkQBRAKEAMQBA%3D%3D");
        for (auto type : {"songs", "videos", "albums", "artists", "featured", "community"})
            QVERIFY(!SearchParser::filterParams(type).isEmpty());
    }
    void requestsFiltersContinuationDeduplicationAndRetry() {
        QTemporaryDir dir;
        Fake fake;
        SearchEngine engine(dir.path(), fake.transport());
        QSignalSpy ready(&engine, &SearchEngine::pageReady), failed(&engine, &SearchEngine::failed);
        engine.search("query", "songs");
        QCOMPARE(fake.calls.size(), 1);
        QCOMPARE(fake.calls[0].body["params"].toString(), SearchParser::filterParams("songs"));
        QVERIFY(fake.calls[0].session);
        fake.reply(0, shelfPage(items({"one", "one"}), "A"));
        QCOMPARE(ready.last()[0].toJsonObject()["items"].toArray().size(), 1);
        engine.loadMore();
        engine.loadMore();
        QCOMPARE(fake.calls.size(), 2);
        QCOMPARE(fake.calls[1].body["_continuation"].toString(), "A");
        fake.reply(1, {{"error", "offline"}});
        QCOMPARE(failed.size(), 1);
        engine.loadMore();
        QCOMPARE(fake.calls.size(), 3);
        fake.reply(2, shelfPage(items({"one", "two"}), "A", true));
        auto page = ready.last()[0].toJsonObject();
        QCOMPARE(page["items"].toArray().size(), 2);
        QVERIFY(page["continuation"].toString().isEmpty());
        QVERIFY(ready.last()[1].toBool());
        engine.loadMore();
        QCOMPARE(fake.calls.size(), 3);
        engine.search("query", "albums");
        QCOMPARE(fake.calls.size(), 4);
        fake.reply(3,
                   shelfPage({QJsonObject{{"musicResponsiveListItemRenderer", renderer("MPRE", "album")}}}));
        engine.search("query", "songs");
        QCOMPARE(fake.calls.size(), 4);
        QCOMPARE(ready.last()[0].toJsonObject()["items"].toArray().size(), 2);
    }
    void staleRepliesAndFallbackCannotReplaceNewQuery() {
        QTemporaryDir dir;
        Fake fake;
        SearchEngine engine(dir.path(), fake.transport());
        QSignalSpy ready(&engine, &SearchEngine::pageReady);
        engine.search("old", "songs");
        engine.search("new", "albums");
        fake.reply(0, shelfPage(items({"stale"})));
        QCOMPARE(ready.size(), 0);
        fake.reply(1,
                   shelfPage({QJsonObject{{"musicResponsiveListItemRenderer", renderer("MPRE", "album")}}}));
        QCOMPARE(ready.size(), 1);
        engine.search("fallback");
        fake.reply(2, shelfPage(items({"one"})));
        QCOMPARE(fake.calls.size(), 4);
        QCOMPARE(fake.calls[3].body["params"].toString(), SearchParser::filterParams("songs"));
        engine.search("replacement", "songs");
        fake.reply(3, shelfPage(items({"ignored"})));
        QCOMPARE(fake.calls.size(), 5);
        fake.reply(4, shelfPage(items({"replacement"}), "next"));
        engine.loadMore();
        engine.search("last", "songs");
        fake.reply(5, shelfPage(items({"late"}), "", true));
        QCOMPARE(ready.last()[0].toJsonObject()["query"].toString(), "replacement");
        fake.reply(6, shelfPage(items({"last"})));
        QCOMPARE(ready.last()[0].toJsonObject()["query"].toString(), "last");
    }
    void summaryFallbackMatchesAndroidOrderAndPreviewSize() {
        QTemporaryDir dir;
        Fake fake;
        SearchEngine engine(dir.path(), fake.transport());
        QSignalSpy ready(&engine, &SearchEngine::pageReady);
        engine.search("summary");
        fake.reply(0, {});
        QStringList ids;
        for (int i = 0; i < 20; ++i)
            ids << QString::number(i);
        fake.reply(1, shelfPage(items(ids)));
        QCOMPARE(fake.calls[2].body["params"].toString(), SearchParser::filterParams("albums"));
        fake.reply(2,
                   shelfPage({QJsonObject{{"musicResponsiveListItemRenderer", renderer("MPRE", "album")}}}));
        const auto groups = ready.last()[0].toJsonObject()["groups"].toArray();
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups[0].toObject()["title"].toString(), "Songs");
        QCOMPARE(groups[0].toObject()["items"].toArray().size(), 12);
        QCOMPARE(groups[1].toObject()["title"].toString(), "Albums");
    }
    void historySettingsPersistenceAndSuggestions() {
        QTemporaryDir dir;
        Fake fake;
        {
            SearchEngine engine(dir.path(), fake.transport());
            engine.remember(" first ");
            engine.remember("second");
            engine.remember("first");
            QCOMPARE(engine.history(), QStringList({"first", "second"}));
            QVERIFY(engine.history("irs").isEmpty());
            QCOMPARE(engine.history("fir"), QStringList{"first"});
            engine.setSettings(
                {{"hideExplicit", true}, {"hideVideos", true}, {"language", "ar"}, {"country", "eg"}});
            QCOMPARE(engine.settings()["country"].toString(), "EG");
            QSignalSpy suggestions(&engine, &SearchEngine::suggestionsReady);
            engine.suggest("first");
            QVERIFY(!fake.calls.last().session);
            QJsonArray queries{
                node({"searchSuggestionRenderer", "suggestion", "runs"}, QJsonArray{run("first")}),
                node({"searchSuggestionRenderer", "suggestion", "runs"}, QJsonArray{run("fresh")})};
            QJsonArray recommendations{
                QJsonObject{{"musicResponsiveListItemRenderer", renderer("bad", "song", true)}},
                QJsonObject{{"musicResponsiveListItemRenderer", renderer("good")}}};
            QJsonObject response{
                {"contents",
                 QJsonArray{node({"searchSuggestionsSectionRenderer", "contents"}, queries),
                            node({"searchSuggestionsSectionRenderer", "contents"}, recommendations)}}};
            fake.reply(0, response);
            auto page = suggestions.last()[0].toJsonObject();
            QCOMPARE(page["history"].toArray(), QJsonArray{"first"});
            QCOMPARE(page["queries"].toArray(), QJsonArray{"fresh"});
            QCOMPARE(page["items"].toArray().size(), 1);
            engine.setSettings({{"pauseHistory", true}});
            engine.remember("ignored");
            QVERIFY(!engine.history().contains("ignored"));
            engine.removeHistory("second");
        }
        SearchEngine restored(dir.path(), fake.transport());
        QCOMPARE(restored.history(), QStringList{"first"});
        QCOMPARE(restored.settings()["language"].toString(), "ar");
        restored.clearHistory();
        QVERIFY(restored.history().isEmpty());
        QSignalSpy failed(&restored, &SearchEngine::failed);
        restored.setSettings({{"country", "bad"}});
        QCOMPARE(failed.size(), 1);
        QCOMPARE(restored.settings()["country"].toString(), "EG");
    }
    void transientRetriesAreBoundedAndCancelledWhenTypingChanges() {
        QTemporaryDir dir;
        Fake fake;
        SearchEngine engine(dir.path(), fake.transport());
        QSignalSpy ready(&engine, &SearchEngine::pageReady), failed(&engine, &SearchEngine::failed);
        engine.search("retry", "songs");
        fake.reply(0, {{"error", "network"}, {"retryable", true}});
        QTRY_COMPARE_WITH_TIMEOUT(fake.calls.size(), 2, 1500);
        fake.reply(1, {{"error", "network"}, {"retryable", true}});
        engine.search("replacement", "songs");
        fake.reply(2, shelfPage(items({"valid"})));
        QTest::qWait(1100);
        QCOMPARE(fake.calls.size(), 3);
        QCOMPARE(ready.size(), 1);
        QCOMPARE(failed.size(), 0);
    }
    void localSearchUsesOnlySuppliedLibraryData() {
        auto videoWithoutRoot = renderer("video", "song", false, true);
        const auto endpoint = videoWithoutRoot.take("navigationEndpoint");
        auto columns = videoWithoutRoot["flexColumns"].toArray();
        auto titleRun = run("Video title");
        titleRun["navigationEndpoint"] = endpoint;
        columns[0] = column({titleRun});
        videoWithoutRoot["flexColumns"] = columns;
        QCOMPARE(SearchParser::item(videoWithoutRoot)["kind"].toString(), "video");
        auto song = SearchParser::item(renderer("one"));
        auto album = SearchParser::item(renderer("album", "album"));
        const auto page = SearchEngine::localPage("Title", "all", {song, album});
        QCOMPARE(page["groups"].toArray().size(), 2);
        QCOMPARE(SearchEngine::localPage("Title", "albums", {song, album})["items"].toArray().size(), 1);
        QVERIFY(SearchEngine::localPage("absent", "all", {song, album})["items"].toArray().isEmpty());
    }
};
QTEST_GUILESS_MAIN(SearchTests)
#include "SearchTests.moc"
