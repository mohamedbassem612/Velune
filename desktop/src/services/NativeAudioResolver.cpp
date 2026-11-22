#include "NativeAudioResolver.h"
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSettings>
#include <QUrlQuery>
#include <algorithm>

namespace velune {
namespace {
QString sharedVisitorData;
struct Client {
    QString name, version, id, agent;
    QJsonObject extra;
};
Client clientFor(const QString &name) {
    // Keep in sync with Android's YouTubeClient definitions.
    if (name == "IOS")
        return {"IOS",
                "19.29.1",
                "5",
                "com.google.ios.youtube/19.29.1 (iPhone16,2; U; CPU iOS 17_5_1 like Mac OS X;)",
                {{"osVersion", "17.5.1.21F90"}}};
    return {"VISIONOS",
            "0.1",
            "101",
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) "
            "Version/18.0 Safari/605.1.15",
            {{"osName", "visionOS"},
             {"osVersion", "1.3.21O771"},
             {"deviceMake", "Apple"},
             {"deviceModel", "RealityDevice14,1"}}};
}
bool supported(const QUrl &url) {
    return url.scheme() == "https" && url.port(443) == 443 && url.userInfo().isEmpty() &&
           (url.host() == "googlevideo.com" || url.host().endsWith(".googlevideo.com")) &&
           !QUrlQuery(url).hasQueryItem("n"); // Needs the JS solver; never publish an unprocessed URL.
}
} // namespace
void NativeAudioResolver::setVisitorData(const QString &value) {
    sharedVisitorData = value;
}
NativeAudioResolver::NativeAudioResolver(QObject *parent, QNetworkAccessManager *manager)
    : QObject(parent), transport(manager ? manager : &network), persist(!manager) {
    deadline.setSingleShot(true);
    deadline.setInterval(4000);
    connect(&deadline, &QTimer::timeout, this, &NativeAudioResolver::finishUnavailable);
}
void NativeAudioResolver::cancel() {
    ++generation;
    running = false;
    deadline.stop();
    if (reply) {
        auto old = reply;
        reply = nullptr;
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
}
void NativeAudioResolver::finishUnavailable() {
    if (qEnvironmentVariableIntValue("VELUNE_PLAYBACK_TRACE") == 1)
        qInfo() << "velune.native fallback" << (deadline.isActive() ? "clients exhausted" : "deadline");
    cancel();
    emit unavailable();
}
void NativeAudioResolver::resolve(const QString &id) {
    cancel();
    videoId = id;
    visitorData = persist ? sharedVisitorData : QString();
    visitorRetried = false;
    clients = {"VISIONOS", "IOS"};
    if (persist) {
        const auto last = QSettings().value("playback/lastNativeClient").toString();
        if (clients.removeOne(last))
            clients.prepend(last);
    }
    clientIndex = 0;
    running = true;
    deadline.start();
    nextClient();
}
void NativeAudioResolver::nextClient() {
    if (clientIndex >= clients.size()) {
        finishUnavailable();
        return;
    }
    const auto client = clientFor(clients[clientIndex++]);
    auto context = client.extra;
    context["clientName"] = client.name;
    context["clientVersion"] = client.version;
    context["hl"] = "en";
    context["gl"] = "US";
    if (!visitorData.isEmpty())
        context["visitorData"] = visitorData;
    QJsonObject body{{"context", QJsonObject{{"client", context}}},
                     {"videoId", videoId},
                     {"contentCheckOk", true},
                     {"racyCheckOk", true}};
    QNetworkRequest request(QUrl("https://music.youtube.com/youtubei/v1/player?prettyPrint=false"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", client.agent.toUtf8());
    request.setRawHeader("X-YouTube-Client-Name", client.id.toUtf8());
    request.setRawHeader("X-YouTube-Client-Version", client.version.toUtf8());
    if (!visitorData.isEmpty())
        request.setRawHeader("X-Goog-Visitor-Id", visitorData.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(2500);
    const auto ticket = generation;
    auto response = transport->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply = response;
    response->setReadBufferSize(2 * 1024 * 1024 + 1);
    connect(response, &QNetworkReply::readyRead, this, [this, response, ticket] {
        if (ticket == generation && response->bytesAvailable() > 2 * 1024 * 1024)
            finishUnavailable();
    });
    connect(response, &QNetworkReply::finished, this, [this, response, client, ticket] {
        if (ticket != generation)
            return;
        reply = nullptr;
        response->deleteLater();
        const auto object = QJsonDocument::fromJson(response->readAll()).object();
        const bool hadVisitor = !visitorData.isEmpty();
        const auto visitor = object["responseContext"].toObject()["visitorData"].toString();
        if (!visitor.isEmpty()) {
            visitorData = visitor;
            if (persist)
                sharedVisitorData = visitor;
        }
        if (response->error() != QNetworkReply::NoError ||
            object["playabilityStatus"].toObject()["status"].toString() != "OK") {
            if (qEnvironmentVariableIntValue("VELUNE_PLAYBACK_TRACE") == 1)
                qInfo() << "velune.native player rejected" << client.name
                        << response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                        << int(response->error());
            // Android includes the visitor context on player calls. On a cold
            // desktop launch, the first response may be what establishes it.
            if (!hadVisitor && !visitorData.isEmpty() && !visitorRetried) {
                visitorRetried = true;
                --clientIndex;
            }
            nextClient();
            return;
        }
        const auto data = object["streamingData"].toObject();
        const auto duration =
            object["videoDetails"].toObject()["lengthSeconds"].toString().toLongLong() * 1000;
        QList<QJsonObject> formats;
        for (const auto &value : data["adaptiveFormats"].toArray()) {
            const auto f = value.toObject();
            // Android's preview check: do not substitute a short preview for a full song.
            const auto approx = f["approxDurationMs"].toVariant().toLongLong();
            if (duration >= 90000 && approx > 0 && approx <= qMin(qint64(90000), duration * 9 / 10))
                continue;
            if (f["mimeType"].toString().startsWith("audio/") && f["bitrate"].toInt() > 0 &&
                supported(QUrl(f["url"].toString())))
                formats.append(f);
        }
        // Match Android HIGH: prefer the best audio at/below 160 kbps, otherwise the lowest above.
        std::sort(formats.begin(), formats.end(), [](const auto &a, const auto &b) {
            const int x = a["bitrate"].toInt(), y = b["bitrate"].toInt();
            if ((x <= 160000) != (y <= 160000))
                return x <= 160000;
            // Qt 6.4 exposes reliable duration/range seeking for direct M4A;
            // some live WebM streams report no duration until the file ends.
            const bool am = a["mimeType"].toString().startsWith("audio/mp4");
            const bool bm = b["mimeType"].toString().startsWith("audio/mp4");
            if (am != bm)
                return am;
            return x <= 160000 ? x > y : x < y;
        });
        if (formats.isEmpty()) {
            if (qEnvironmentVariableIntValue("VELUNE_PLAYBACK_TRACE") == 1)
                qInfo() << "velune.native no direct full-length audio" << client.name;
            nextClient();
            return;
        }
        const QUrl url(formats.first()["url"].toString());
        bool ok = false;
        auto lifetime = data["expiresInSeconds"].toVariant().toLongLong(&ok);
        if (!ok || lifetime <= 60) {
            nextClient();
            return;
        }
        const auto expiry = QUrlQuery(url).queryItemValue("expire").toLongLong();
        if (expiry > 0)
            lifetime = qMin(lifetime, expiry - QDateTime::currentSecsSinceEpoch());
        if (lifetime <= 60) {
            nextClient();
            return;
        }
        QNetworkRequest probe(url);
        probe.setRawHeader("User-Agent", client.agent.toUtf8());
        probe.setRawHeader("Range", "bytes=0-0");
        probe.setRawHeader("Accept-Encoding", "identity");
        probe.setTransferTimeout(1500);
        probe.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto check = transport->get(probe);
        reply = check;
        check->setReadBufferSize(1024);
        // Complete on the first byte; a server ignoring Range must not download the whole song.
        auto complete = [this, check, url, client, duration, lifetime, ticket] {
            if (ticket != generation || reply != check)
                return;
            const auto status = check->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool valid = (status == 200 || status == 206) && check->bytesAvailable() > 0;
            if (!valid && !check->isFinished())
                return;
            reply = nullptr;
            check->disconnect(this);
            check->abort();
            check->deleteLater();
            if (!valid) {
                if (qEnvironmentVariableIntValue("VELUNE_PLAYBACK_TRACE") == 1)
                    qInfo() << "velune.native probe rejected" << client.name << status;
                nextClient();
                return;
            }
            running = false;
            deadline.stop();
            if (persist)
                QSettings().setValue("playback/lastNativeClient", client.name);
            emit ready(url, {{"User-Agent", client.agent}}, duration, lifetime - 60);
        };
        connect(check, &QNetworkReply::readyRead, this, complete);
        connect(check, &QNetworkReply::finished, this, complete);
    });
}
} // namespace velune
