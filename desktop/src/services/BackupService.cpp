#include "BackupService.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <cmath>
namespace velune {
namespace {
constexpr qint64 MaxBackup = 32ll * 1024 * 1024;
const QStringList Names{"library.json", "catalog.json", "search.json", "last-playback.json", "storage.json"};
QJsonObject defaultFile(const QString &name) {
    if (name == "catalog.json")
        return {{"preferences", QJsonObject{}},
                {"remoteEntries", QJsonArray{}},
                {"bookmarks", QJsonObject{}},
                {"playlistTags", QJsonObject{}}};
    if (name == "search.json")
        return {{"settings", QJsonObject{}}, {"history", QJsonArray{}}};
    if (name == "storage.json")
        return {{"audioMiB", 1024}, {"imageMiB", 256}};
    if (name == "last-playback.json")
        return {{"schema", 1}, {"queue", QJsonArray{}}, {"index", -1}};
    return {};
}
bool write(QString path, QByteArray bytes) {
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.setPermissions(QFile::ReadOwner | QFile::WriteOwner) &&
           f.write(bytes) == bytes.size() && f.commit();
}
bool fail(QString *error, QString text) {
    if (error)
        *error = text;
    return false;
}
QJsonObject read(QString path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > MaxBackup)
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
bool safeFile(const QString &root, const QString &name) {
    return !QFileInfo(root + '/' + name).isSymLink();
}
bool rollback(QString root, QJsonObject journal, QString *error) {
    const auto folder = journal["folder"].toString();
    if (!QRegularExpression("^restore-[a-f0-9-]{36}$").match(folder).hasMatch() ||
        QFileInfo(root + '/' + folder).isSymLink())
        return fail(error, "Invalid restore recovery journal. Original files were not changed.");
    const auto previous = journal["previous"].toObject();
    for (auto name : previous.keys())
        if (!Names.contains(name) || !previous[name].isBool())
            return fail(error, "Invalid restore recovery file list.");
    for (auto name : previous.keys()) {
        if (!safeFile(root, name))
            return fail(error, "Restore target is a symbolic link.");
        if (previous[name].toBool()) {
            QFile old(root + '/' + folder + '/' + name);
            if (QFileInfo(old.fileName()).isSymLink() || !old.open(QIODevice::ReadOnly) ||
                old.size() > MaxBackup || !write(root + '/' + name, old.readAll()))
                return fail(error, "Could not recover the previous data. Keep the recovery folder and retry "
                                   "on the next launch.");
        } else if (QFileInfo::exists(root + '/' + name) && !QFile::remove(root + '/' + name))
            return fail(error, "Could not remove a newly restored settings file during recovery.");
    }
    return true;
}
} // namespace
bool BackupService::validate(const QJsonObject &backup, QString *error) {
    if (backup["format"] != "velune-desktop-backup" || backup["version"].toInt() != 1 ||
        !backup["files"].isObject())
        return fail(error, "This is not a supported Velune desktop backup.");
    const auto files = backup["files"].toObject();
    if (!files.contains("library.json"))
        return fail(error, "The backup does not contain a library.");
    for (auto name : files.keys())
        if (!Names.contains(name) || !files[name].isObject())
            return fail(error, "The backup contains unsupported data.");
    const auto library = files["library.json"].toObject();
    if (library["schema"].toInt() != 1 || !library["likes"].isArray() || !library["playlists"].isArray() ||
        !library["history"].isArray())
        return fail(error, "The backup library is invalid.");
    auto validTrack = [](QJsonValue value) {
        if (!value.isObject())
            return false;
        auto track = value.toObject();
        return track["id"].isString() && !track["id"].toString().isEmpty() &&
               track["id"].toString().size() <= 256 && track["title"].isString() &&
               track["title"].toString().size() <= 10000;
    };
    for (auto value : library["likes"].toArray())
        if (!validTrack(value.toObject()["track"]))
            return fail(error, "Invalid song metadata in backup.");
    for (auto value : library["history"].toArray())
        if (!validTrack(value.toObject()["track"]))
            return fail(error, "Invalid listening history in backup.");
    for (auto value : library["playlists"].toArray()) {
        auto p = value.toObject();
        if (!p["id"].isString() || p["id"].toString().isEmpty() || !p["songs"].isArray())
            return fail(error, "Invalid playlist in backup.");
        for (auto song : p["songs"].toArray())
            if (!validTrack(song))
                return fail(error, "Invalid playlist song in backup.");
    }
    if (files.contains("catalog.json")) {
        auto catalog = files["catalog.json"].toObject();
        if (!catalog["preferences"].isObject() || !catalog["remoteEntries"].isArray() ||
            !catalog["bookmarks"].isObject() || !catalog["playlistTags"].isObject())
            return fail(error, "Invalid catalog settings in backup.");
        const auto prefs = catalog["preferences"].toObject();
        for (auto key : {"pauseListeningHistory", "systemFont", "hidePlayerArtwork", "showLiked",
                         "showOffline", "showTop", "showCached"})
            if (prefs.contains(key) && !prefs[key].isBool())
                return fail(error, "Invalid preference value in backup.");
        if (prefs.contains("defaultPage") &&
            !QStringList{"Home", "Search", "Explore", "Library", "Downloads"}.contains(
                prefs["defaultPage"].toString()))
            return fail(error, "Invalid startup page in backup.");
        if (prefs.contains("crossfadeSeconds") &&
            (!prefs["crossfadeSeconds"].isDouble() || prefs["crossfadeSeconds"].toDouble() < 0 ||
             prefs["crossfadeSeconds"].toDouble() > 12))
            return fail(error, "Invalid crossfade duration in backup.");
    }
    if (files.contains("search.json")) {
        auto search = files["search.json"].toObject();
        if (!search["settings"].isObject() || !search["history"].isArray())
            return fail(error, "Invalid search settings in backup.");
        const auto prefs = search["settings"].toObject();
        for (auto key : {"hideExplicit", "hideVideos", "pauseHistory", "useLogin"})
            if (prefs.contains(key) && !prefs[key].isBool())
                return fail(error, "Invalid content setting in backup.");
        if (prefs.contains("language") && !QRegularExpression("^[a-z]{2,3}(-[A-Za-z]{2,4})?$")
                                               .match(prefs["language"].toString())
                                               .hasMatch())
            return fail(error, "Invalid content language in backup.");
        if (prefs.contains("country") &&
            !QRegularExpression("^[A-Z]{2}$").match(prefs["country"].toString()).hasMatch())
            return fail(error, "Invalid content country in backup.");
        for (auto query : search["history"].toArray())
            if (!query.isString())
                return fail(error, "Invalid search history in backup.");
    }
    if (files.contains("last-playback.json")) {
        const auto state = files["last-playback.json"].toObject();
        if (state["schema"].toInt() != 1 || !state["queue"].isArray() ||
            state["queue"].toArray().size() > 20000)
            return fail(error, "Invalid playback checkpoint in backup.");
        for (auto track : state["queue"].toArray())
            if (!validTrack(track))
                return fail(error, "Invalid playback queue in backup.");
        const auto index = state["index"].toInt(-1);
        const auto count = state["queue"].toArray().size();
        if (index < -1 || (count > 0 && (index < 0 || index >= count)))
            return fail(error, "Invalid playback selection in backup.");
        for (auto key : {"position", "duration"})
            if (state.contains(key) &&
                (!state[key].isDouble() || state[key].toDouble() < 0 || state[key].toDouble() > 9e12))
                return fail(error, "Invalid playback time in backup.");
    }
    if (files.contains("storage.json")) {
        const auto storage = files["storage.json"].toObject();
        if (!storage["audioMiB"].isDouble() || storage["audioMiB"].toInt() < 128 ||
            storage["audioMiB"].toInt() > 16384 || !storage["imageMiB"].isDouble() ||
            storage["imageMiB"].toInt() < 16 || storage["imageMiB"].toInt() > 2048)
            return fail(error, "Invalid cache limits in backup.");
    }
    // Archives have a fixed set of JSON sections, never executable content or file paths to extract.
    return true;
}
bool BackupService::exportTo(QString path) {
    const QFileInfo destination(path);
    const auto appRoot = QFileInfo(root).canonicalFilePath();
    const auto parent = QFileInfo(destination.absolutePath()).canonicalFilePath();
    if (destination.isSymLink() || parent == appRoot || parent.startsWith(appRoot + '/')) {
        emit message("Choose a backup location outside Velune’s app data folder.");
        return false;
    }
    QJsonObject files;
    for (auto name : Names) {
        if (!safeFile(root, name)) {
            emit message("Cannot back up a linked app data file.");
            return false;
        }
        if (!QFileInfo::exists(root + '/' + name)) {
            if (name != "library.json")
                files[name] = defaultFile(name);
            continue;
        }
        const auto object = read(root + '/' + name);
        if (object.isEmpty()) {
            emit message("Could not read an app data file. No backup was written.");
            return false;
        }
        files[name] = object;
    }
    QJsonObject archive{{"format", "velune-desktop-backup"},
                        {"version", 1},
                        {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                        {"files", files}};
    QString error;
    if (!validate(archive, &error)) {
        emit message(error);
        return false;
    }
    auto bytes = QJsonDocument(archive).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxBackup || !write(path, bytes)) {
        emit message("Could not save the backup. Check free space and folder access.");
        return false;
    }
    emit message("Backup saved. Music files and sign-in sessions are not included.");
    return true;
}
bool BackupService::inspect(QString path) {
    candidate = {};
    summary = {};
    auto archive = read(path);
    QString error;
    if (!validate(archive, &error)) {
        emit message(error);
        return false;
    }
    candidate = archive;
    auto library = candidate["files"].toObject()["library.json"].toObject();
    int songs = 0, playlists = 0;
    for (auto value : library["likes"].toArray())
        if (value.toObject()["liked"].toBool())
            ++songs;
    for (auto value : library["playlists"].toArray())
        if (!value.toObject()["removed"].toBool())
            ++playlists;
    summary = {{"createdAt", archive["createdAt"]}, {"likes", songs}, {"playlists", playlists}};
    emit previewReady();
    return true;
}
bool BackupService::stageRestore() {
    QString error;
    if (!validate(candidate, &error)) {
        emit message("Select a valid backup first.");
        return false;
    }
    if (!safeFile(root, ".restore-request.json") ||
        !write(root + "/.restore-request.json", QJsonDocument(candidate).toJson(QJsonDocument::Compact))) {
        emit message("Could not prepare the restore. Your current data is unchanged.");
        return false;
    }
    return true;
}
bool BackupService::applyPending(const QString &directory, QString *error) {
    const auto root = QDir(directory).absolutePath();
    const auto journalPath = root + "/.restore-journal.json";
    if (QFileInfo::exists(journalPath)) {
        if (!safeFile(root, ".restore-journal.json") || !rollback(root, read(journalPath), error))
            return false;
        if (!QFile::remove(journalPath))
            return fail(error, "Could not finish recovery.");
    }
    const auto request = root + "/.restore-request.json";
    if (!QFileInfo::exists(request))
        return true;
    if (!safeFile(root, ".restore-request.json"))
        return fail(error, "Restore request is a symbolic link.");
    auto archive = read(request);
    if (!validate(archive, error))
        return false;
    auto files = archive["files"].toObject();
    for (auto name : Names)
        if (!files.contains(name))
            files[name] = defaultFile(name);
    auto library = files["library.json"].toObject();
    // Connect identity and the local version counter belong to this installation.
    const auto local = read(root + "/library.json");
    library["device"] = local["device"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    library["counter"] = qMax(library["counter"].toDouble(), local["counter"].toDouble()) + 1;
    files["library.json"] = library;
    const auto folder = "restore-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(root + '/' + folder))
        return fail(error, "Could not create the recovery snapshot.");
    QFile::setPermissions(root + '/' + folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QJsonObject previous;
    for (auto name : files.keys()) {
        if (!safeFile(root, name))
            return fail(error, "Cannot restore over a symbolic link.");
        const bool exists = QFileInfo::exists(root + '/' + name);
        previous[name] = exists;
        if (exists) {
            QFile f(root + '/' + name);
            if (!f.open(QIODevice::ReadOnly) || f.size() > MaxBackup ||
                !write(root + '/' + folder + '/' + name, f.readAll()))
                return fail(error, "Could not save the previous data. Restore was not started.");
        }
    }
    const QJsonObject journal{{"folder", folder}, {"previous", previous}};
    if (!write(journalPath, QJsonDocument(journal).toJson()))
        return fail(error, "Could not write the restore recovery journal.");
    bool ok = true;
    for (auto name : files.keys())
        if (!write(root + '/' + name, QJsonDocument(files[name].toObject()).toJson())) {
            ok = false;
            break;
        }
    if (!ok) {
        QString recovery;
        if (rollback(root, journal, &recovery)) {
            QFile::remove(journalPath);
            QFile::remove(request);
            return fail(error, "Restore failed. The previous data has been recovered.");
        }
        return fail(error, recovery);
    }
    if (!QFile::remove(journalPath) || !QFile::remove(request))
        return fail(error,
                    "Restore completed but recovery markers could not be cleared. Check folder permissions.");
    // Keep the pre-restore snapshot as a recoverable copy; never delete user music.
    return true;
}
} // namespace velune
