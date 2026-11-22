#pragma once
#include "PlaybackTypes.h"
#include <QElapsedTimer>
#include <QJsonObject>
namespace velune {
// One bounded, sanitized record per current request. Storage/logging are disabled by default.
class PlaybackTrace {
  public:
    explicit PlaybackTrace(bool enabled = qEnvironmentVariableIntValue("VELUNE_PLAYBACK_TRACE") == 1)
        : enabled(enabled) {}
    void begin(PlaybackRequest request, const QString &backend, const QString &category,
               QElapsedTimer origin = {});
    void mark(PlaybackRequest request, const QString &stage);
    void finish(const QString &outcome);
    QJsonObject snapshot() const;

  private:
    bool enabled;
    PlaybackRequest request = 0;
    QElapsedTimer clock;
    QJsonObject record, stages;
};
} // namespace velune
