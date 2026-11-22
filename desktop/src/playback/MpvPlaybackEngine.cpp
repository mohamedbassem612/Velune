#include "MpvPlaybackEngine.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QTimer>
#include <cmath>
#include <mutex>
#ifdef VELUNE_MPV
#include "MpvApi.h"
#include <QHash>
#endif
namespace velune {
#ifdef VELUNE_MPV
std::shared_ptr<MpvApi> MpvApi::open(QString &error) {
    auto api = std::make_shared<MpvApi>();
#ifdef Q_OS_WIN
    const QStringList names{QCoreApplication::applicationDirPath() + "/mpv-2.dll",
                            QCoreApplication::applicationDirPath() + "/libmpv-2.dll"};
#else
    const QStringList names{QStringLiteral("libmpv.so.2")};
#endif
    for (const auto &name : names) {
        api->library.setFileName(name);
        if (api->library.load())
            break;
    }
    if (!api->library.isLoaded()) {
        error = "libmpv runtime is missing or one of its dependencies could not be loaded.";
        return {};
    }
#define RESOLVE_MPV(name)                                                                                    \
    api->name = reinterpret_cast<decltype(api->name)>(api->library.resolve("mpv_" #name));                   \
    if (!api->name) {                                                                                        \
        error = "libmpv runtime is missing a required C API symbol.";                                        \
        return {};                                                                                           \
    }
    VELUNE_MPV_FUNCTIONS(RESOLVE_MPV)
#undef RESOLVE_MPV
    const auto version = api->client_api_version();
    if ((version >> 16) != 2 || version < MPV_MAKE_VERSION(2, 2)) {
        error = "libmpv needs compatible client API 2.2 or newer within major version 2.";
        return {};
    }
    return api;
}
#endif
struct MpvPlaybackEngine::Backend {
#ifdef VELUNE_MPV
    std::shared_ptr<MpvApi> api;
    mpv_handle *handle = nullptr;
    // Callback storage survives until the native core has actually stopped.
    std::mutex mutex;
    MpvPlaybackEngine *receiver = nullptr;
    bool queued = false, controlsQueued = false;
    bool submitted = false, retiring = false, idle = true, stopReplied = false;
    bool nativeFileActive = false;
    bool seekInFlight = false;
    int appliedPause = -1;
    double appliedVolume = -1;
    qint64 entry = -1;
    quint64 serial = 0, epoch = 0;
    enum Kind { Load, Stop, Pause, Volume, Seek };
    struct Command {
        Kind kind;
        quint64 revision;
    };
    QHash<quint64, Command> commands;
    static void wake(void *data) noexcept {
        auto *self = static_cast<Backend *>(data);
        std::lock_guard lock(self->mutex);
        if (!self->receiver || self->queued)
            return;
        self->queued = true;
        auto *receiver = self->receiver;
        QMetaObject::invokeMethod(receiver, [receiver] { receiver->drain(); }, Qt::QueuedConnection);
    }
    bool command(Kind kind, quint64 revision, const char **args) {
        const auto id = ++serial;
        commands.insert(id, {kind, revision});
        if (api->command_async(handle, id, args) >= 0)
            return true;
        commands.remove(id);
        return false;
    }
    bool property(Kind kind, quint64 revision, const char *name, mpv_format format, void *value) {
        const auto id = ++serial;
        commands.insert(id, {kind, revision});
        if (api->set_property_async(handle, id, name, format, value) >= 0)
            return true;
        commands.remove(id);
        return false;
    }
    ~Backend() {
        if (handle)
            api->terminate_destroy(handle);
    }
#endif
};
MpvPlaybackEngine::MpvPlaybackEngine(QObject *parent, QString device) : IPlaybackEngine(parent) {
#ifdef VELUNE_MPV
    auto api = MpvApi::open(failure);
    initialize(std::move(api), {}, device);
#else
    Q_UNUSED(device);
    failure = "This build has no libmpv support. Install development headers and rebuild with "
              "VELUNE_ENABLE_MPV=ON.";
#endif
}
MpvPlaybackEngine::MpvPlaybackEngine(std::shared_ptr<MpvApi> api, QString driver, QObject *parent)
    : IPlaybackEngine(parent) {
    initialize(std::move(api), driver);
}
void MpvPlaybackEngine::initialize(std::shared_ptr<MpvApi> api, const QString &driver,
                                   const QString &device) {
#ifdef VELUNE_MPV
    if (!api)
        return;
    backend = std::make_shared<Backend>();
    backend->api = std::move(api);
    backend->handle = backend->api->create();
    if (!backend->handle) {
        failure = "libmpv could not create a playback instance.";
        backend.reset();
        return;
    }
    auto &b = *backend;
    const std::pair<const char *, const char *> options[] = {{"config", "no"},
                                                             {"load-scripts", "no"},
                                                             {"ytdl", "no"},
                                                             {"terminal", "no"},
                                                             {"input-default-bindings", "no"},
                                                             {"input-terminal", "no"},
                                                             {"idle", "yes"},
                                                             {"vid", "no"},
                                                             {"sid", "no"},
                                                             {"audio-display", "no"},
                                                             {"force-window", "no"},
        {"tls-verify", "yes"},
        {"pause", "yes"},
                                                             {"volume", "75"}};
    bool valid = true;
    for (auto [name, value] : options)
        valid = b.api->set_option_string(b.handle, name, value) >= 0 && valid;
    if (!driver.isEmpty())
        valid = b.api->set_option_string(b.handle, "ao", driver.toUtf8().constData()) >= 0 && valid;
    if (!device.isEmpty())
        valid = b.api->set_option_string(b.handle, "audio-device", device.toUtf8().constData()) >= 0 && valid;
    if (!valid || b.api->initialize(b.handle) < 0) {
        failure = "libmpv initialization failed. Check the installed runtime and audio dependencies.";
        backend.reset();
        return;
    }
    if (b.api->observe_property(b.handle, 0, "idle-active", MPV_FORMAT_FLAG) < 0) {
        failure = "libmpv could not observe its load lifecycle.";
        backend.reset();
        return;
    }
    status = PlaybackState::Idle;
    b.receiver = this;
    b.api->set_wakeup_callback(b.handle, &Backend::wake, &b);
#else
    Q_UNUSED(api);
    Q_UNUSED(driver);
    Q_UNUSED(device);
    failure = "This build has no libmpv support.";
#endif
}
MpvPlaybackEngine::~MpvPlaybackEngine() {
    cancel();
#ifdef VELUNE_MPV
    if (backend) {
        {
            std::lock_guard lock(backend->mutex);
            backend->receiver = nullptr;
        }
        backend->api->set_wakeup_callback(backend->handle, nullptr, nullptr);
        // Native destruction can wait for demux/network threads. Never wait on the GUI thread.
        QThreadPool::globalInstance()->start([owned = std::move(backend)] {});
    }
#endif
}
bool MpvPlaybackEngine::available() const {
    return bool(backend);
}
QString MpvPlaybackEngine::initializationError() const {
    return failure;
}
float MpvPlaybackEngine::volume() const {
    return level;
}
qint64 MpvPlaybackEngine::position() const {
    return time;
}
qint64 MpvPlaybackEngine::duration() const {
    return length;
}
bool MpvPlaybackEngine::seekable() const {
    return canSeek;
}
QUrl MpvPlaybackEngine::source() const {
    return media;
}
PlaybackState MpvPlaybackEngine::state() const {
    return status;
}
void MpvPlaybackEngine::transition(PlaybackState value) {
    if (status == value)
        return;
    status = value;
    if (request)
        emit stateChanged(request, value);
}
void MpvPlaybackEngine::fail(const QString &message) {
    const auto id = request;
    const auto version = revision;
    intent = false;
    transition(PlaybackState::Error);
    if (id && version == revision)
        emit errorOccurred(id, message);
    if (version == revision && backend)
        retire();
}
void MpvPlaybackEngine::load(const QUrl &url, PlaybackRequest id) {
    cancel();
    request = id;
    if (!available()) {
        const auto version = revision;
        emit stateChanged(id, PlaybackState::Unavailable);
        if (request == id && revision == version)
            emit errorOccurred(id, failure);
        return;
    }
    if (!url.isValid() || url.isEmpty() ||
        (!url.isLocalFile() && url.scheme() != "http" && url.scheme() != "https")) {
        fail("Invalid media source. Use a local file or HTTP/HTTPS URL.");
        return;
    }
    media = url;
    transition(PlaybackState::Loading);
    scheduleControls();
}
void MpvPlaybackEngine::retire() {
#ifdef VELUNE_MPV
    auto &b = *backend;
    if (!b.submitted || b.retiring)
        return;
    b.retiring = true;
    b.stopReplied = false;
    b.commands.clear();
    b.seekInFlight = false;
    b.api->unobserve_property(b.handle, b.epoch);
    const char *args[]{"stop", nullptr};
    if (!b.command(Backend::Stop, revision, args))
        fail("libmpv could not cancel the previous source.");
#endif
}
void MpvPlaybackEngine::cancel() {
    ++revision;
    request = 0;
    media.clear();
    time = length = 0;
    pendingSeek = -1;
    canSeek = intent = loaded = ended = false;
    status = available() ? PlaybackState::Idle : PlaybackState::Unavailable;
    if (backend)
        retire();
}
void MpvPlaybackEngine::play() {
    if (media.isEmpty() || status == PlaybackState::Error)
        return;
    intent = true;
    if (status == PlaybackState::Ended || status == PlaybackState::Stopped) {
        const auto url = media;
        const auto id = request;
        const auto version = revision;
        const auto deferred = pendingSeek;
        load(url, id);
        if (revision != version + 1 || request != id)
            return;
        pendingSeek = deferred;
        intent = true;
    }
    scheduleControls();
}
void MpvPlaybackEngine::resume() {
    play();
}
void MpvPlaybackEngine::pause() {
    intent = false;
    scheduleControls();
}
void MpvPlaybackEngine::stop() {
    if (media.isEmpty())
        return;
    intent = false;
    loaded = canSeek = false;
    time = 0;
    pendingSeek = -1;
    const auto version = revision;
    const bool wasError = status == PlaybackState::Error;
    retire();
    if (version == revision && (status != PlaybackState::Error || wasError))
        transition(PlaybackState::Stopped);
}
void MpvPlaybackEngine::seek(qint64 value) {
    if (media.isEmpty())
        return;
    pendingSeek = qMax(qint64(0), value);
    if (length > 0)
        pendingSeek = qMin(pendingSeek, length);
    scheduleControls();
}
void MpvPlaybackEngine::setVolume(float value) {
    if (!std::isfinite(value))
        return;
    level = qBound(0.f, value, 1.f);
    scheduleControls();
}
void MpvPlaybackEngine::scheduleControls() {
#ifdef VELUNE_MPV
    if (!backend || backend->controlsQueued)
        return;
    backend->controlsQueued = true;
    QTimer::singleShot(0, this, [this] {
        backend->controlsQueued = false;
        controls();
    });
#endif
}
void MpvPlaybackEngine::beginLoad() {
#ifdef VELUNE_MPV
    auto &b = *backend;
    b.submitted = true;
    b.entry = -1;
    b.epoch = ++b.serial;
    for (const auto name :
         {"time-pos", "duration", "seekable", "pause", "core-idle", "cache-buffering-state"})
        if (b.api->observe_property(b.handle, b.epoch, name,
                                    (QByteArray(name) == "seekable" || QByteArray(name) == "pause" ||
                                     QByteArray(name) == "core-idle")
                                        ? MPV_FORMAT_FLAG
                                        : MPV_FORMAT_DOUBLE) < 0) {
            fail("libmpv could not observe playback properties.");
            return;
        }
    int paused = 1;
    b.property(Backend::Pause, revision, "pause", MPV_FORMAT_FLAG, &paused);
    b.appliedPause = 1;
    b.appliedVolume = -1;
    const auto source = media.isLocalFile() ? media.toLocalFile().toUtf8() : media.toEncoded();
    const char *args[]{"loadfile", source.constData(), "replace", nullptr};
    if (!b.command(Backend::Load, revision, args))
        fail("libmpv could not queue the media source.");
#endif
}
void MpvPlaybackEngine::controls() {
#ifdef VELUNE_MPV
    auto &b = *backend;
    if (b.retiring || !request || media.isEmpty() || status == PlaybackState::Error ||
        status == PlaybackState::Stopped || status == PlaybackState::Ended)
        return;
    if (!b.submitted)
        beginLoad();
    if (!loaded)
        return;
    // Coalesce command spam. Outstanding replies schedule a final reconciliation.
    for (const auto &command : b.commands)
        if (command.kind == Backend::Pause || command.kind == Backend::Volume)
            return;
    int paused = !intent;
    // Silence first when pausing. On startup/restart, apply seek and volume
    // before unpausing so a muted/deferred request cannot briefly play at defaults.
    if (paused && b.appliedPause != paused) {
        if (!b.property(Backend::Pause, revision, "pause", MPV_FORMAT_FLAG, &paused))
            fail("libmpv could not pause playback.");
        else
            b.appliedPause = paused;
        return;
    }
    if (canSeek && pendingSeek >= 0 && !b.seekInFlight) {
        if (length > 0)
            pendingSeek = qMin(pendingSeek, length);
        const auto target = QByteArray::number(pendingSeek / 1000., 'f', 3);
        pendingSeek = -1;
        const char *args[]{"seek", target.constData(), "absolute+exact", nullptr};
        b.seekInFlight = b.command(Backend::Seek, revision, args);
        if (!b.seekInFlight)
            fail("libmpv could not queue the seek.");
        return;
    }
    if (b.seekInFlight)
        return;
    double volume = level * 100.;
    if (b.appliedVolume != volume) {
        if (!b.property(Backend::Volume, revision, "volume", MPV_FORMAT_DOUBLE, &volume))
            fail("libmpv could not set the volume.");
        else
            b.appliedVolume = volume;
        return;
    }
    if (b.appliedPause != paused) {
        if (!b.property(Backend::Pause, revision, "pause", MPV_FORMAT_FLAG, &paused))
            fail("libmpv could not resume playback.");
        else
            b.appliedPause = paused;
    }
#endif
}
void MpvPlaybackEngine::drain() {
#ifdef VELUNE_MPV
    auto &b = *backend;
    {
        std::lock_guard lock(b.mutex);
        b.queued = false;
    }
    QElapsedTimer budget;
    budget.start();
    for (int count = 0; count < 128 && budget.elapsed() < 4; ++count) {
        const auto *event = b.api->wait_event(b.handle, 0);
        if (event->event_id == MPV_EVENT_NONE) {
            if (b.retiring && b.idle && b.stopReplied && !b.nativeFileActive) {
                b.retiring = b.submitted = false;
                scheduleControls();
            }
            return;
        }
        if (event->event_id == MPV_EVENT_SHUTDOWN || event->event_id == MPV_EVENT_QUEUE_OVERFLOW) {
            fail("libmpv stopped unexpectedly or its event queue overflowed.");
            continue;
        }
        // A permanent observer fences retirement independently of source observers.
        // Do not rely on the deprecated MPV_EVENT_IDLE notification.
        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE && event->reply_userdata == 0) {
            const auto *property = static_cast<mpv_event_property *>(event->data);
            if (property->data && property->format == MPV_FORMAT_FLAG &&
                QByteArray(property->name) == "idle-active")
                b.idle = *static_cast<int *>(property->data);
            continue;
        }
        if (event->event_id == MPV_EVENT_COMMAND_REPLY || event->event_id == MPV_EVENT_SET_PROPERTY_REPLY) {
            if (!b.commands.contains(event->reply_userdata))
                continue;
            const auto command = b.commands.take(event->reply_userdata);
            if (command.kind == Backend::Stop)
                b.stopReplied = true;
            if (command.revision != revision || !request)
                continue;
            if (event->error < 0) {
                fail("libmpv rejected a playback operation.");
                continue;
            }
            if (command.kind == Backend::Seek)
                b.seekInFlight = false;
            if (command.kind == Backend::Pause && loaded && !intent)
                transition(PlaybackState::Paused);
            scheduleControls();
            continue;
        }
        if (event->event_id == MPV_EVENT_START_FILE) {
            b.entry = static_cast<mpv_event_start_file *>(event->data)->playlist_entry_id;
            b.nativeFileActive = true;
            continue;
        }
        if (event->event_id == MPV_EVENT_END_FILE &&
            static_cast<mpv_event_end_file *>(event->data)->playlist_entry_id == b.entry)
            b.nativeFileActive = false;
        if (!request || b.retiring || !b.submitted || status == PlaybackState::Error)
            continue;
        const auto version = revision;
        const auto id = request;
        if (event->event_id == MPV_EVENT_FILE_LOADED) {
            loaded = true;
            emit mediaReady(id);
            if (revision == version) {
                if (!intent)
                    transition(PlaybackState::Paused);
                if (revision == version)
                    scheduleControls();
            }
        } else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART && loaded && intent) {
            transition(PlaybackState::Playing);
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end->playlist_entry_id != b.entry)
                continue;
            loaded = false;
            if (end->reason == MPV_END_FILE_REASON_ERROR)
                fail("libmpv could not play this media. Check the source, codec, or audio device.");
            else if (end->reason == MPV_END_FILE_REASON_EOF && !ended) {
                ended = true;
                transition(PlaybackState::Ended);
                if (version == revision)
                    emit endOfMedia(id);
            }
        } else if (event->event_id == MPV_EVENT_PROPERTY_CHANGE && event->reply_userdata == b.epoch &&
                   loaded) {
            const auto *p = static_cast<mpv_event_property *>(event->data);
            if (!p->data)
                continue;
            const QByteArray name(p->name);
            if (p->format == MPV_FORMAT_DOUBLE) {
                const auto value = *static_cast<double *>(p->data);
                if (!std::isfinite(value))
                    continue;
                if (name == "time-pos") {
                    time = qMax(qint64(0), qint64(value * 1000));
                    emit positionChanged(id, time);
                } else if (name == "duration") {
                    length = qMax(qint64(0), qint64(value * 1000));
                    emit durationChanged(id, length);
                } else if (name == "cache-buffering-state")
                    emit bufferingChanged(id, float(qBound(0., value / 100., 1.)));
            } else if (p->format == MPV_FORMAT_FLAG) {
                const bool value = *static_cast<int *>(p->data);
                if (name == "seekable") {
                    canSeek = value;
                    emit seekableChanged(id, value);
                    if (version == revision)
                        scheduleControls();
                } else if (name == "pause" && value && !intent)
                    transition(PlaybackState::Paused);
                else if (name == "core-idle" && !value && intent)
                    transition(PlaybackState::Playing);
            }
        }
    }
    Backend::wake(&b);
#endif
}
} // namespace velune
