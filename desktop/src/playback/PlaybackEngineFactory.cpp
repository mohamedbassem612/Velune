#include "PlaybackEngineFactory.h"
#include "CrossfadePlaybackEngine.h"
#include "ProcessPlaybackEngine.h"
namespace velune {
std::unique_ptr<IPlaybackEngine> createPlaybackEngine() {
    return std::make_unique<CrossfadePlaybackEngine>(
        [] { return std::make_unique<ProcessPlaybackEngine>(); });
}
} // namespace velune
