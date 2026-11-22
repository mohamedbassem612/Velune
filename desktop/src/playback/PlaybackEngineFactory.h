#pragma once
#include "IPlaybackEngine.h"
#include <memory>
namespace velune {
struct PlaybackEngineSelection {
    std::unique_ptr<IPlaybackEngine> engine;
    QString diagnostic;
};
// An explicit value is useful for tests; the default reads VELUNE_PLAYBACK_BACKEND.
PlaybackEngineSelection
selectPlaybackEngine(QString requested = qEnvironmentVariable("VELUNE_PLAYBACK_BACKEND"),
                     QByteArray audioDevice = {});
std::unique_ptr<IPlaybackEngine> createPlaybackEngine();
} // namespace velune
