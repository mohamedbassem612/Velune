#include "QtPlaybackEngine.h"
#include <QTimer>
#include <cmath>
#ifdef VELUNE_MULTIMEDIA
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QPluginLoader>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QMediaPlayer>
#include <QVideoSink>
#include <algorithm>
#endif
namespace velune {
#ifdef VELUNE_MULTIMEDIA
namespace {
bool multimediaPluginReady(QString &reason) {
    const auto requested = qEnvironmentVariable("QT_MEDIA_BACKEND");
    QStringList candidates;
    struct Candidate { QString path; QJsonObject metadata; };
    QList<Candidate> dynamic;
    auto key = [](const QJsonObject &metadata) {
        const auto keys = metadata.value("MetaData").toObject().value("Keys").toArray();
        return keys.isEmpty() ? QString() : keys.first().toString();
    };
    auto matches = [&](const QJsonObject &metadata) {
        if (metadata.value("IID").toString() != QLatin1String("org.qt-project.Qt.QPlatformMediaPlugin"))
            return false;
        const auto name = key(metadata);
        return !name.isEmpty() && (requested.isEmpty() || name == requested);
    };
    // Do not instantiate Multimedia objects here: Qt 6.4 calls qFatal when its
    // integration plugin is absent. Use only public QtCore plugin APIs, and load
    // one candidate only so FFmpeg and GStreamer cannot initialize concurrently.
    for (const auto &plugin : QPluginLoader::staticPlugins()) {
        if (matches(plugin.metaData())) {
            const auto name = key(plugin.metaData());
            candidates << name;
            if (!plugin.instance()) {
                reason = "The Qt Multimedia plugin '" + name + "' could not initialize.";
                return false;
            }
        }
    }
    for (const auto &path : QCoreApplication::libraryPaths()) {
        const QDir directory(QDir(path).filePath("multimedia"));
        for (const auto &file : directory.entryList(QDir::Files)) {
            QPluginLoader plugin(directory.absoluteFilePath(file));
            if (!matches(plugin.metaData()))
                continue;
            dynamic.append({directory.absoluteFilePath(file), plugin.metaData()});
        }
    }
    if (!requested.isEmpty()) {
        if (candidates.isEmpty() && dynamic.isEmpty())
            reason = "The requested Qt Multimedia backend '" + requested + "' was not found.";
        else if (!dynamic.isEmpty()) {
            QPluginLoader plugin(dynamic.first().path);
            if (!plugin.load()) {
                reason = "The requested Qt Multimedia backend could not load: " + plugin.errorString();
                return false;
            }
        }
        return !candidates.isEmpty() || !dynamic.isEmpty();
    }
    if (candidates.isEmpty() && dynamic.isEmpty()) {
        reason = "No Qt Multimedia plugin was found.";
        return false;
    }
    // Qt 6.4 prefers FFmpeg when it is available. Preflight that same candidate
    // and leave the final integration selection to Qt.
    auto selected = std::find_if(dynamic.cbegin(), dynamic.cend(), [&](const Candidate &candidate) {
        return key(candidate.metadata) == QLatin1String("ffmpeg");
    });
    if (selected == dynamic.cend())
        selected = dynamic.cbegin();
    if (selected != dynamic.cend()) {
        QPluginLoader plugin(selected->path);
        if (!plugin.load()) {
            reason = "The Qt Multimedia backend could not load: " + plugin.errorString();
            return false;
        }
    }
    return true;
}
} // namespace
#endif
struct QtPlaybackEngine::Backend {
    bool deviceFound = true;
#ifdef VELUNE_MULTIMEDIA
    // Player is destroyed first, before its audio output.
    QAudioOutput output;
    QMediaPlayer player;
#endif
};
QtPlaybackEngine::QtPlaybackEngine(QObject *parent)
    : IPlaybackEngine(parent) {
#ifdef VELUNE_MULTIMEDIA
    if (!multimediaPluginReady(unavailableReason)) {
        status = PlaybackState::Unavailable;
        return;
    }
    backend = std::make_unique<Backend>();
    backend->player.setAudioOutput(&backend->output);
    backend->player.setVideoSink(new QVideoSink(&backend->player));
    backend->output.setVolume(level);
    if (!backend->player.isAvailable()) {
        unavailableReason = "The loaded Qt Multimedia backend has no media player support.";
        status = PlaybackState::Unavailable;
    }
#else
    status = PlaybackState::Unavailable;
#endif
}
QtPlaybackEngine::~QtPlaybackEngine() {
    cancel();
}
QtPlaybackEngine::QtPlaybackEngine(const QByteArray &device, QObject *parent) : QtPlaybackEngine(parent) {
#ifdef VELUNE_MULTIMEDIA
    if (!backend || device.isEmpty()) return;
    backend->deviceFound = false;
    for (const auto &output : QMediaDevices::audioOutputs())
        if (output.id() == device) {
            backend->output.setDevice(output);
            backend->deviceFound = true;
            break;
        }
    if (!backend->deviceFound) {
        unavailableReason = "The requested audio output device was not found.";
        status = PlaybackState::Unavailable;
    }
#else
    Q_UNUSED(device);
#endif
}
bool QtPlaybackEngine::available() const {
#ifdef VELUNE_MULTIMEDIA
    return backend && backend->deviceFound && backend->player.isAvailable();
#else
    return false;
#endif
}
void QtPlaybackEngine::transition(PlaybackState value) {
    if (status == value)
        return;
    status = value;
    if (request)
        emit stateChanged(request, status);
}
void QtPlaybackEngine::load(const QUrl &url, PlaybackRequest id) {
    cancel();
    request = id;
    if (!available()) {
        status = PlaybackState::Unavailable;
        emit stateChanged(request, PlaybackState::Unavailable);
        emit errorOccurred(
            request,
            "Local playback needs Qt Multimedia. " + unavailableReason +
                " Install the Qt Multimedia plugins and their runtime dependencies, or check QT_PLUGIN_PATH "
                "and QT_MEDIA_BACKEND. You can control playback on a connected Velune device.");
        return;
    }
    if (url.isEmpty() || !url.isValid() || url.scheme().isEmpty()) {
        transition(PlaybackState::Error);
        emit errorOccurred(request, "Invalid media source.");
        return;
    }
    media = url;
#ifdef VELUNE_MULTIMEDIA
    auto &p = backend->player;
    const auto version = revision;
    // Rebind each source. Already queued invocations must also pass the revision fence.
    auto active = [this, version] { return request && revision == version; };
    connect(&p, &QMediaPlayer::tracksChanged, this, [this, active] {
        QTimer::singleShot(0, this, [this, active] {
            if (active() && backend->player.activeVideoTrack() != -1)
                backend->player.setActiveVideoTrack(-1);
        });
    });
    connect(&p, &QMediaPlayer::positionChanged, this, [this, active](qint64 value) {
        if (active())
            emit positionChanged(request, value);
    });
    connect(&p, &QMediaPlayer::durationChanged, this, [this, active](qint64 value) {
        if (active())
            emit durationChanged(request, value);
    });
    connect(&p, &QMediaPlayer::seekableChanged, this, [this, active](bool value) {
        if (!active())
            return;
        emit seekableChanged(request, value);
        if (active() && value && pendingSeek >= 0) {
            const auto target = pendingSeek;
            pendingSeek = -1;
            seek(target);
        }
    });
    connect(&p, &QMediaPlayer::bufferProgressChanged, this, [this, active](float value) {
        if (active())
            emit bufferingChanged(request, value);
    });
    connect(&p, &QMediaPlayer::playbackStateChanged, this, [this, active](QMediaPlayer::PlaybackState value) {
        if (!active() || status == PlaybackState::Error || status == PlaybackState::Ended)
            return;
        transition(value == QMediaPlayer::PlayingState  ? PlaybackState::Playing
                   : value == QMediaPlayer::PausedState ? PlaybackState::Paused
                                                        : PlaybackState::Stopped);
    });
    connect(&p, &QMediaPlayer::errorOccurred, this,
            [this, active](QMediaPlayer::Error, const QString &message) {
                if (!active())
                    return;
                const auto id = request;
                transition(PlaybackState::Error);
                if (active())
                    emit errorOccurred(id, message);
            });
    connect(&p, &QMediaPlayer::mediaStatusChanged, this, [this, active](QMediaPlayer::MediaStatus value) {
        if (!active())
            return;
        if (value == QMediaPlayer::LoadingMedia)
            transition(PlaybackState::Loading);
        if (value == QMediaPlayer::LoadedMedia)
            emit mediaReady(request);
        if (!active())
            return;
        if (value == QMediaPlayer::EndOfMedia && !ended) {
            ended = true;
            const auto id = request;
            transition(PlaybackState::Ended);
            if (active())
                emit endOfMedia(id);
        }
    });
    transition(PlaybackState::Loading);
    if (active())
        p.setSource(url);
#endif
}
void QtPlaybackEngine::play() {
#ifdef VELUNE_MULTIMEDIA
    if (backend && !media.isEmpty() && status != PlaybackState::Error) {
        ended = false;
        // Allow transitions after an explicit restart from Ended.
        if (status == PlaybackState::Ended)
            transition(PlaybackState::Stopped);
        backend->player.play();
    }
#endif
}
void QtPlaybackEngine::resume() {
    play();
}
void QtPlaybackEngine::pause() {
#ifdef VELUNE_MULTIMEDIA
    if (backend && !media.isEmpty() && status != PlaybackState::Error)
        backend->player.pause();
#endif
}
void QtPlaybackEngine::stop() {
#ifdef VELUNE_MULTIMEDIA
    pendingSeek = -1;
    ended = false;
    if (backend) backend->player.stop();
    if (!media.isEmpty())
        transition(PlaybackState::Stopped);
#endif
}
void QtPlaybackEngine::cancel() {
    ++revision;
    request = 0;
#ifdef VELUNE_MULTIMEDIA
    if (backend) {
        disconnect(&backend->player, nullptr, this, nullptr);
        backend->player.stop();
        backend->player.setSource({});
    }
#endif
    media.clear();
    pendingSeek = -1;
    ended = false;
    status = available() ? PlaybackState::Idle : PlaybackState::Unavailable;
}
void QtPlaybackEngine::seek(qint64 value) {
    value = qMax(qint64(0), value);
    if (duration() > 0)
        value = qMin(value, duration());
#ifdef VELUNE_MULTIMEDIA
    if (seekable())
        backend->player.setPosition(value);
    else if (!media.isEmpty())
        pendingSeek = value;
#else
    Q_UNUSED(value);
#endif
}
void QtPlaybackEngine::setVolume(float value) {
    if (!std::isfinite(value))
        return;
    level = qBound(0.f, value, 1.f);
#ifdef VELUNE_MULTIMEDIA
    if (backend) backend->output.setVolume(level);
#endif
}
float QtPlaybackEngine::volume() const {
    return level;
}
qint64 QtPlaybackEngine::position() const {
#ifdef VELUNE_MULTIMEDIA
    return backend ? backend->player.position() : 0;
#else
    return 0;
#endif
}
qint64 QtPlaybackEngine::duration() const {
#ifdef VELUNE_MULTIMEDIA
    return backend ? backend->player.duration() : 0;
#else
    return 0;
#endif
}
bool QtPlaybackEngine::seekable() const {
#ifdef VELUNE_MULTIMEDIA
    return backend && backend->player.isSeekable();
#else
    return false;
#endif
}
QUrl QtPlaybackEngine::source() const {
    return media;
}
PlaybackState QtPlaybackEngine::state() const {
    return status;
}
} // namespace velune
