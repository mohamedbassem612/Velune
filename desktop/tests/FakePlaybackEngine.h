#pragma once
#include "playback/IPlaybackEngine.h"
#include <memory>
namespace velune {
struct PlaybackCalls {
    int loads = 0, cancels = 0;
    bool destroyed = false;
};
class FakePlaybackEngine final : public IPlaybackEngine {
  public:
    std::shared_ptr<PlaybackCalls> calls = std::make_shared<PlaybackCalls>();
    PlaybackRequest request = 0;
    QUrl media;
    PlaybackState status = PlaybackState::Idle;
    qint64 time = 0, length = 20000;
    float level = .75f;
    bool usable = true;
    ~FakePlaybackEngine() override {
        calls->destroyed = true;
    }
    bool available() const override {
        return usable;
    }
    void load(const QUrl &url, PlaybackRequest id) override {
        ++calls->loads;
        media = url;
        request = id;
        time = 0;
        change(usable ? PlaybackState::Loading : PlaybackState::Unavailable);
        if (!usable)
            emit errorOccurred(id, "Local playback needs Qt Multimedia.");
    }
    void change(PlaybackState value) {
        status = value;
        emit stateChanged(request, value);
    }
    void play() override {
        if (!media.isEmpty())
            change(PlaybackState::Playing);
    }
    void pause() override {
        if (!media.isEmpty())
            change(PlaybackState::Paused);
    }
    void resume() override {
        play();
    }
    void stop() override {
        time = 0;
        change(PlaybackState::Stopped);
    }
    void cancel() override {
        ++calls->cancels;
        media.clear();
        request = 0;
        time = 0;
        status = PlaybackState::Idle;
    }
    void seek(qint64 value) override {
        time = qBound(qint64(0), value, length);
        emit positionChanged(request, time);
    }
    void setVolume(float value) override {
        level = qBound(0.f, value, 1.f);
    }
    float volume() const override {
        return level;
    }
    qint64 position() const override {
        return time;
    }
    qint64 duration() const override {
        return length;
    }
    bool seekable() const override {
        return !media.isEmpty();
    }
    QUrl source() const override {
        return media;
    }
    PlaybackState state() const override {
        return status;
    }
};
} // namespace velune
