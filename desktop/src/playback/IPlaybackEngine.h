#pragma once
#include "PlaybackTypes.h"
#include <QObject>
#include <QUrl>
namespace velune {
// All operations and notifications belong to the object's thread. Times are milliseconds.
class IPlaybackEngine : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual bool available() const = 0;
    virtual QString backendName() const { return QStringLiteral("unknown"); }
    virtual void load(const QUrl &source, PlaybackRequest request) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;                  // Retains media; resume explicitly restarts it.
    virtual void cancel() = 0;                // Invalidates notifications and releases media.
    virtual void seek(qint64 position) = 0;   // Last request wins until seekable.
    virtual void setVolume(float volume) = 0; // Normalized [0, 1].
    virtual float volume() const = 0;
    virtual qint64 position() const = 0;
    virtual qint64 duration() const = 0;
    virtual bool seekable() const = 0;
    virtual QUrl source() const = 0;
    virtual PlaybackState state() const = 0;
  signals:
    // Media parsed/loaded; neither this nor Playing confirms audible output.
    void mediaReady(velune::PlaybackRequest request);
    void stateChanged(velune::PlaybackRequest request, velune::PlaybackState state);
    void positionChanged(velune::PlaybackRequest request, qint64 position);
    void durationChanged(velune::PlaybackRequest request, qint64 duration);
    void seekableChanged(velune::PlaybackRequest request, bool seekable);
    void bufferingChanged(velune::PlaybackRequest request, float progress);
    void errorOccurred(velune::PlaybackRequest request, QString message);
    void endOfMedia(velune::PlaybackRequest request);
};
} // namespace velune
