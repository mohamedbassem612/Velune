#include "ProcessPlaybackEngine.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
namespace velune {
ProcessPlaybackEngine::ProcessPlaybackEngine(QObject *p) : IPlaybackEngine(p) {}
ProcessPlaybackEngine::~ProcessPlaybackEngine() {
    cancel();
}
QString ProcessPlaybackEngine::executable() const {
    auto base = QCoreApplication::applicationDirPath();
    auto path = base + "/native/velune-audio";
#ifdef Q_OS_WIN
    path += ".exe";
#endif
    return path;
}
bool ProcessPlaybackEngine::available() const {
    return QFileInfo(executable()).isExecutable();
}
void ProcessPlaybackEngine::command(QByteArray name, QByteArray value) {
    if (process && initialized && process->state() == QProcess::Running)
        process->write(name + '\t' + value + '\n');
}
void ProcessPlaybackEngine::change(PlaybackState s) {
    if (status != s) {
        status = s;
        emit stateChanged(request, s);
    }
}
void ProcessPlaybackEngine::cancel() {
    auto old = process;
    process = nullptr;
    request = 0;
    initialized = loaded = canSeek = intent = releasePending = false;
    output.clear();
    url = QUrl();
    pos = length = pendingSeek = 0;
    status = PlaybackState::Idle;
    if (old) {
        old->disconnect(this);
        old->closeWriteChannel();
        if (old->state() != QProcess::NotRunning) {
            old->kill();
            old->waitForFinished(1000);
        }
        delete old;
    }
}
void ProcessPlaybackEngine::load(const QUrl &src, PlaybackRequest ticket) {
    cancel();
    request = ticket;
    url = src;
    if (!available() || src.isEmpty()) {
        change(PlaybackState::Error);
        emit errorOccurred(request, "The native audio helper is unavailable.");
        return;
    }
    process = new QProcess(this);
    auto child = process;
    auto env = QProcessEnvironment::systemEnvironment();
    const auto libs = QCoreApplication::applicationDirPath() + "/native/lib";
    env.insert("LD_LIBRARY_PATH", libs);
    env.insert("VELUNE_MPV_LIBRARY", libs + "/libmpv.so.2");
    process->setProcessEnvironment(env);
    process->setProgram(executable());
    connect(child, &QProcess::readyReadStandardOutput, this, [this, child, ticket] {
        if (process != child || request != ticket)
            return;
        output += child->readAllStandardOutput();
        if (output.size() > 1024 * 1024) {
            child->kill();
            emit errorOccurred(ticket, "Invalid audio helper response.");
            return;
        }
        int end;
        while ((end = output.indexOf('\n')) >= 0) {
            auto line = output.left(end);
            output.remove(0, end + 1);
            receive(QJsonDocument::fromJson(line).object());
            if (process != child)
                return;
        }
    });
    connect(child, &QProcess::readyReadStandardError, child, [child] { child->readAllStandardError(); });
    connect(child, &QProcess::errorOccurred, this, [this, child, ticket](QProcess::ProcessError) {
        if (process == child && request == ticket) {
            change(PlaybackState::Error);
            emit errorOccurred(ticket, "The native audio helper stopped unexpectedly.");
        }
    });
    connect(child, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, child, ticket](int, QProcess::ExitStatus) {
                if (process == child && request == ticket && status != PlaybackState::Ended &&
                    status != PlaybackState::Error) {
                    change(PlaybackState::Error);
                    emit errorOccurred(ticket, "The native audio helper exited.");
                }
            });
    change(PlaybackState::Loading);
    child->start();
    QTimer::singleShot(20000, this, [this, child, ticket] {
        if (process == child && request == ticket && !loaded) {
            child->kill();
            change(PlaybackState::Error);
            emit errorOccurred(ticket, "The audio source did not become ready.");
        }
    });
}
void ProcessPlaybackEngine::play() {
    intent = true;
    if (loaded)
        command("pause", "0");
}
void ProcessPlaybackEngine::pause() {
    intent = false;
    command("pause", "1");
}
void ProcessPlaybackEngine::release() {
    intent = false;
    releasePending = true;
    if (!process) {
        emit pauseConfirmed(request);
        return;
    }
    command("release", "1");
}
void ProcessPlaybackEngine::stop() {
    pause();
    seek(0);
    change(PlaybackState::Stopped);
}
void ProcessPlaybackEngine::seek(qint64 value) {
    pendingSeek = qBound<qint64>(0, value, 9000000000000ll);
    pos = pendingSeek;
    if (loaded)
        command("seek", QByteArray::number(pendingSeek));
}
void ProcessPlaybackEngine::setVolume(float v) {
    level = qBound(0.f, v, 1.f);
    command("volume", QByteArray::number(qRound(level * 100)));
}
void ProcessPlaybackEngine::setSpeed(float v) {
    speed = qBound(.25f, v, 4.f);
    command("speed", QByteArray::number(speed));
}
void ProcessPlaybackEngine::receive(QJsonObject m) {
    auto type = m["event"].toString();
    if (type == "initialized") {
        initialized = true;
        command("volume", QByteArray::number(qRound(level * 100)));
        command("speed", QByteArray::number(speed));
        command("load", url.toEncoded(QUrl::FullyEncoded).toHex());
        if (releasePending)
            command("release", "1");
    } else if (type == "ready") {
        loaded = true;
        command("seek", QByteArray::number(pendingSeek));
        command("pause", intent ? "0" : "1");
        if (!intent)
            change(PlaybackState::Paused);
        emit mediaReady(request);
    } else if (type == "active") {
        change(intent ? PlaybackState::Playing : PlaybackState::Paused);
    } else if (type == "pause-confirmed") {
        change(PlaybackState::Paused);
        emit pauseConfirmed(request);
    } else if (type == "ended") {
        intent = false;
        change(PlaybackState::Ended);
        emit endOfMedia(request);
    } else if (type == "error") {
        change(PlaybackState::Error);
        emit errorOccurred(request, "The audio source could not be played.");
    } else if (type == "property") {
        auto name = m["name"].toString();
        auto value = m["value"];
        if (name == "time-pos" && loaded) {
            pos = qMax<qint64>(0, qRound64(value.toDouble() * 1000));
            emit positionChanged(request, pos);
        } else if (name == "duration") {
            length = qMax<qint64>(0, qRound64(value.toDouble() * 1000));
            emit durationChanged(request, length);
        } else if (name == "seekable") {
            canSeek = value.toBool();
            emit seekableChanged(request, canSeek);
        } else if (name == "pause" && loaded)
            change(value.toBool() ? PlaybackState::Paused : PlaybackState::Playing);
        else if (name == "cache-buffering-state")
            emit bufferingChanged(request, float(value.toDouble() / 100));
    }
}
} // namespace velune
