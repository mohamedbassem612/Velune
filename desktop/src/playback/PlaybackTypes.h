#pragma once
#include <QMetaType>
namespace velune {
using PlaybackRequest = quint64;
enum class PlaybackState { Unavailable, Idle, Loading, Playing, Paused, Stopped, Ended, Error };
} // namespace velune
Q_DECLARE_METATYPE(velune::PlaybackState)
