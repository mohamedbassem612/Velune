#pragma once
#include "IPlaybackEngine.h"
#include <memory>
namespace velune {
struct MpvApi;
class MpvPlaybackEngine final : public IPlaybackEngine {
    Q_OBJECT
  public:
    explicit MpvPlaybackEngine(QObject *parent = nullptr, QString audioDevice = {});
    // Native test/benchmark injection. Empty driver uses mpv's normal audio selection.
    MpvPlaybackEngine(std::shared_ptr<MpvApi> api, QString audioDriver, QObject *parent = nullptr);
    ~MpvPlaybackEngine() override;
    bool available() const override;
    QString backendName() const override {
        return QStringLiteral("mpv");
    }
    QString initializationError() const;
    void load(const QUrl &, PlaybackRequest) override;
    void play() override;
    void pause() override;
    void resume() override;
    void stop() override;
    void cancel() override;
    void seek(qint64) override;
    void setVolume(float) override;
    float volume() const override;
    qint64 position() const override;
    qint64 duration() const override;
    bool seekable() const override;
    QUrl source() const override;
    PlaybackState state() const override;

  private:
    struct Backend;
    std::shared_ptr<Backend> backend;
    PlaybackRequest request = 0;
    quint64 revision = 0;
    QUrl media;
    PlaybackState status = PlaybackState::Unavailable;
    QString failure;
    qint64 time = 0, length = 0, pendingSeek = -1;
    float level = .75f;
    bool canSeek = false, intent = false, loaded = false, ended = false;
    void initialize(std::shared_ptr<MpvApi>, const QString &, const QString &device = {});
    void drain();
    void beginLoad();
    void scheduleControls();
    void controls();
    void retire();
    void transition(PlaybackState);
    void fail(const QString &);
};
} // namespace velune
