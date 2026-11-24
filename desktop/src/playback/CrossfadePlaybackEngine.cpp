#include "CrossfadePlaybackEngine.h"
namespace velune {
CrossfadePlaybackEngine::CrossfadePlaybackEngine(Factory f, QObject *p)
    : IPlaybackEngine(p), factory(std::move(f)), live(factory()) {
    fadeTimer.setInterval(20);
    connect(&fadeTimer, &QTimer::timeout, this, &CrossfadePlaybackEngine::tick);
    wireLive();
}
CrossfadePlaybackEngine::~CrossfadePlaybackEngine() {
    cancel();
}
bool CrossfadePlaybackEngine::available() const {
    return live && live->available();
}
qint64 CrossfadePlaybackEngine::position() const {
    return live->position();
}
qint64 CrossfadePlaybackEngine::duration() const {
    return live->duration();
}
bool CrossfadePlaybackEngine::seekable() const {
    return live->seekable();
}
QUrl CrossfadePlaybackEngine::source() const {
    return live->source();
}
PlaybackState CrossfadePlaybackEngine::state() const {
    return live->state();
}
void CrossfadePlaybackEngine::wireLive() {
    auto *e = live.get();
    connect(e, &IPlaybackEngine::mediaReady, this, [this](PlaybackRequest) { emit mediaReady(request); });
    connect(e, &IPlaybackEngine::stateChanged, this,
            [this](PlaybackRequest, PlaybackState s) { emit stateChanged(request, s); });
    connect(e, &IPlaybackEngine::positionChanged, this,
            [this](PlaybackRequest, qint64 p) { emit positionChanged(request, p); });
    connect(e, &IPlaybackEngine::durationChanged, this,
            [this](PlaybackRequest, qint64 d) { emit durationChanged(request, d); });
    connect(e, &IPlaybackEngine::seekableChanged, this,
            [this](PlaybackRequest, bool s) { emit seekableChanged(request, s); });
    connect(e, &IPlaybackEngine::bufferingChanged, this,
            [this](PlaybackRequest, float p) { emit bufferingChanged(request, p); });
    connect(e, &IPlaybackEngine::pauseConfirmed, this,
            [this](PlaybackRequest) { emit pauseConfirmed(request); });
    connect(e, &IPlaybackEngine::errorOccurred, this, [this](PlaybackRequest, QString error) {
        finishFade();
        emit errorOccurred(request, error);
    });
    connect(e, &IPlaybackEngine::endOfMedia, this, [this](PlaybackRequest) {
        finishFade();
        emit endOfMedia(request);
    });
}
void CrossfadePlaybackEngine::finishFade() {
    fadeTimer.stop();
    if (retiring) {
        retiring->cancel();
        retiring.reset();
        mix = 1;
        live->setVolume(level);
        emit crossfadeFinished();
    }
}
void CrossfadePlaybackEngine::discardNext() {
    ready = false;
    nextToken = 0;
    if (next) {
        next->disconnect(this);
        next->cancel();
        next.reset();
    }
}
void CrossfadePlaybackEngine::cancel() {
    discardNext();
    finishFade();
    live->cancel();
    request = 0;
}
void CrossfadePlaybackEngine::load(const QUrl &url, PlaybackRequest id) {
    cancel();
    request = id;
    live->setVolume(level);
    live->setSpeed(speed);
    live->load(url, id);
}
void CrossfadePlaybackEngine::play() {
    live->play();
}
void CrossfadePlaybackEngine::pause() {
    finishFade();
    live->pause();
}
void CrossfadePlaybackEngine::release() {
    discardNext();
    finishFade();
    live->release();
}
void CrossfadePlaybackEngine::stop() {
    discardNext();
    finishFade();
    live->stop();
}
void CrossfadePlaybackEngine::seek(qint64 p) {
    finishFade();
    live->seek(p);
}
void CrossfadePlaybackEngine::setVolume(float v) {
    level = qBound(0.f, v, 1.f);
    live->setVolume(float(level * mix));
    if (retiring)
        retiring->setVolume(float(level * (1 - mix)));
}
void CrossfadePlaybackEngine::setSpeed(float v) {
    speed = qBound(.25f, v, 4.f);
    live->setSpeed(speed);
    if (next)
        next->setSpeed(speed);
    if (retiring)
        retiring->setSpeed(speed);
}
void CrossfadePlaybackEngine::prepareNext(const QUrl &url, quint64 token) {
    discardNext();
    nextToken = token;
    next = factory();
    auto *candidate = next.get();
    next->setVolume(0);
    next->setSpeed(speed);
    connect(candidate, &IPlaybackEngine::mediaReady, this, [this, candidate, token](PlaybackRequest) {
        if (next.get() != candidate || nextToken != token)
            return;
        ready = true;
        emit nextPrepared(token, true);
    });
    connect(candidate, &IPlaybackEngine::errorOccurred, this,
            [this, candidate, token](PlaybackRequest, QString) {
                if (next.get() != candidate || nextToken != token)
                    return;
                ready = false;
                // The owner discards this failed candidate after leaving the engine callback.
                emit nextPrepared(token, false);
            });
    next->load(url, token);
}
bool CrossfadePlaybackEngine::activateNext(PlaybackRequest id, int ms) {
    if (!next || !ready)
        return false;
    finishFade();
    live->disconnect(this);
    next->disconnect(this);
    retiring = std::move(live);
    live = std::move(next);
    request = id;
    ready = false;
    nextToken = 0;
    mix = 0;
    fadeMs = qBound(100, ms, 12000);
    wireLive();
    elapsed.start();
    fadeTimer.start();
    live->setVolume(0);
    live->play();
    emit mediaReady(request);
    emit durationChanged(request, live->duration());
    return true;
}
void CrossfadePlaybackEngine::tick() {
    if (!retiring) {
        fadeTimer.stop();
        return;
    }
    mix = qBound(0.0, double(elapsed.elapsed()) / fadeMs, 1.0);
    live->setVolume(float(level * mix));
    retiring->setVolume(float(level * (1 - mix)));
    if (mix >= 1)
        finishFade();
}
} // namespace velune
