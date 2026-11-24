#include "DownloadService.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTemporaryFile>
namespace velune {
namespace {
constexpr qint64 MaxSong = 512ll * 1024 * 1024;
constexpr qint64 Chunk = 512 * 1024;
bool saveJson(QString path, QJsonObject object) {
    QSaveFile f(path);
    auto b = QJsonDocument(object).toJson();
    return f.open(QIODevice::WriteOnly) && f.write(b) == b.size() && f.commit();
}
QString stem(Library *l, const QString &id) {
    return l->directory() + "/connect/media/" +
           QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
}
} // namespace
DownloadService::DownloadService(Library *l, QObject *p, QNetworkAccessManager *manager)
    : QObject(p), library(l), resolver(this, {}, {}, manager), transport(manager ? manager : &network) {
    QDir().mkpath(l->directory() + "/downloads");
    QFile config(l->directory() + "/downloads.json");
    if (config.open(QIODevice::ReadOnly))
        folder = QJsonDocument::fromJson(config.read(65536)).object()["folder"].toString();
    connect(&scanTick, &QTimer::timeout, this, &DownloadService::scanStep);
    scanTick.setInterval(0);
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, this,
            [this] { finish(false, "The download timed out. Please retry."); });
    rescanTimer.setSingleShot(true);
    rescanTimer.setInterval(700);
    connect(&rescanTimer, &QTimer::timeout, this, [this] {
        if (active.id.isEmpty())
            scan();
    });
    connect(&watcher, &QFileSystemWatcher::directoryChanged, this, [this] { rescanTimer.start(); });
    connect(&resolver, &AudioResolver::ready, this, [this](QUrl url, QJsonObject headers, qint64 duration) {
        sourceUrl = url;
        sourceHeaders = headers;
        if (active.duration <= 0)
            active.duration = int(duration / 1000);
        stage = "Downloading";
        fetchChunk();
    });
    connect(&resolver, &AudioResolver::error, this, [this](QString error) { finish(false, error); });
    QTimer::singleShot(0, this, &DownloadService::scan);
}
DownloadService::~DownloadService() {
    stopWork();
}
bool DownloadService::contains(const QString &id) const {
    if (!saved.contains(id))
        return false;
    const auto s = saved.value(id);
    QFileInfo f(s.path);
    return f.isFile() && f.isReadable() && f.size() == s.size && s.size > 0;
}
QString DownloadService::cover(const QString &id) const {
    const auto path = library->directory() + "/downloads/" + id + ".jpg";
    return contains(id) && QFileInfo::exists(path) ? path : QString();
}
QJsonObject DownloadService::state() const {
    QJsonArray items, jobs;
    qint64 bytes = 0;
    for (const auto &s : saved)
        if (contains(s.track.id)) {
            auto o = s.track.wire();
            o["downloaded"] = true;
            items.append(o);
            bytes += s.size;
        }
    auto job = [&](const Track &t, QString status) {
        auto o = t.wire();
        o["status"] = status;
        jobs.append(o);
    };
    if (!active.id.isEmpty())
        job(active, stage);
    for (const auto &t : pending)
        job(t, "Queued");
    for (const auto &t : failed)
        job(t, "Failed — retry");
    return {{"folder", folder},
            {"folderUrl", QUrl::fromLocalFile(folder).toString()},
            {"scanning", bool(iterator)},
            {"scanned", scanned},
            {"busy", !active.id.isEmpty()},
            {"items", items},
            {"jobs", jobs},
            {"bytes", double(bytes)},
            {"progress", total > 0 ? double(received) / total : 0.0},
            {"error", errorText},
            {"canChangeFolder", active.id.isEmpty() && pending.isEmpty() && !iterator}};
}
bool DownloadService::setFolder(const QString &path) {
    if (!active.id.isEmpty() || !pending.isEmpty() || iterator) {
        emit message("Wait for downloads and scanning to finish before changing the folder.");
        return false;
    }
    QFileInfo info(path);
    if (!info.isDir() || !info.isReadable()) {
        emit message("Choose an accessible folder.");
        return false;
    }
    const auto chosen = info.canonicalFilePath();
    QTemporaryFile test(QDir(chosen).filePath(".velune-write-XXXXXX"));
    if (!test.open()) {
        emit message("Velune cannot write to this folder. Choose a writable folder.");
        return false;
    }
    if (!saveJson(library->directory() + "/downloads.json", {{"folder", chosen}})) {
        emit message("Could not save the downloads folder.");
        return false;
    }
    QMap<QString, QString> previous;
    for (const auto &s : saved)
        previous[s.track.id] = s.path;
    library->updateDownloadFiles(previous, {});
    saved.clear();
    failed.clear();
    folder = chosen;
    errorText.clear();
    scan();
    return true;
}
void DownloadService::scan() {
    if (iterator || !active.id.isEmpty() || folder.isEmpty())
        return;
    if (!watcher.directories().isEmpty())
        watcher.removePaths(watcher.directories());
    if (!QFileInfo(folder).isDir() || !QFileInfo(folder).isReadable()) {
        errorText = "The downloads folder is unavailable. Reconnect the drive or choose another folder.";
        for (const auto &track : pending)
            failed[track.id] = track;
        pending.clear();
        emit changed();
        return;
    }
    watcher.addPath(folder);
    found.clear();
    knownTracks.clear();
    for (const auto &track : library->tracks())
        knownTracks[track.id] = track;
    scanned = 0;
    iterator =
        std::make_unique<QDirIterator>(folder, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    scanTick.start();
    emit changed();
}
void DownloadService::remember(const Mp3Info &info, const QString &path, QMap<QString, Saved> &into) {
    if (!info.valid || into.contains(info.track.id))
        return;
    auto t = info.track;
    // Keep richer metadata already learned from the catalog.
    const auto known = active.id == t.id ? active : knownTracks.value(t.id);
    if (known.id == t.id) {
        if (t.title == t.id)
            t.title = known.title;
        if (t.artist.isEmpty())
            t.artist = known.artist;
        if (t.album.isEmpty())
            t.album = known.album;
        t.albumId = known.albumId;
        t.thumbnail = known.thumbnail;
        if (t.duration <= 0)
            t.duration = known.duration;
    }
    into[t.id] = {t, path, QFileInfo(path).size()};
    if (!info.cover.isEmpty()) {
        auto image = QImage::fromData(info.cover);
        if (!image.isNull())
            image.scaled(1000, 1000, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                .save(library->directory() + "/downloads/" + t.id + ".jpg", "JPEG", 92);
    }
}
void DownloadService::scanStep() {
    // Yield between files so a large music folder does not block the UI.
    for (int n = 0; n < 8 && iterator && iterator->hasNext(); ++n) {
        const auto path = iterator->next();
        if (QFileInfo(path).suffix().compare("mp3", Qt::CaseInsensitive) != 0)
            continue;
        ++scanned;
        remember(readVeluneMp3(path), path, found);
    }
    if (iterator && iterator->hasNext()) {
        emit changed();
        return;
    }
    scanTick.stop();
    iterator.reset();
    QMap<QString, QString> previous;
    for (const auto &s : saved)
        previous[s.track.id] = s.path;
    saved = found;
    found.clear();
    QList<QPair<Track, QString>> current;
    for (const auto &s : saved)
        current.append({s.track, s.path});
    library->updateDownloadFiles(previous, current);
    errorText.clear();
    emit changed();
    startNext();
}
void DownloadService::enqueue(const QList<Track> &tracks) {
    if (tracks.isEmpty())
        return;
    if (folder.isEmpty()) {
        emit folderRequired();
        return;
    }
    for (const auto &t : tracks) {
        if (!QRegularExpression("^[A-Za-z0-9_-]{11}$").match(t.id).hasMatch() || contains(t.id) ||
            active.id == t.id)
            continue;
        bool queued = false;
        for (const auto &p : pending)
            if (p.id == t.id)
                queued = true;
        if (!queued) {
            pending.append(t);
            failed.remove(t.id);
        }
    }
    emit changed();
    // Re-scan before starting a new batch: catches renamed/copied files, even after reinstall.
    if (active.id.isEmpty())
        scan();
}
QString DownloadService::sourcePath() const {
    return library->directory() + "/downloads/" + active.id + ".source";
}
QString DownloadService::cacheSource(const QString &id) const {
    auto local = library->mediaPath(id);
    if (!local.isEmpty())
        return local;
    const auto base = stem(library, id);
    QFile metadata(base + ".json");
    if (!metadata.open(QIODevice::ReadOnly))
        return {};
    auto o = QJsonDocument::fromJson(metadata.read(8192)).object();
    QFileInfo file(base + ".audio");
    return o["id"] == id && file.isReadable() && file.size() > 0 && file.size() <= MaxSong &&
                   o["length"].toDouble() == file.size()
               ? file.filePath()
               : QString();
}
void DownloadService::startNext() {
    if (!active.id.isEmpty() || iterator || pending.isEmpty())
        return;
    active = pending.takeFirst();
    if (contains(active.id)) {
        active = {};
        QTimer::singleShot(0, this, &DownloadService::startNext);
        emit changed();
        return;
    }
    if (AudioResolver::toolPath("ffmpeg").isEmpty()) {
        finish(false,
               "MP3 downloads need FFmpeg. Install it or add it to Velune's tools folder, then retry.");
        return;
    }
    lock = std::make_unique<QLockFile>(QDir(folder).filePath(".velune-downloads.lock"));
    lock->setStaleLockTime(30000);
    if (!lock->tryLock()) {
        finish(false, "Another Velune instance is using this downloads folder.");
        return;
    }
    work = std::make_unique<QTemporaryDir>(QDir(folder).filePath(".velune-work-XXXXXX"));
    if (!work->isValid()) {
        finish(false, "Cannot write to the downloads folder. Check the drive and free space.");
        return;
    }
    received = 0;
    total = -1;
    errorText.clear();
    stage = "Preparing";
    emit changed();
    timeout.start(15 * 60 * 1000);
    // Only a committed complete source is reused after a conversion failure/restart.
    if (QFileInfo(sourcePath()).size() > 0) {
        convert(sourcePath());
        return;
    }
    const auto cached = cacheSource(active.id);
    if (!cached.isEmpty()) {
        convert(cached);
        return;
    }
    sourceFile = std::make_unique<QSaveFile>(sourcePath());
    if (!sourceFile->open(QIODevice::WriteOnly)) {
        finish(false, "Could not create the temporary download. Check free space.");
        return;
    }
    resolver.resolve(active.id, cookies ? cookies() : QByteArray());
}
void DownloadService::fetchChunk() {
    if (sourceUrl.scheme() != "https" || sourceUrl.port(443) != 443 || !sourceUrl.userInfo().isEmpty() ||
        !(sourceUrl.host() == "googlevideo.com" || sourceUrl.host().endsWith(".googlevideo.com"))) {
        finish(false, "The audio server returned an invalid URL.");
        return;
    }
    QNetworkRequest request(sourceUrl);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    for (auto name : {"User-Agent", "Referer", "Origin"})
        if (sourceHeaders.contains(name))
            request.setRawHeader(name, sourceHeaders[name].toString().toUtf8());
    request.setRawHeader("Accept-Encoding", "identity");
    request.setRawHeader("Range", "bytes=" + QByteArray::number(received) + '-' +
                                      QByteArray::number(qMin(MaxSong - 1, received + Chunk - 1)));
    auto r = transport->get(request);
    reply = r;
    connect(r, &QNetworkReply::readyRead, this, [r] {
        if (r->bytesAvailable() > Chunk)
            r->abort();
    });
    connect(r, &QNetworkReply::finished, this, [this, r] {
        reply = nullptr;
        r->deleteLater();
        auto bytes = r->readAll();
        auto m = QRegularExpression("^bytes ([0-9]+)-([0-9]+)/([0-9]+)$")
                     .match(QString::fromLatin1(r->rawHeader("Content-Range")));
        auto length = m.captured(3).toLongLong(), end = m.captured(2).toLongLong();
        if (r->error() != QNetworkReply::NoError ||
            r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 206 || !m.hasMatch() ||
            m.captured(1).toLongLong() != received || bytes.isEmpty() || bytes.size() > Chunk ||
            end + 1 - received != bytes.size() || length <= 0 || length > MaxSong || end >= length ||
            (total >= 0 && total != length) || sourceFile->write(bytes) != bytes.size()) {
            finish(false, "Download incomplete. Check your connection and free space, then retry.");
            return;
        }
        total = length;
        received += bytes.size();
        emit changed();
        if (received == total) {
            if (!sourceFile->commit()) {
                finish(false, "Could not save the completed audio download.");
                return;
            }
            sourceFile.reset();
            convert(sourcePath());
        } else
            fetchChunk();
    });
}
void DownloadService::convert(const QString &input) {
    stage = "Converting to MP3";
    emit changed();
    fetchCover(input);
}
void DownloadService::fetchCover(const QString &input) {
    const auto local = stem(library, active.id) + ".cover";
    auto image = QImage(local);
    if (!image.isNull() && image.save(work->filePath("cover.jpg"), "JPEG", 92)) {
        runConverter(input, true);
        return;
    }
    QUrl url(active.thumbnail);
    const auto host = url.host().toLower();
    if (url.scheme() != "https" || url.port(443) != 443 || !url.userInfo().isEmpty() ||
        !(host == "i.ytimg.com" || host == "yt3.ggpht.com" || host.endsWith(".googleusercontent.com") ||
          host.endsWith(".ggpht.com"))) {
        runConverter(input, false);
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto r = transport->get(request);
    reply = r;
    connect(r, &QNetworkReply::readyRead, this, [r] {
        if (r->bytesAvailable() > 8 * 1024 * 1024)
            r->abort();
    });
    connect(r, &QNetworkReply::finished, this, [this, r, input] {
        reply = nullptr;
        r->deleteLater();
        auto image = r->error() == QNetworkReply::NoError ? QImage::fromData(r->readAll()) : QImage();
        bool cover =
            !image.isNull() && image.scaled(1000, 1000, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                   .save(work->filePath("cover.jpg"), "JPEG", 92);
        runConverter(input, cover);
    });
}
void DownloadService::runConverter(const QString &input, bool withCover) {
    auto process = new QProcess(this);
    converter = process;
    QStringList args{"-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", input};
    if (withCover)
        args << "-i" << work->filePath("cover.jpg") << "-map" << "0:a:0" << "-map" << "1:v:0" << "-c:v"
             << "copy" << "-metadata:s:v" << "title=Album cover" << "-metadata:s:v"
             << "comment=Cover (front)";
    else
        args << "-vn" << "-map" << "0:a:0";
    args << "-map_metadata" << "-1" << "-c:a" << "libmp3lame" << "-q:a" << "2" << "-id3v2_version" << "3"
         << "-metadata" << "title=" + active.title << "-metadata" << "artist=" + active.artist << "-metadata"
         << "album=" + active.album << "-metadata"
         << "TLEN=" + QString::number(qint64(active.duration) * 1000) << "-f" << "mp3"
         << work->filePath("track.part");
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            finish(false, "FFmpeg could not start. Check the installed download tools.");
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, input, withCover](int code, QProcess::ExitStatus status) {
                converter = nullptr;
                process->deleteLater();
                if (code != 0 || status != QProcess::NormalExit) {
                    if (withCover) {
                        runConverter(input, false);
                        return;
                    }
                    finish(false, "MP3 conversion failed. The complete source is kept for retry.");
                    return;
                }
                const auto part = work->filePath("track.part");
                if (!writeVeluneId(part, active.id)) {
                    finish(false, "Could not write the song identity to the MP3.");
                    return;
                }
                const auto info = readVeluneMp3(part);
                if (!info.valid || info.track.id != active.id) {
                    finish(false, "The converted MP3 could not be verified.");
                    return;
                }
                auto destination = QDir(folder).filePath(downloadFileName(active));
                if (QFileInfo::exists(destination)) {
                    const auto existing = readVeluneMp3(destination);
                    if (existing.valid && existing.track.id == active.id) {
                        remember(existing, destination, saved);
                        finish(true);
                        return;
                    }
                    // Never overwrite an unrelated or damaged user file.
                    const auto base = destination.left(destination.size() - 4);
                    int suffix = 2;
                    while (QFileInfo::exists(destination))
                        destination = base + " (" + QString::number(suffix++) + ").mp3";
                }
                if (!QFile::rename(part, destination)) {
                    finish(false, "Could not publish the MP3. Check folder access and free space.");
                    return;
                }
                remember(info, destination, saved);
                finish(true);
            });
    process->start(AudioResolver::toolPath("ffmpeg"), args);
}
void DownloadService::stopWork() {
    timeout.stop();
    resolver.cancel();
    if (reply) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
        reply = nullptr;
    }
    if (converter) {
        converter->disconnect(this);
        converter->kill();
        converter->waitForFinished(3000);
        delete converter;
        converter = nullptr;
    }
    sourceFile.reset();
    work.reset();
    lock.reset();
}
void DownloadService::finish(bool success, QString error) {
    const auto track = active;
    if (success && saved.contains(track.id)) {
        library->registerMedia(saved[track.id].track, saved[track.id].path, false);
        QFile::remove(sourcePath());
        failed.remove(track.id);
    } else if (!error.isEmpty()) {
        failed[track.id] = track;
        errorText = error;
        emit message(error);
    }
    stopWork();
    active = {};
    stage.clear();
    emit changed();
    QTimer::singleShot(0, this, &DownloadService::startNext);
}
void DownloadService::cancel(const QString &id) {
    for (int i = pending.size() - 1; i >= 0; --i)
        if (pending[i].id == id)
            pending.removeAt(i);
    failed.remove(id);
    if (active.id == id)
        finish(false);
    emit changed();
}
} // namespace velune
