#include "ConnectSession.h"
#include "MusicService.h"
#include <QDateTime>
#include <QFileInfo>
#include <QPointer>
#include <QRandomGenerator>
namespace velune {
void MusicService::setupConnect() {
    prepareDeadline.setSingleShot(true);
    connect(&prepareDeadline, &QTimer::timeout, this, [this] {
        if (!preparedTransfer.isEmpty())
            connectFailed("The playback destination could not become ready in time.");
    });
    connect(remote, &ConnectClient::prepareRequested, this, &MusicService::prepareConnect);
    connect(remote, &ConnectClient::abortRequested, this, &MusicService::abortConnect);
    connect(remote, &ConnectClient::commandRequested, this, &MusicService::executeConnectCommand);
    connect(remote, &ConnectClient::releaseRequested, this, [this](QString transaction) {
        if (!connectRenderer || !remote->rendersLocally())
            return;
        auto snapshot = connectSnapshot();
        releasingConnect = true;
        wantsPlayback = false;
        if (engine->source().isEmpty()) {
            remote->released(transaction, snapshot);
            return;
        }
        const auto ticket = playbackGeneration;
        auto connection = std::make_shared<QMetaObject::Connection>();
        auto acknowledged = std::make_shared<bool>(false);
        *connection =
            connect(engine.get(), &IPlaybackEngine::pauseConfirmed, this,
                    [this, transaction, snapshot, ticket, connection, acknowledged](PlaybackRequest id) {
                        if (id != ticket)
                            return;
                        *acknowledged = true;
                        disconnect(*connection);
                        if (playbackGeneration == ticket && releasingConnect)
                            remote->released(transaction, snapshot);
                    });
        engine->release();
        QTimer::singleShot(5000, this, [this, connection, acknowledged] {
            disconnect(*connection);
            if (!*acknowledged)
                emit message("Audio release was not confirmed. The transfer stays paused.");
        });
    });
    connect(this, &MusicService::playbackChanged, this, [this](QJsonObject snapshot) {
        if (!remote->hasTarget() || (connectRenderer && preparedTransfer.isEmpty() && !releasingConnect))
            remote->rememberLocalPlayback(snapshot);
    });
    connect(&connectTicker, &QTimer::timeout, this, [this] {
        if (remote->hasTarget() && (!connectRenderer || !preparedTransfer.isEmpty() || releasingConnect ||
                                    !remote->rendersLocally()))
            return;
        auto timer = current["sleepTimer"].toObject();
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (!timer["id"].toString().isEmpty() && timer["remainingMs"].toDouble(-1) >= 0) {
            const auto remaining = qMax(qint64(0), qint64(timer["remainingMs"].toDouble()) -
                                                       qMax(qint64(0), now - sleepUpdatedAt));
            timer["remainingMs"] = double(remaining);
            current["sleepTimer"] = timer;
            if (!remaining) {
                current["sleepTimer"] = QJsonObject{};
                if (remote->hasTarget())
                    executeConnectCommand({{"name", "pause"}});
                else
                    action("pause");
            }
        }
        sleepUpdatedAt = now;
        remote->publishRendererState(connectSnapshot());
    });
    connectTicker.setInterval(1000);
    connectTicker.start();
    connect(peerCache, &ConnectMediaCache::assetsChanged, this, [this](QString song) {
        if (current["song"].toObject()["id"].toString() == song)
            emit playbackChanged(current);
    });
}
QJsonObject MusicService::connectSnapshot() const {
    auto s = current;
    if (connectRenderer && !engine->source().isEmpty()) {
        s["position"] = double(qMax(qint64(0), engine->position()));
        if (engine->duration() > 0)
            s["duration"] = double(engine->duration());
    }
    s["playing"] = wantsPlayback;
    s["deviceVolume"] = QJsonObject{{"level", qRound(engine->volume() * 100)},
                                    {"min", 0},
                                    {"max", 100},
                                    {"muted", engine->volume() <= 0},
                                    {"fixed", false}};
    normalizeSession(s);
    return s;
}
void MusicService::applyConnectState(QJsonObject s) {
    const bool local = s["rendererId"].toString() == remote->identity();
    const bool takeover = !connectRenderer || renderedSession != s["sessionId"].toString() ||
                          renderedEpoch != qint64(s["epoch"].toDouble());
    if (!local) {
        if (connectRenderer && preparedTransfer.isEmpty()) {
            cancelPlayback();
            connectRenderer = false;
        }
        if (preparedTransfer.isEmpty())
            current = s;
        releasingConnect = false;
        renderedSession.clear();
        renderedEpoch = -1;
        emit playbackChanged(s);
        return;
    }
    if (takeover) {
        const bool adopt = !preparedTransfer.isEmpty() && connectLoaded && current["queue"] == s["queue"] &&
                           current["index"] == s["index"];
        preparedTransfer.clear();
        prepareDeadline.stop();
        releasingConnect = false;
        renderedSession = s["sessionId"].toString();
        renderedEpoch = qint64(s["epoch"].toDouble());
        sleepUpdatedAt = QDateTime::currentMSecsSinceEpoch();
        if (adopt) {
            current = s;
            wantsPlayback = s["playing"].toBool();
            engine->seek(qint64(s["position"].toDouble()));
            engine->setSpeed(float(s["speed"].toDouble(1)));
            if (wantsPlayback)
                engine->play();
            else
                engine->pause();
            emit playbackChanged(connectSnapshot());
        } else
            startConnectTrack(s);
    } else {
        // Coordinator echoes acknowledge our snapshot, without seeking our own player.
        for (const auto &key : {"sessionId", "epoch", "revision", "queueRevision", "rendererId"})
            current[key] = s[key];
        if (!s["playing"].toBool() && releasingConnect) {
            releasingConnect = false;
            wantsPlayback = false;
            engine->pause();
        }
        emit playbackChanged(connectSnapshot());
    }
}
void MusicService::prepareConnect(QString transaction, QJsonObject s) {
    if (!preparedTransfer.isEmpty() || !normalizeSession(s) || s["index"].toInt(-1) < 0 ||
        !engine->available()) {
        remote->prepared(transaction, false);
        return;
    }
    preparedBackup = current;
    preparedSnapshot = s;
    preparedTransfer = transaction;
    releasingConnect = false;
    prepareDeadline.start(remote->hosting() ? 145000 : 24000);
    startConnectTrack(s);
}
void MusicService::abortConnect(QString transaction) {
    if (transaction != preparedTransfer)
        return;
    preparedTransfer.clear();
    prepareDeadline.stop();
    cancelPlayback();
    connectRenderer = false;
    releasingConnect = false;
    current = remote->hasTarget() ? remote->playback() : preparedBackup;
    if (!remote->hasTarget())
        current["playing"] = false;
    emit playbackChanged(current);
}
void MusicService::connectMediaReady() {
    if (preparedTransfer.isEmpty() || !connectLoaded ||
        (preparedSnapshot["position"].toDouble() > 0 && !engine->seekable()) ||
        engine->property("connectReadyTransfer").toString() == preparedTransfer)
        return;
    engine->setProperty("connectReadyTransfer", preparedTransfer);
    prepareDeadline.start(30000);
    remote->prepared(preparedTransfer, true);
}
void MusicService::connectFailed(QString error) {
    const auto transaction = preparedTransfer;
    preparedTransfer.clear();
    prepareDeadline.stop();
    cancelPlayback();
    current["playing"] = false;
    if (!transaction.isEmpty()) {
        connectRenderer = false;
        remote->prepared(transaction, false);
    } else if (remote->rendersLocally())
        remote->publishRendererState(connectSnapshot());
    emit playbackChanged(current);
    emit message(error);
}
void MusicService::startConnectTrack(QJsonObject s) {
    cancelPlayback();
    retriedStream = false;
    connectRenderer = true;
    releasingConnect = false;
    current = s;
    queue.clear();
    for (auto item : s["queue"].toArray())
        queue.append(Track::fromWire(item.toObject()));
    library->rememberTracks(queue);
    shuffled = s["shuffle"].toBool();
    repeatMode = s["repeat"].toInt();
    const int index = s["index"].toInt(-1);
    wantsPlayback = s["playing"].toBool() && preparedTransfer.isEmpty();
    pendingSeek = qMax(qint64(0), qint64(s["position"].toDouble()));
    current["playing"] = false;
    if (index < 0 || index >= queue.size()) {
        wantsPlayback = false;
        emit playbackChanged(current);
        return;
    }
    const auto track = queue[index];
    const auto generation = playbackGeneration;
    trace.begin(generation, engine->backendName(), "connect");
    bindSourceCallbacks(generation);
    emit playbackChanged(current);
    if (!engine->available()) {
        connectFailed("Local audio playback is unavailable on this computer.");
        return;
    }
    peerCache->fetchAssets(track);
    const QPointer<MusicService> self(this);
    const auto load = [self, generation](QString file, QString error) {
        if (!self || !self->active(generation))
            return;
        if (!error.isEmpty()) {
            self->connectFailed(error);
            return;
        }
        if (!file.isEmpty()) {
            const auto position = self->pendingSeek;
            self->engine->load(QUrl::fromLocalFile(file), generation);
            if (!self->active(generation))
                return;
            self->engine->setSpeed(float(self->current["speed"].toDouble(1)));
            self->engine->seek(qMax(qint64(0), position));
            self->pendingSeek = -1;
            if (self->wantsPlayback)
                self->engine->play();
        } else {
            const auto &track = self->queue[self->current["index"].toInt()];
            self->resolver->resolve(track.id,
                                    self->authenticated ? self->auth.playbackCookies() : QByteArray());
        }
    };
    auto path = library->mediaPath(track.id);
    if (!QFileInfo(path).isReadable())
        path = peerCache->path(track.id);
    if (!path.isEmpty())
        load(path, {});
    else if (!remote->mediaProviders().isEmpty()) {
        emit message("Receiving audio from the connected device…");
        peerCache->fetch(track, load);
    } else
        load({}, {});
}
void MusicService::executeConnectCommand(QJsonObject c) {
    if (!connectRenderer || !remote->rendersLocally() || !preparedTransfer.isEmpty() || releasingConnect)
        return;
    const auto name = c["name"].toString();
    if (c.contains("position") && (c["position"].toDouble() < 0 || c["position"].toDouble() > 9e15))
        return;
    auto s = connectSnapshot();
    auto items = s["queue"].toArray();
    int index = s["index"].toInt(-1);
    bool reload = false;
    if (name == "pause") {
        wantsPlayback = false;
        engine->pause();
        current["playing"] = false;
    } else if (name == "resume") {
        wantsPlayback = true;
        if (!engine->source().isEmpty() && endedGeneration != playbackGeneration)
            engine->resume();
        else if (resolver->busy() || peerCache->busy()) {
            // The in-flight source will observe the new play intent when it arrives.
        } else {
            s["playing"] = true;
            if (endedGeneration == playbackGeneration)
                s["position"] = 0;
            startConnectTrack(s);
        }
    } else if (name == "stop") {
        cancelPlayback();
        current["playing"] = false;
        current["position"] = 0;
    } else if (name == "clear") {
        s["queue"] = QJsonArray{};
        s["order"] = QJsonArray{};
        s["index"] = -1;
        s["playing"] = false;
        reload = true;
    } else if (name == "next" || name == "previous") {
        if (name == "previous" && s["position"].toDouble() > 3000) {
            s["position"] = 0;
            s["playing"] = true;
            startConnectTrack(s);
            remote->publishRendererState(connectSnapshot());
            return;
        }
        auto order = s["order"].toArray();
        int at = index;
        if (shuffled) {
            at = -1;
            for (int i = 0; i < order.size(); ++i)
                if (order[i].toInt() == index)
                    at = i;
        }
        at += name == "next" ? 1 : -1;
        if (at >= items.size() && repeatMode == 2)
            at = 0;
        if (at < 0 && repeatMode == 2)
            at = items.size() - 1;
        if (at < 0 || at >= items.size()) {
            wantsPlayback = false;
            engine->pause();
            current["playing"] = false;
        } else {
            s["index"] = shuffled ? order[at].toInt() : at;
            s["position"] = 0;
            s["playing"] = true;
            reload = true;
        }
    } else if (name == "seek") {
        const auto position = qMax(0.0, c["position"].toDouble());
        const int target = c["index"].toInt(index);
        if (target >= 0 && target < items.size()) {
            if (target != index) {
                s["index"] = target;
                s["position"] = position;
                reload = true;
            } else {
                current["position"] = position;
                if (!engine->source().isEmpty())
                    engine->seek(qint64(position));
                else
                    pendingSeek = qint64(position);
            }
        }
    } else if (name == "replace") {
        s["queue"] = c["queue"];
        s.remove("order");
        s["shuffle"] = false;
        s["index"] = c["index"].toInt();
        s["position"] = c["position"].toDouble();
        s["playing"] = c["playing"].toBool(true);
        s["queueTitle"] = c["queueTitle"];
        reload = true;
    } else if (name == "deviceVolume") {
        float volume = engine->volume();
        const auto op = c["operation"].toString();
        if (op == "set")
            volume = c["value"].toInt() / 100.f;
        else if (op == "adjust")
            volume += qBound(-1, c["value"].toInt(), 1) * .05f;
        else if (op == "mute") {
            if (c["muted"].toBool()) {
                engine->setProperty("unmutedVolume", volume);
                volume = 0;
            } else
                volume = engine->property("unmutedVolume").isValid()
                             ? engine->property("unmutedVolume").toFloat()
                             : .75f;
        }
        engine->setVolume(qBound(0.f, volume, 1.f));
    } else if (name == "repeat") {
        repeatMode = qBound(0, c["mode"].toInt(), 2);
        current["repeat"] = repeatMode;
    } else if (name == "shuffle") {
        shuffled = c["enabled"].toBool();
        current["shuffle"] = shuffled;
        QJsonArray order;
        QList<int> permutation;
        for (int i = 0; i < items.size(); ++i)
            if (!shuffled || i != index)
                permutation.append(i);
        if (shuffled)
            for (int i = int(permutation.size()) - 1; i > 0; --i)
                permutation.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
        if (shuffled && index >= 0)
            order.append(index);
        for (auto i : permutation)
            order.append(i);
        current["order"] = order;
    } else if (name == "order" || name == "shuffleOrder") {
        auto candidate = current;
        candidate["order"] = c["order"];
        if (normalizeSession(candidate))
            current["order"] = candidate["order"];
    } else if (name == "speed") {
        const float speed = float(qBound(.25, c["speed"].toDouble(1), 4.0));
        engine->setSpeed(speed);
        current["speed"] = speed;
    } else if (name == "sleepTimer") {
        auto timer = c["timer"].toObject();
        if (timer["id"].toString().size() <= 128) {
            timer["remainingMs"] = qBound(-1.0, timer["remainingMs"].toDouble(-1), 604800000.0);
            current["sleepTimer"] = timer;
            sleepUpdatedAt = QDateTime::currentMSecsSinceEpoch();
        }
    } else {
        // Preserve the current queue entry while editing positions, including duplicates.
        QList<QPair<QJsonValue, bool>> entries;
        for (int i = 0; i < items.size(); ++i)
            entries.append({items[i], i == index});
        if (name == "append" || name == "playNext" || name == "insert") {
            int at = name == "append"     ? int(entries.size())
                     : name == "playNext" ? qMax(0, index + 1)
                                          : qBound(0, c["index"].toInt(), int(entries.size()));
            for (auto v : c["queue"].toArray())
                entries.insert(at++, {v, false});
        } else if (name == "remove" || name == "replaceItems") {
            const int from = qBound(0, c["from"].toInt(), int(entries.size()));
            const int to = qBound(from, c["to"].toInt(from), int(entries.size()));
            entries.erase(entries.begin() + from, entries.begin() + to);
            if (name == "replaceItems") {
                int at = from;
                for (auto v : c["queue"].toArray())
                    entries.insert(at++, {v, false});
            }
        } else if (name == "move" && !entries.isEmpty()) {
            const int from = qBound(0, c["from"].toInt(), int(entries.size()) - 1);
            auto value = entries.takeAt(from);
            entries.insert(qBound(0, c["to"].toInt(), int(entries.size())), value);
        } else if (name == "moveRange") {
            const int from = qBound(0, c["from"].toInt(), int(entries.size()));
            const int to = qBound(from, c["to"].toInt(from), int(entries.size()));
            const auto moving = entries.mid(from, to - from);
            entries.erase(entries.begin() + from, entries.begin() + to);
            int at = qBound(0, c["index"].toInt(), int(entries.size()));
            for (auto v : moving)
                entries.insert(at++, v);
        } else {
            emit message("This playback command is not supported.");
            return;
        }
        if (entries.size() > 20000)
            return;
        QJsonArray updated;
        int newIndex = -1;
        for (int i = 0; i < entries.size(); ++i) {
            updated.append(entries[i].first);
            if (entries[i].second)
                newIndex = i;
        }
        if (newIndex < 0)
            newIndex = entries.isEmpty() ? -1 : qBound(0, index, int(entries.size()) - 1);
        s["queue"] = updated;
        s["index"] = newIndex;
        s.remove("order");
        if (index < 0 || newIndex < 0 || items[index] != updated[newIndex]) {
            s["position"] = 0;
            reload = true;
        } else {
            if (!normalizeSession(s))
                return;
            current = s;
            queue.clear();
            for (auto v : updated)
                queue.append(Track::fromWire(v.toObject()));
        }
    }
    if (reload) {
        if (!normalizeSession(s)) {
            emit message("The device sent an invalid queue.");
            return;
        }
        startConnectTrack(s);
    }
    emit playbackChanged(connectSnapshot());
    if (!releasingConnect && preparedTransfer.isEmpty())
        remote->publishRendererState(connectSnapshot());
}
} // namespace velune
