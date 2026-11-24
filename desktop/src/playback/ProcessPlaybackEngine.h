#pragma once
#include "IPlaybackEngine.h"
#include <QProcess>
#include <QTimer>
namespace velune {
// Each source owns a helper process. Notifications are fenced by process and request.
class ProcessPlaybackEngine final : public IPlaybackEngine {
    Q_OBJECT
  public:
    explicit ProcessPlaybackEngine(QObject *parent = nullptr);
    ~ProcessPlaybackEngine() override;
    bool available() const override;
    QString backendName() const override {
        return "libmpv process";
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
    qint64 position() const override {
        return pos;
    }
    qint64 duration() const override {
        return length;
    }
    bool seekable() const override {
        return canSeek;
    }
    QUrl source() const override {
        return url;
    }
    PlaybackState state() const override {
        return status;
    }

  private:
    QProcess *process = nullptr;
    QByteArray output;
    QUrl url;
    PlaybackRequest request = 0;
    PlaybackState status = PlaybackState::Idle;
    qint64 pos = 0, length = 0, pendingSeek = 0;
    float level = .75f, speed = 1;
    bool canSeek = false, loaded = false, initialized = false, intent = false, releasePending = false;
    QString executable() const;
    void command(QByteArray, QByteArray);
    void change(PlaybackState);
    void receive(QJsonObject);
};
} // namespace velune
