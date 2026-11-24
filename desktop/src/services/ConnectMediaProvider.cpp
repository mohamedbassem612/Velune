#include "ConnectMediaProvider.h"
#include "CacheStorage.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>
namespace velune {
static constexpr qint64 MaxSong = 512ll * 1024 * 1024;
ConnectMediaProvider::ConnectMediaProvider(Library *l, QObject *p) : QObject(p), library(l) {
    QDir().mkpath(l->directory() + "/connect/media");
}
ConnectMediaProvider::~ConnectMediaProvider() {
    for (auto f : fills) {
        if (f->reply) {
            f->reply->disconnect(this);
            f->reply->abort();
        }
        if (f->resolver)
            f->resolver->cancel();
    }
}
QString ConnectMediaProvider::stem(QString song) const {
    return library->directory() + "/connect/media/" +
           QString::fromLatin1(QCryptographicHash::hash(song.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString ConnectMediaProvider::cached(QString song) const {
    auto local = library->mediaPath(song);
    if (QFileInfo(local).isReadable() && QFileInfo(local).isFile())
        return local;
    QFile meta(stem(song) + ".json");
    if (!meta.open(QIODevice::ReadOnly))
        return {};
    auto obj = QJsonDocument::fromJson(meta.read(8192)).object();
    QFileInfo file(stem(song) + ".audio");
    return obj["id"] == song && obj["length"].toDouble() == file.size() && file.size() > 0 &&
                   file.size() <= MaxSong && file.isReadable()
               ? file.filePath()
               : QString();
}
void ConnectMediaProvider::trim() {
    if (!busy()) CacheStorage::trim(library->directory());
}
void ConnectMediaProvider::complete(QString song, std::function<void(QString)> done) {
    if (auto path = cached(song); !path.isEmpty()) {
        done(path);
        return;
    }
    if (fills.contains(song)) {
        fills[song]->done.append(std::move(done));
        return;
    }
    if (fills.size() >= 4 || !QRegularExpression("^[A-Za-z0-9_-]{1,256}$").match(song).hasMatch()) {
        done({});
        return;
    }
    trim();
    auto task = std::make_shared<Fill>();
    task->song = song;
    task->done.append(std::move(done));
    task->file = std::make_unique<QSaveFile>(stem(song) + ".audio");
    if (!task->file->open(QIODevice::WriteOnly)) {
        task->done.first()({});
        return;
    }
    task->file->setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    fills[song] = task;
    task->resolver = new AudioResolver(this);
    connect(task->resolver, &AudioResolver::ready, this, [this, task](QUrl url, QJsonObject headers, qint64) {
        task->url = url;
        task->headers = headers;
        chunk(task);
    });
    connect(task->resolver, &AudioResolver::error, this, [this, task](QString) { finish(task, false); });
    task->resolver->resolve(song, cookies ? cookies() : QByteArray());
    QTimer::singleShot(115000, this, [this, task] {
        if (fills.value(task->song) == task)
            finish(task, false);
    });
}
void ConnectMediaProvider::chunk(std::shared_ptr<Fill> task) {
    auto url = task->url;
    if (url.scheme() != "https" || url.port(443) != 443 || !url.userInfo().isEmpty() ||
        !(url.host() == "googlevideo.com" || url.host().endsWith(".googlevideo.com"))) {
        finish(task, false);
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // No account credentials may reach the audio CDN.
    for (auto name : {"User-Agent", "Referer", "Origin"})
        if (task->headers.contains(name))
            request.setRawHeader(name, task->headers[name].toString().toUtf8());
    request.setRawHeader("Accept-Encoding", "identity");
    request.setRawHeader("Range", "bytes=" + QByteArray::number(task->received) + '-' +
                                      QByteArray::number(qMin(MaxSong - 1, task->received + 512 * 1024 - 1)));
    auto reply = network.get(request);
    task->reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 512 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, task, reply] {
        reply->deleteLater();
        if (fills.value(task->song) != task)
            return;
        task->reply = nullptr;
        auto bytes = reply->readAll();
        auto match = QRegularExpression("^bytes ([0-9]+)-([0-9]+)/([0-9]+)$")
                         .match(QString::fromLatin1(reply->rawHeader("Content-Range")));
        auto total = match.captured(3).toLongLong();
        auto end = match.captured(2).toLongLong();
        if (reply->error() != QNetworkReply::NoError ||
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 206 || !match.hasMatch() ||
            match.captured(1).toLongLong() != task->received || bytes.isEmpty() ||
            bytes.size() > 512 * 1024 || end + 1 - task->received != bytes.size() || total <= 0 ||
            total > MaxSong || end >= total || (task->total >= 0 && task->total != total) ||
            task->file->write(bytes) != bytes.size()) {
            finish(task, false);
            return;
        }
        task->total = total;
        task->received += bytes.size();
        if (task->received == total)
            finish(task, true);
        else
            chunk(task);
    });
}
void ConnectMediaProvider::finish(std::shared_ptr<Fill> task, bool ok) {
    if (fills.value(task->song) != task)
        return;
    fills.remove(task->song);
    if (task->reply) {
        task->reply->disconnect(this);
        task->reply->abort();
        task->reply->deleteLater();
    }
    task->resolver->disconnect(this);
    task->resolver->cancel();
    task->resolver->deleteLater();
    if (ok)
        ok = task->file->commit();
    else
        task->file->cancelWriting();
    if (ok) {
        QSaveFile meta(stem(task->song) + ".json");
        auto bytes = QJsonDocument(QJsonObject{{"id", task->song}, {"length", double(task->received)}})
                         .toJson(QJsonDocument::Compact);
        ok = meta.open(QIODevice::WriteOnly) && meta.write(bytes) == bytes.size() && meta.commit();
    }
    auto path = ok ? cached(task->song) : QString();
    for (auto done : task->done)
        done(path);
    trim();
}
void ConnectMediaProvider::disconnectPeer(QString peer) {
    for (auto it = readers.begin(); it != readers.end();)
        if (it->peer == peer)
            it = readers.erase(it);
        else
            ++it;
    for (auto key : pending.values())
        if (key.startsWith(peer + "/"))
            pending.remove(key);
}
void ConnectMediaProvider::serve(QString peer, QJsonObject message, Reply send) {
    auto id = message["requestId"].toString();
    if (id.isEmpty() || id.size() > 128)
        return;
    auto key = peer + "/" + id;
    if (message["type"] == "mediaCancel") {
        pending.remove(key);
        for (auto it = readers.begin(); it != readers.end();)
            if (it->peer == peer && it->request == id)
                it = readers.erase(it);
            else
                ++it;
        return;
    }
    QJsonObject result{{"type", "mediaReply"}, {"requestId", id}, {"ok", false}};
    auto op = message["operation"].toString();
    if (op == "open") {
        auto song = message["song"].toString();
        auto position = message["position"].toDouble(-1), length = message["length"].toDouble(-1);
        if (song.isEmpty() || song.size() > 256 || position < 0 || position > 9e15 ||
            std::floor(position) != position ||
            !(length == -1 || (length > 0 && length <= 9e15 && std::floor(length) == length)) ||
            readers.size() >= 32 || pending.size() >= 32) {
            send(result);
            return;
        }
        int count = 0;
        for (auto reader : readers)
            if (reader.peer == peer)
                ++count;
        if (count >= 4) {
            send(result);
            return;
        }
        if (pending.contains(key)) {
            send(result);
            return;
        }
        pending.insert(key);
        auto open = [this, peer, id, key, position, length, result, send](QString path) mutable {
            if (!pending.remove(key))
                return;
            int count = 0;
            for (auto reader : readers)
                if (reader.peer == peer)
                    ++count;
            auto file = std::make_shared<QFile>(path);
            if (count < 4 && readers.size() < 32 && !path.isEmpty() && file->open(QIODevice::ReadOnly) &&
                position <= file->size() && file->seek(qint64(position))) {
                auto handle = QUuid::createUuid().toString(QUuid::WithoutBraces);
                auto remaining = length < 0 ? file->size() - qint64(position)
                                            : qMin(qint64(length), file->size() - qint64(position));
                readers[handle] = {peer, id, file, remaining};
                result["ok"] = true;
                result["handle"] = handle;
                result["length"] = double(remaining);
            }
            send(result);
        };
        auto path = cached(song);
        if (!path.isEmpty() || message["cachedOnly"].toBool())
            open(path);
        else
            complete(song, open);
    } else if (op == "read") {
        auto it = readers.find(message["handle"].toString());
        if (it != readers.end() && it->peer == peer) {
            auto bytes = it->file->read(qMin<qint64>(65536, it->remaining));
            if (it->file->error() == QFile::NoError) {
                it->remaining -= bytes.size();
                result["ok"] = true;
                result["data"] = QString::fromLatin1(bytes.toBase64());
                result["eof"] = bytes.isEmpty();
            }
        }
        send(result);
    } else if (op == "close") {
        auto it = readers.find(message["handle"].toString());
        if (it != readers.end() && it->peer == peer)
            readers.erase(it);
        result["ok"] = true;
        send(result);
    } else if (op == "assets") {
        auto song = message["song"].toString();
        QJsonObject assets;
        if (!song.isEmpty() && song.size() <= 256) {
            auto fields = message["fields"].toArray();
            if (fields.contains("lyrics")) {
                QFile file(stem(song) + ".lyrics");
                if (file.size() <= 2 * 1024 * 1024 && file.open(QIODevice::ReadOnly))
                    assets["lyrics"] = QString::fromUtf8(file.readAll());
            }
            if (fields.contains("cover")) {
                QFile file(stem(song) + ".cover");
                if (file.size() <= 2 * 1024 * 1024 && file.open(QIODevice::ReadOnly))
                    assets["cover"] = QString::fromLatin1(file.readAll().toBase64());
            }
            if (fields.contains("metadata"))
                for (auto track : library->tracks())
                    if (track.id == song) {
                        assets["metadata"] = track.wire();
                        break;
                    }
        }
        result["ok"] = true;
        result["assets"] = assets;
        send(result);
    } else
        send(result);
}
} // namespace velune
