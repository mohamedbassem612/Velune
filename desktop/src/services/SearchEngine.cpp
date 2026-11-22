#include "SearchEngine.h"
#include "core/Library.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
namespace velune {
namespace {
QString text(QJsonObject object) {
    if (object.contains("simpleText"))
        return object["simpleText"].toString();
    QString result;
    for (auto run : object["runs"].toArray())
        result += run.toObject()["text"].toString();
    return result;
}
QJsonValue find(const QJsonValue &value, QString key) {
    if (value.isObject()) {
        auto object = value.toObject();
        if (object.contains(key))
            return object[key];
        for (auto child : object) {
            auto result = find(child, key);
            if (!result.isUndefined() && !result.isNull())
                return result;
        }
    } else if (value.isArray())
        for (auto child : value.toArray()) {
            auto result = find(child, key);
            if (!result.isUndefined() && !result.isNull())
                return result;
        }
    return {};
}
QString image(QJsonObject renderer) {
    auto container = renderer["thumbnail"].toObject();
    if (container.isEmpty())
        container = renderer["thumbnailRenderer"].toObject();
    auto images = find(container, "thumbnails").toArray();
    return images.isEmpty() ? QString{} : images.last().toObject()["url"].toString();
}
QJsonArray unique(QJsonArray items) {
    QSet<QString> seen;
    QJsonArray result;
    for (auto value : items) {
        const auto id = value.toObject()["id"].toString();
        if (!id.isEmpty() && !seen.contains(id)) {
            seen.insert(id);
            result.append(value);
        }
    }
    return result;
}
QString continuation(QJsonValue value) {
    auto token = find(value, "nextContinuationData").toObject()["continuation"].toString();
    if (token.isEmpty())
        token = find(value, "continuationCommand").toObject()["token"].toString();
    return token;
}
QJsonArray contents(QJsonArray values, bool summary) {
    QJsonArray result;
    for (auto value : values) {
        const auto object = value.toObject();
        for (auto key : {"musicResponsiveListItemRenderer", "musicTwoRowItemRenderer"}) {
            if (!object.contains(key))
                continue;
            auto item = SearchParser::item(object[key].toObject(), summary);
            if (!item.isEmpty())
                result.append(item);
        }
    }
    return unique(result);
}
} // namespace
QString SearchParser::filterParams(const QString &filter) {
    static const QHash<QString, QString> filters{
        {"songs", "EgWKAQIIAWoKEAkQBRAKEAMQBA%3D%3D"},      {"videos", "EgWKAQIQAWoKEAkQChAFEAMQBA%3D%3D"},
        {"albums", "EgWKAQIYAWoKEAkQChAFEAMQBA%3D%3D"},     {"artists", "EgWKAQIgAWoKEAkQChAFEAMQBA%3D%3D"},
        {"featured", "EgeKAQQoADgBagwQDhAKEAMQBRAJEAQ%3D"}, {"community", "EgeKAQQoAEABagoQAxAEEAoQCRAF"}};
    return filters.value(filter);
}
QJsonObject SearchParser::item(const QJsonObject &renderer, bool summary) {
    auto endpoint = renderer["navigationEndpoint"].toObject();
    if (endpoint.isEmpty())
        endpoint = renderer["onTap"].toObject();
    const auto browse = endpoint["browseEndpoint"].toObject();
    auto watch = endpoint["watchEndpoint"].toObject();
    if (watch.isEmpty()) {
        const auto columns = renderer["flexColumns"].toArray();
        if (!columns.isEmpty())
            watch = find(columns.first(), "watchEndpoint").toObject();
    }
    if (watch.isEmpty())
        watch = find(renderer["overlay"], "watchEndpoint").toObject();
    const auto pageType = find(browse, "pageType").toString();
    QString kind, id = renderer["playlistItemData"].toObject()["videoId"].toString();
    if (id.isEmpty())
        id = watch["videoId"].toString();
    const auto browseId = browse["browseId"].toString();
    if (!browseId.isEmpty()) {
        id = browseId;
        if (pageType.endsWith("ALBUM") || pageType.endsWith("AUDIOBOOK") || id.startsWith("MPRE"))
            kind = "album";
        else if (pageType.endsWith("ARTIST") || id.startsWith("UC"))
            kind = "artist";
        else if (pageType.endsWith("PLAYLIST") || id.startsWith("VL")) {
            kind = "playlist";
            if (id.startsWith("VL"))
                id = id.mid(2);
        } else
            return {};
    } else if (!id.isEmpty())
        kind = "song";
    else
        return {};
    const auto musicVideoType = find(watch, "musicVideoType").toString();
    const bool video = musicVideoType == "MUSIC_VIDEO_TYPE_OMV" || musicVideoType == "MUSIC_VIDEO_TYPE_UGC";
    if (video && kind == "song")
        kind = "video";
    auto title = text(renderer["title"].toObject());
    QJsonObject secondary = renderer["subtitle"].toObject();
    QJsonArray runs = secondary["runs"].toArray();
    const auto columns = renderer["flexColumns"].toArray();
    if (!columns.isEmpty())
        title = text(
            columns[0].toObject()["musicResponsiveListItemFlexColumnRenderer"].toObject()["text"].toObject());
    for (int i = 1; i < columns.size(); ++i) {
        auto column =
            columns[i].toObject()["musicResponsiveListItemFlexColumnRenderer"].toObject()["text"].toObject();
        if (i == 1)
            secondary = column;
        if (i > 1)
            runs.append(QJsonObject{{"text", " • "}});
        for (auto run : column["runs"].toArray())
            runs.append(run);
    }
    if (title.isEmpty())
        return {};
    QJsonArray artists;
    QSet<QString> names;
    QJsonObject album;
    for (auto value : runs) {
        auto run = value.toObject();
        const auto name = run["text"].toString();
        const auto link = run["navigationEndpoint"].toObject()["browseEndpoint"].toObject();
        const auto linkId = link["browseId"].toString();
        if (linkId.startsWith("MPRE"))
            album = {{"id", linkId}, {"name", name}};
        else if (!linkId.isEmpty() && !names.contains(name)) {
            artists.append(QJsonObject{{"name", name}, {"id", linkId}});
            names.insert(name);
        }
    }
    const auto parts = text(secondary).split(QRegularExpression("\\s*[•·]\\s*"), Qt::SkipEmptyParts);
    if (artists.isEmpty() && kind != "artist" && !parts.isEmpty()) {
        const bool labelled = summary || kind == "album";
        int at = (labelled && parts.size() > 1) ? 1 : 0;
        if (!QRegularExpression("^[0-9:]+$").match(parts[at]).hasMatch())
            artists.append(QJsonObject{{"name", parts[at]}});
    }
    int duration = 0;
    for (const auto &part : parts)
        if (QRegularExpression("^\\d+(?::\\d{2}){1,2}$").match(part).hasMatch()) {
            duration = 0;
            for (auto segment : part.split(':'))
                duration = duration * 60 + segment.toInt();
        }
    auto play = find(renderer["overlay"], "playNavigationEndpoint").toObject();
    if (play.isEmpty())
        play = find(renderer["thumbnailOverlay"], "playNavigationEndpoint").toObject();
    if (play.isEmpty())
        play = find(renderer["buttons"], "command").toObject();
    auto playWatch = find(play, "watchPlaylistEndpoint").toObject();
    if (playWatch.isEmpty())
        playWatch = find(play, "watchEndpoint").toObject();
    if (playWatch.isEmpty())
        playWatch = find(renderer["menu"], "watchPlaylistEndpoint").toObject();
    if (playWatch.isEmpty())
        playWatch = watch;
    bool explicitContent = false;
    auto badges = renderer["badges"].toArray();
    for (auto badge : renderer["subtitleBadges"].toArray())
        badges.append(badge);
    for (auto badge : badges)
        if (find(badge, "iconType").toString() == "MUSIC_EXPLICIT_BADGE")
            explicitContent = true;
    QString author;
    for (auto value : artists) {
        if (!author.isEmpty())
            author += ", ";
        author += value.toObject()["name"].toString();
    }
    int year = 0;
    for (const auto &part : parts)
        if (QRegularExpression("^[12][0-9]{3}$").match(part).hasMatch())
            year = part.toInt();
    return {{"id", id},
            {"title", title},
            {"artists", artists},
            {"subtitle", text(secondary)},
            {"thumbnail", image(renderer)},
            {"duration", duration},
            {"album", album},
            {"kind", kind},
            {"browseId", browseId},
            {"playlistId", playWatch["playlistId"]},
            {"watchEndpoint", playWatch},
            {"explicit", explicitContent},
            {"isVideo", video},
            {"year", year}};
}
QJsonArray SearchParser::visible(const QJsonArray &items, const QJsonObject &settings) {
    QJsonArray result;
    for (auto value : items) {
        auto item = value.toObject();
        if (settings["hideExplicit"].toBool() && item["explicit"].toBool())
            continue;
        if (settings["hideVideos"].toBool() && (item["isVideo"].toBool() || item["kind"] == "video"))
            continue;
        result.append(item);
    }
    return result;
}
QJsonObject SearchParser::suggestions(const QJsonObject &response) {
    QJsonArray queries, items;
    const auto sections = response["contents"].toArray();
    if (!sections.isEmpty())
        for (auto value :
             sections[0].toObject()["searchSuggestionsSectionRenderer"].toObject()["contents"].toArray()) {
            const auto query =
                text(value.toObject()["searchSuggestionRenderer"].toObject()["suggestion"].toObject());
            if (!query.isEmpty())
                queries.append(query);
        }
    if (sections.size() > 1) {
        auto parsed = contents(
            sections[1].toObject()["searchSuggestionsSectionRenderer"].toObject()["contents"].toArray(),
            true);
        for (auto value : parsed)
            if (value.toObject()["kind"] != "playlist")
                items.append(value);
    }
    return {{"queries", queries}, {"items", items}};
}
QJsonObject SearchParser::page(const QJsonObject &response, bool summary) {
    QJsonArray groups, items;
    QString token;
    auto continued = response["continuationContents"].toObject()["musicShelfContinuation"].toObject();
    if (!continued.isEmpty()) {
        items = contents(continued["contents"].toArray(), false);
        if (!items.isEmpty())
            token = continuation(continued["continuations"]);
    } else {
        const auto tabs =
            response["contents"].toObject()["tabbedSearchResultsRenderer"].toObject()["tabs"].toArray();
        const auto sections = tabs.isEmpty() ? QJsonArray{}
                                             : tabs.first()
                                                   .toObject()["tabRenderer"]
                                                   .toObject()["content"]
                                                   .toObject()["sectionListRenderer"]
                                                   .toObject()["contents"]
                                                   .toArray();
        for (auto value : sections) {
            const auto section = value.toObject();
            QJsonObject shelf;
            QString title;
            if (summary && section.contains("itemSectionRenderer")) {
                const auto entries =
                    contents(section["itemSectionRenderer"].toObject()["contents"].toArray(), true);
                for (auto entry : entries) {
                    const auto kind = entry.toObject()["kind"].toString();
                    const auto title = kind == "song"     ? "Songs"
                                       : kind == "video"  ? "Videos"
                                       : kind == "album"  ? "Albums"
                                       : kind == "artist" ? "Artists"
                                                          : "Playlists";
                    int target = -1;
                    for (int i = 0; i < groups.size(); ++i)
                        if (groups[i].toObject()["title"].toString() == title) {
                            target = i;
                            break;
                        }
                    QJsonObject group =
                        target < 0 ? QJsonObject{{"title", title}} : groups[target].toObject();
                    auto items = group["items"].toArray();
                    items.append(entry);
                    group["items"] = unique(items);
                    if (target < 0)
                        groups.append(group);
                    else
                        groups[target] = group;
                }
            } else if (section.contains("musicCardShelfRenderer")) {
                shelf = section["musicCardShelfRenderer"].toObject();
                title = text(find(shelf["header"], "title").toObject());
                if (title.isEmpty())
                    title = "Top result";
                auto entries = contents(shelf["contents"].toArray(), true);
                auto top = item(shelf, true);
                if (!top.isEmpty())
                    entries.prepend(top);
                if (!entries.isEmpty())
                    groups.append(QJsonObject{{"title", title}, {"items", unique(entries)}});
            } else if (section.contains("musicShelfRenderer") ||
                       section.contains("musicCarouselShelfRenderer")) {
                shelf = section[section.contains("musicShelfRenderer") ? "musicShelfRenderer"
                                                                       : "musicCarouselShelfRenderer"]
                            .toObject();
                title = text(shelf["title"].toObject());
                if (title.isEmpty())
                    title = text(find(shelf["header"], "title").toObject());
                auto entries = contents(shelf["contents"].toArray(), summary);
                if (summary) {
                    if (!entries.isEmpty())
                        groups.append(
                            QJsonObject{{"title", title.isEmpty() ? "Other" : title}, {"items", entries}});
                } else {
                    items = entries;
                    token = continuation(shelf["continuations"]);
                }
            }
        }
    }
    return {{"groups", groups}, {"items", items}, {"continuation", token}};
}
SearchEngine::SearchEngine(QString root, Transport send, QObject *parent)
    : QObject(parent), directory(root), transport(std::move(send)) {
    const auto locale = QLocale::system().name().split('_');
    preferences = {{"language", locale.value(0).size() >= 2 ? locale.value(0) : "en"},
                   {"country", locale.value(1, "US")},
                   {"hideExplicit", false},
                   {"hideVideos", false},
                   {"pauseHistory", false},
                   {"useLogin", true}};
    QFile file(directory + "/search.json");
    if (file.open(QIODevice::ReadOnly)) {
        const auto saved = QJsonDocument::fromJson(file.readAll()).object();
        const auto settings = saved["settings"].toObject();
        for (auto key : preferences.keys())
            if (settings.contains(key))
                preferences[key] = settings[key];
        for (auto value : saved["history"].toArray())
            if (value.isString() && !queries.contains(value.toString()))
                queries.append(value.toString());
    }
}
void SearchEngine::save() {
    QDir().mkpath(directory);
    QSaveFile file(directory + "/search.json");
    QJsonArray history;
    for (auto query : queries)
        history.append(query);
    const auto data = QJsonDocument(QJsonObject{{"settings", preferences}, {"history", history}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        emit failed("Could not save search preferences or history.");
    else
        QFile::setPermissions(directory + "/search.json", QFile::ReadOwner | QFile::WriteOwner);
}
void SearchEngine::setBusy(bool value) {
    if (busy == value)
        return;
    busy = value;
    emit busyChanged(value);
}
void SearchEngine::cancel() {
    ++generation;
    for (auto reply : requests)
        if (reply && !reply->isFinished()) {
            reply->setProperty("veluneSearchCancelled", true);
            reply->abort();
        }
    requests.clear();
    setBusy(false);
}
void SearchEngine::setSettings(QJsonObject settings) {
    auto updated = preferences;
    for (auto key : preferences.keys())
        if (settings.contains(key))
            updated[key] = settings[key];
    auto language = updated["language"].toString().trimmed();
    auto country = updated["country"].toString().trimmed().toUpper();
    if (!QRegularExpression("^[a-z]{2,3}(-[A-Za-z]{2,4})?$").match(language).hasMatch() ||
        !QRegularExpression("^[A-Z]{2}$").match(country).hasMatch()) {
        emit failed("Enter a language code such as ar or en and a country code such as EG or US.");
        return;
    }
    updated["language"] = language;
    updated["country"] = country;
    if (updated == preferences)
        return;
    cancel();
    cache.clear();
    preferences = updated;
    save();
    emit settingsChanged();
}
QStringList SearchEngine::history(QString input) const {
    QStringList result;
    for (const auto &query : queries)
        if (input.isEmpty() || query.startsWith(input, Qt::CaseInsensitive))
            result.append(query);
    return input.isEmpty() ? result : result.mid(0, 3);
}
void SearchEngine::remember(QString query) {
    query = query.trimmed();
    if (query.isEmpty() || preferences["pauseHistory"].toBool())
        return;
    queries.removeAll(query);
    queries.prepend(query);
    save();
    emit historyChanged();
}
void SearchEngine::removeHistory(QString query) {
    if (!queries.removeAll(query))
        return;
    save();
    emit historyChanged();
}
void SearchEngine::clearHistory() {
    queries.clear();
    save();
    emit historyChanged();
}
QString SearchEngine::cacheKey() const {
    return query + QChar(0) + filter;
}
void SearchEngine::send(QString endpoint, QJsonObject body, quint64 ticket, Done done, bool session,
                        int attempt) {
    if (ticket != generation)
        return;
    requests.append(transport(
        endpoint, body,
        [this, endpoint, body, ticket, done, session, attempt](QJsonObject response) {
            if (ticket != generation)
                return;
            if (response.contains("error") && response["retryable"].toBool() && attempt < 2) {
                QTimer::singleShot(500 * (1 << attempt), this,
                                   [this, endpoint, body, ticket, done, session, attempt] {
                                       send(endpoint, body, ticket, done, session, attempt + 1);
                                   });
                return;
            }
            done(response);
        },
        session));
}
void SearchEngine::request(QJsonObject body, quint64 ticket, Done done) {
    send("search", body, ticket, done, preferences["useLogin"].toBool());
}
void SearchEngine::suggest(QString input) {
    cancel();
    input = input.trimmed();
    const auto ticket = generation;
    const auto merge = [this, input](QJsonObject response) {
        auto parsed = SearchParser::suggestions(response);
        QJsonArray historic, queries;
        const auto recent = history(input);
        for (auto value : recent)
            historic.append(value);
        for (auto value : parsed["queries"].toArray())
            if (!recent.contains(value.toString()))
                queries.append(value);
        parsed["history"] = historic;
        parsed["queries"] = queries;
        parsed["items"] = SearchParser::visible(parsed["items"].toArray(), preferences);
        if (response.contains("error"))
            parsed["error"] = response["error"];
        return parsed;
    };
    if (input.isEmpty()) {
        emit suggestionsReady(merge({}));
        return;
    }
    setBusy(true);
    send(
        "music/get_search_suggestions", {{"input", input}}, ticket,
        [this, ticket, merge](QJsonObject response) {
            if (ticket != generation)
                return;
            setBusy(false);
            if (response.contains("error"))
                emit failed("Search suggestions could not be loaded.");
            emit suggestionsReady(merge(response));
        },
        false);
}
void SearchEngine::publish(QJsonObject page, bool append) {
    QJsonArray groups;
    for (auto value : page["groups"].toArray()) {
        auto group = value.toObject();
        group["items"] = SearchParser::visible(group["items"].toArray(), preferences);
        if (!group["items"].toArray().isEmpty())
            groups.append(group);
    }
    page["groups"] = groups;
    page["items"] = SearchParser::visible(page["items"].toArray(), preferences);
    page["query"] = query;
    page["filter"] = filter;
    currentPage = page;
    if (cache.size() > 16)
        cache.clear();
    cache[cacheKey()] = page;
    setBusy(false);
    emit pageReady(page, append);
}
void SearchEngine::fallback(QJsonObject page, int stage, quint64 ticket) {
    if (stage == 2) {
        publish(page);
        return;
    }
    const QString type = stage == 0 ? "songs" : "albums";
    request({{"query", query}, {"params", SearchParser::filterParams(type)}}, ticket,
            [this, page, stage, ticket, type](QJsonObject response) mutable {
                if (!response.contains("error")) {
                    auto items = SearchParser::page(response, false)["items"].toArray();
                    QJsonArray preview;
                    for (int i = 0; i < qMin(12, items.size()); ++i)
                        preview.append(items[i]);
                    if (!preview.isEmpty()) {
                        auto groups = page["groups"].toArray();
                        groups.append(
                            QJsonObject{{"title", type == "songs" ? "Songs" : "Albums"}, {"items", preview}});
                        page["groups"] = groups;
                    }
                }
                fallback(page, stage + 1, ticket);
            });
}
void SearchEngine::search(QString input, QString type, bool record) {
    cancel();
    query = input.trimmed();
    filter = type;
    consumedTokens.clear();
    currentPage = {};
    if (query.isEmpty()) {
        publish({});
        return;
    }
    if (filter != "all" && SearchParser::filterParams(filter).isEmpty()) {
        emit failed("Unknown search filter.");
        return;
    }
    if (record)
        remember(query);
    const auto key = cacheKey();
    if (cache.contains(key)) {
        currentPage = cache[key];
        emit pageReady(currentPage, false);
        return;
    }
    setBusy(true);
    const auto ticket = generation;
    QJsonObject body{{"query", query}};
    if (filter != "all")
        body["params"] = SearchParser::filterParams(filter);
    request(body, ticket, [this, ticket](QJsonObject response) {
        if (response.contains("error")) {
            setBusy(false);
            emit failed("Search failed. Try again.");
            return;
        }
        auto page = SearchParser::page(response, filter == "all");
        if (filter == "all" && page["groups"].toArray().size() <= 1)
            fallback(page, 0, ticket);
        else
            publish(page);
    });
}
void SearchEngine::loadMore() {
    const auto token = currentPage["continuation"].toString();
    if (busy || token.isEmpty() || filter == "all")
        return;
    setBusy(true);
    const auto ticket = generation;
    request({{"_continuation", token}}, ticket, [this, token](QJsonObject response) {
        if (response.contains("error")) {
            setBusy(false);
            emit failed("Could not load more results. Try again.");
            return;
        }
        auto next = SearchParser::page(response, false);
        auto items = currentPage["items"].toArray();
        for (auto value : next["items"].toArray())
            items.append(value);
        auto continuation = next["continuation"].toString();
        consumedTokens.insert(token);
        if (consumedTokens.contains(continuation) || next["items"].toArray().isEmpty())
            continuation.clear();
        currentPage["items"] = unique(items);
        currentPage["continuation"] = continuation;
        publish(currentPage, true);
    });
}
QJsonObject SearchEngine::localPage(QString query, QString filter, const QJsonArray &items) {
    QJsonArray result;
    query = query.trimmed();
    if (query.isEmpty())
        return {{"items", result}, {"groups", QJsonArray{}}};
    for (auto value : items) {
        auto item = value.toObject();
        const auto kind = item["kind"].toString();
        if (filter != "all" &&
            !((filter == "songs" && kind == "song") || (filter == "albums" && kind == "album") ||
              (filter == "artists" && kind == "artist") || (filter == "community" && kind == "playlist")))
            continue;
        const auto searchable =
            item["title"].toString() + " " + item["subtitle"].toString() + " " + Track::fromWire(item).artist;
        if (searchable.contains(query, Qt::CaseInsensitive))
            result.append(item);
    }
    result = unique(result);
    QJsonArray groups;
    if (filter == "all") {
        for (auto kind : {"song", "album", "artist", "playlist"}) {
            QJsonArray typed;
            for (auto value : result)
                if (value.toObject()["kind"].toString() == kind)
                    typed.append(value);
            const QString name = QString(kind) == "song"     ? "Songs"
                                 : QString(kind) == "album"  ? "Albums"
                                 : QString(kind) == "artist" ? "Artists"
                                                             : "Playlists";
            if (!typed.isEmpty())
                groups.append(QJsonObject{{"title", name}, {"items", typed}});
        }
    }
    return {{"items", filter == "all" ? QJsonArray{} : result},
            {"groups", groups},
            {"continuation", ""},
            {"query", query},
            {"filter", filter},
            {"local", true}};
}
} // namespace velune
