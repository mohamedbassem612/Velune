#pragma once
#include "IPlaybackEngine.h"
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <memory>
namespace velune {
class CrossfadePlaybackEngine final : public IPlaybackEngine {
    Q_OBJECT
  public:
    using Factory = std::function<std::unique_ptr<IPlaybackEngine>()>;
    explicit CrossfadePlaybackEngine(Factory factory, QObject *parent = nullptr);
    ~CrossfadePlaybackEngine() override;
    bool available() const override;
    QString backendName() const override {
        return "libmpv crossfade";
    }
    void load(const QUrl &, PlaybackRequest) override;
    void play() override;
    void pause() override;
    void release() override;
    void resume() override {
        play();
    }
    void stop() override;
    void cancel() override;
    void seek(qint64) override;
    void setVolume(float) override;
    void setSpeed(float) override;
    float volume() const override {
        return level;
    }
    qint64 position() const override;
    qint64 duration() const override;
    bool seekable() const override;
    QUrl source() const override;
    PlaybackState state() const override;
    bool supportsCrossfade() const override {
        return true;
    }
    void prepareNext(const QUrl &, quint64) override;
    void discardNext() override;
    bool activateNext(PlaybackRequest, int) override;

  private:
    Factory factory;
    std::unique_ptr<IPlaybackEngine> live, next, retiring;
    PlaybackRequest request = 0;
    quint64 nextToken = 0;
    bool ready = false;
    float level = .75f, speed = 1;
    int fadeMs = 0;
    double mix = 1;
    QTimer fadeTimer;
    QElapsedTimer elapsed;
    void wireLive();
    void finishFade();
    void tick();
};
} // namespace velune
