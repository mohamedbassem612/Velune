#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <cmath>
namespace velune {
// Validate the platform-neutral session before it reaches either renderer.
inline bool normalizeSession(QJsonObject &s, const QJsonObject &base = {}) {
    for (const auto &key : {"epoch", "revision", "queueRevision"}) {
        const double n = s[key].toDouble();
        if (!std::isfinite(n) || n < 0 || n > 9e15 || std::floor(n) != n)
            return false;
    }
    if (s["sessionId"].toString().size() > 128 || s["rendererId"].toString().size() > 64)
        return false;
    if (!s.contains("queue"))
        s["queue"] = base["queue"].isArray() ? base["queue"] : QJsonArray{};
    const auto queue = s["queue"].toArray();
    if (!s["queue"].isArray() || queue.size() > 20000)
        return false;
    for (auto v : queue) {
        const auto t = v.toObject();
        if (t["id"].toString().isEmpty() || t["id"].toString().size() > 256 || !t["title"].isString() ||
            !t["artists"].isArray())
            return false;
    }
    if (!s.contains("order")) {
        if (base["queue"] == s["queue"] && base["order"].isArray())
            s["order"] = base["order"];
        else {
            QJsonArray order;
            for (int i = 0; i < queue.size(); ++i)
                order.append(i);
            s["order"] = order;
        }
    }
    const auto order = s["order"].toArray();
    QSet<int> seen;
    if (order.size() != queue.size())
        return false;
    for (auto v : order) {
        const int i = v.toInt(-1);
        if (!v.isDouble() || v.toDouble() != i || i < 0 || i >= queue.size() || seen.contains(i))
            return false;
        seen.insert(i);
    }
    const int index = qBound(-1, s["index"].toInt(-1), int(queue.size()) - 1);
    s["index"] = index;
    s["song"] = index >= 0 ? queue[index] : QJsonValue(QJsonValue::Null);
    for (const auto &key : {"position", "duration"}) {
        const double n = s[key].toDouble();
        if (!std::isfinite(n) || n < 0 || n > 9e15)
            return false;
        s[key] = n;
    }
    s["repeat"] = qBound(0, s["repeat"].toInt(), 2);
    s["shuffle"] = s["shuffle"].toBool();
    s["speed"] = qBound(0.25, s["speed"].toDouble(1), 4.0);
    s["playing"] = index >= 0 && s["playing"].toBool();
    return true;
}
inline bool sessionEnvelope(const QJsonObject &m, const QJsonObject &s) {
    return !s["sessionId"].toString().isEmpty() && m["sessionId"] == s["sessionId"] &&
           m["epoch"] == s["epoch"];
}
} // namespace velune
