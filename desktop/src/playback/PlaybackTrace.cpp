#include "PlaybackTrace.h"
#include <QDebug>
#include <QJsonDocument>
namespace velune {
void PlaybackTrace::begin(PlaybackRequest id, const QString &backend, const QString &category,
                          QElapsedTimer origin) {
    if (!enabled)
        return;
    finish("replaced");
    request = id;
    clock = origin;
    if (!clock.isValid())
        clock.start();
    stages = {{"play_request", 0}};
    record = {{"request", QString::number(id)},
              {"backend", backend},
              {"source_category", category},
              {"first_audio_output_ms", QJsonValue::Null},
              {"output_measurement", "unavailable; backend states and position are proxies"}};
}
void PlaybackTrace::mark(PlaybackRequest id, const QString &stage) {
    if (!enabled || !request || id != request || stages.contains(stage))
        return;
    stages[stage] = clock.nsecsElapsed() / 1000000.;
    // A bounded milestone log also makes an in-progress load observable.
    qInfo().noquote() << "velune.playback" << QJsonDocument(snapshot()).toJson(QJsonDocument::Compact);
}
QJsonObject PlaybackTrace::snapshot() const {
    auto result = record;
    result["stages_ms"] = stages;
    return result;
}
void PlaybackTrace::finish(const QString &outcome) {
    if (!enabled || !request)
        return;
    auto result = snapshot();
    result["outcome"] = outcome;
    qInfo().noquote() << "velune.playback" << QJsonDocument(result).toJson(QJsonDocument::Compact);
    request = 0;
}
} // namespace velune
