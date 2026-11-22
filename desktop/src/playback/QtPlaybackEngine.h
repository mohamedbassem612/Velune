#pragma once
#include "IPlaybackEngine.h"
#include <memory>
namespace velune {
// Also supplies the unavailable backend in builds without Qt Multimedia.
class QtPlaybackEngine final : public IPlaybackEngine {
    Q_OBJECT
  public:
    explicit QtPlaybackEngine(QObject *parent = nullptr);
    explicit QtPlaybackEngine(const QByteArray &audioDevice, QObject *parent = nullptr);
    ~QtPlaybackEngine() override;
    bool available() const override;
    QString backendName() const override { return QStringLiteral("qt"); }
    void load(const QUrl &source, PlaybackRequest request) override;
    void play() override;
    void pause() override;
    void resume() override;
    void stop() override;
    void cancel() override;
    void seek(qint64 position) override;
    void setVolume(float volume) override;
    float volume() const override;
    qint64 position() const override;
    qint64 duration() const override;
    bool seekable() const override;
    QUrl source() const override;
    PlaybackState state() const override;

  private:
    struct Backend;
    std::unique_ptr<Backend> backend;
    PlaybackRequest request = 0;
    quint64 revision = 0;
    PlaybackState status = PlaybackState::Idle;
    QUrl media;
    QString unavailableReason;
    float level = .75f;
    qint64 pendingSeek = -1;
    bool ended = false;
    void transition(PlaybackState state);
};
} // namespace velune
