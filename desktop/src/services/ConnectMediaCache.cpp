#include "ConnectMediaCache.h"
#include "CacheStorage.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QPointer>
#include <cmath>
namespace velune {
namespace {
constexpr qint64 MaxSong = 512ll * 1024 * 1024;
bool save(const QString &path, const QByteArray &data) {
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}
} // namespace
ConnectMediaCache::ConnectMediaCache(Library *l, ConnectClient *c, QObject *parent)
    : QObject(parent), library(l), remote(c), root(l->directory() + "/connect/media") {
    QDir().mkpath(root);
}
ConnectMediaCache::~ConnectMediaCache() {
    cancel();
}
QString ConnectMediaCache::stem(QString song) const {
    return root + '/' +
           QString::fromLatin1(QCryptographicHash::hash(song.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString ConnectMediaCache::path(QString song) const {
    const auto base = stem(song);
    QFile info(base + ".json");
    if (!info.open(QIODevice::ReadOnly))
        return {};
    const auto metadata = QJsonDocument::fromJson(info.read(8192)).object();
    const QFileInfo audio(base + ".audio");
    const auto length = qint64(metadata["length"].toDouble(-1));
    return metadata["id"].toString() == song && length > 0 && length <= MaxSong && audio.isFile() &&
                   audio.isReadable() && audio.size() == length
               ? audio.filePath()
               : QString();
}
QString ConnectMediaCache::cover(QString song) const {
    const auto file = stem(song) + ".cover";
    return QFileInfo::exists(file) ? file : QString();
}
QString ConnectMediaCache::lyrics(QString song) const {
    QFile file(stem(song) + ".lyrics");
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.read(2 * 1024 * 1024)) : QString();
}
void ConnectMediaCache::trim() {
    if (!busy()) CacheStorage::trim(library->directory());
}
void ConnectMediaCache::fetch(const Track &track, std::function<void(QString, QString)> done) {
    cancel();
    const auto providers = remote->mediaProviders();
    if (providers.isEmpty()) {
        done(path(track.id), {});
        return;
    }
    trim();
    auto task = std::make_shared<Fetch>();
    task->track = track;
    task->providers = providers;
    task->done = std::move(done);
    current = task;
    openNext(task);
}
void ConnectMediaCache::openNext(const std::shared_ptr<Fetch> &task) {
    if (current != task)
        return;
    if (task->candidate >= task->providers.size()) {
        if (task->cachedOnly) {
            task->cachedOnly = false;
            task->candidate = 0;
        } else {
            finish(task, "The connected phone could not provide this song.");
            return;
        }
    }
    task->provider = task->providers[task->candidate++];
    const QPointer<ConnectMediaCache> self(this);
    task->request =
        remote->requestMedia(task->provider,
                             {{"operation", "open"},
                              {"song", task->track.id},
                              {"position", 0},
                              {"length", -1},
                              {"cachedOnly", task->cachedOnly}},
                             [self, task](QJsonObject reply) {
                                 if (!self || self->current != task)
                                     return;
                                 task->request.clear();
                                 if (!reply["ok"].toBool()) {
                                     self->openNext(task);
                                     return;
                                 }
                                 task->handle = reply["handle"].toString();
                                 const double length = reply["length"].toDouble(-1);
                                 if (length < -1 || length > MaxSong || std::floor(length) != length) {
                                     self->finish(task, "The phone returned an invalid audio length.");
                                     return;
                                 }
                                 task->expected = qint64(length);
                                 if (task->handle.isEmpty() || task->handle.size() > 256 ||
                                     task->expected == 0 || task->expected < -1 || task->expected > MaxSong) {
                                     self->finish(task, "The phone returned invalid audio information.");
                                     return;
                                 }
                                 task->file =
                                     std::make_unique<QSaveFile>(self->stem(task->track.id) + ".audio");
                                 if (!task->file->open(QIODevice::WriteOnly)) {
                                     self->finish(task, "Could not save audio received from the phone.");
                                     return;
                                 }
                                 self->readNext(task);
                             });
}
void ConnectMediaCache::readNext(const std::shared_ptr<Fetch> &task) {
    if (current != task)
        return;
    const QPointer<ConnectMediaCache> self(this);
    task->request = remote->requestMedia(
        task->provider, {{"operation", "read"}, {"handle", task->handle}}, [self, task](QJsonObject reply) {
            if (!self || self->current != task)
                return;
            task->request.clear();
            const auto encoded = reply["data"].toString().toLatin1();
            const auto decoded =
                QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
            const auto bytes = decoded.decoded;
            const bool eof = reply["eof"].toBool();
            if (!reply["ok"].toBool() || !decoded || bytes.size() > 65536 || (eof && !bytes.isEmpty()) ||
                (!eof && bytes.isEmpty()) || task->received + bytes.size() > MaxSong ||
                (task->expected >= 0 && task->received + bytes.size() > task->expected)) {
                self->finish(task, "Audio transfer from the phone was interrupted or invalid.");
                return;
            }
            if (eof) {
                if (!task->received || (task->expected >= 0 && task->received != task->expected) ||
                    !task->file->commit()) {
                    self->finish(task, "The phone audio file was incomplete.");
                    return;
                }
                const QJsonObject metadata{{"id", task->track.id}, {"length", double(task->received)}};
                if (!save(self->stem(task->track.id) + ".json",
                          QJsonDocument(metadata).toJson(QJsonDocument::Compact))) {
                    self->finish(task, "Could not save the phone audio index.");
                    return;
                }
                self->finish(task);
                return;
            }
            if (task->file->write(bytes) != bytes.size()) {
                self->finish(task, "There is not enough space to receive phone audio.");
                return;
            }
            task->received += bytes.size();
            self->readNext(task);
        });
}
void ConnectMediaCache::close(const std::shared_ptr<Fetch> &task) {
    if (!task->request.isEmpty())
        remote->cancelMedia(task->request);
    if (!task->handle.isEmpty())
        remote->requestMedia(task->provider, {{"operation", "close"}, {"handle", task->handle}},
                             [](QJsonObject) {});
    task->request.clear();
    task->handle.clear();
}
void ConnectMediaCache::finish(const std::shared_ptr<Fetch> &task, QString error) {
    if (current != task)
        return;
    current.reset();
    close(task);
    if (!error.isEmpty() && task->file)
        task->file->cancelWriting();
    auto done = std::move(task->done);
    done(error.isEmpty() ? path(task->track.id) : QString(), error);
}
void ConnectMediaCache::cancel() {
    const auto task = current;
    current.reset();
    if (task) {
        close(task);
        if (task->file)
            task->file->cancelWriting();
    }
}
void ConnectMediaCache::fetchAssets(const Track &track) {
    if (track.id.isEmpty() || assetPending.contains(track.id))
        return;
    auto candidates = remote->mediaProviders(true);
    if (candidates.isEmpty())
        return;
    trim();
    // Audio remains independent of optional artwork/lyrics requests.
    assetPending.insert(track.id);
    auto remaining = std::make_shared<QStringList>(candidates);
    auto request = std::make_shared<std::function<void()>>();
    const QPointer<ConnectMediaCache> self(this);
    const std::weak_ptr<std::function<void()>> weak = request;
    *request = [self, track, remaining, weak] {
        if (!self)
            return;
        QJsonArray fields;
        if (!QFileInfo::exists(self->stem(track.id) + ".lyrics"))
            fields.append("lyrics");
        if (self->cover(track.id).isEmpty())
            fields.append("cover");
        if (fields.isEmpty() || remaining->isEmpty()) {
            self->assetPending.remove(track.id);
            return;
        }
        const auto next = weak.lock();
        self->remote->requestMedia(
            remaining->takeFirst(), {{"operation", "assets"}, {"song", track.id}, {"fields", fields}},
            [self, track, next](QJsonObject reply) {
                if (!self)
                    return;
                const auto assets = reply["assets"].toObject();
                bool changed = false;
                const auto lyrics = assets["lyrics"].toString().toUtf8();
                if (!lyrics.isEmpty() && lyrics.size() <= 2 * 1024 * 1024 &&
                    !QFileInfo::exists(self->stem(track.id) + ".lyrics"))
                    changed |= save(self->stem(track.id) + ".lyrics", lyrics);
                const auto encoded = assets["cover"].toString().toLatin1();
                if (encoded.size() <= 2800000 && self->cover(track.id).isEmpty()) {
                    const auto decoded =
                        QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
                    auto image = decoded.decoded;
                    QBuffer buffer(&image);
                    buffer.open(QIODevice::ReadOnly);
                    QImageReader reader(&buffer);
                    const auto size = reader.size();
                    if (decoded && image.size() <= 2 * 1024 * 1024 && size.isValid() &&
                        size.width() <= 8192 && size.height() <= 8192 && reader.canRead())
                        changed |= save(self->stem(track.id) + ".cover", image);
                }
                if (changed)
                    emit self->assetsChanged(track.id);
                if (next)
                    (*next)();
            });
    };
    (*request)();
}
} // namespace velune
