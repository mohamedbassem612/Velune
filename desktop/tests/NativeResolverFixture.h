#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>

class NativeFixtureReply : public QNetworkReply {
    QByteArray data;
    bool aborted = false;

  public:
    NativeFixtureReply(QNetworkRequest request, QByteArray bytes, int status, int delay, QObject *parent)
        : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(delay, this, [this, bytes, status] {
            if (aborted)
                return;
            data = bytes;
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
            if (status >= 400)
                setError(ContentAccessDenied, "Fixture rejected request");
            emit metaDataChanged();
            if (!data.isEmpty())
                emit readyRead();
            if (!aborted) {
                setFinished(true);
                emit finished();
            }
        });
    }
    void abort() override {
        aborted = true;
        setFinished(true);
    }
    qint64 bytesAvailable() const override {
        return data.size() + QNetworkReply::bytesAvailable();
    }
    qint64 readData(char *destination, qint64 maximum) override {
        const auto count = qMin(qint64(data.size()), maximum);
        if (!count)
            return -1;
        memcpy(destination, data.constData(), size_t(count));
        data.remove(0, count);
        return count;
    }
};
class NativeFixtureNetwork : public QNetworkAccessManager {
  public:
    QList<QNetworkRequest> requests;
    QList<QJsonObject> playerBodies;
    int delay = 0, firstProbeStatus = 206, probes = 0;
    QString source = "https://fixture.googlevideo.com/audio";
    int lifetime = 3600;
    int duration = 180000;
    bool requireVisitor = false;
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *body) override {
        requests.append(request);
        if (op == PostOperation) {
            playerBodies.append(QJsonDocument::fromJson(body->readAll()).object());
            QJsonObject object{
                {"playabilityStatus", QJsonObject{{"status", "OK"}}},
                {"videoDetails", QJsonObject{{"lengthSeconds", "180"}}},
                {"streamingData",
                 QJsonObject{{"expiresInSeconds", QString::number(lifetime)},
                             {"adaptiveFormats",
                              QJsonArray{QJsonObject{{"mimeType", "audio/mp4"},
                                                     {"bitrate", 128000},
                                                     {"approxDurationMs", QString::number(duration)},
                                                     {"url", source}}}}}}};
            if (requireVisitor) {
                object["responseContext"] = QJsonObject{{"visitorData", "fixture-visitor"}};
                if (request.rawHeader("X-Goog-Visitor-Id").isEmpty()) {
                    object["playabilityStatus"] = QJsonObject{{"status", "UNPLAYABLE"}};
                    object.remove("streamingData");
                }
            }
            return new NativeFixtureReply(request, QJsonDocument(object).toJson(), 200, delay, this);
        }
        return new NativeFixtureReply(request, "x", probes++ == 0 ? firstProbeStatus : 206, 0, this);
    }
};
