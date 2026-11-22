#include "services/AudioStream.h"
#include <QNetworkReply>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
using namespace velune;
class MediaReply : public QNetworkReply {
    QByteArray data;

  public:
    MediaReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this, request] {
            const bool ranged = request.rawHeader("Range") == "bytes=4-7";
            data = ranged ? "4567" : "0123456789";
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, ranged ? 206 : 200);
            setRawHeader("Content-Type", "audio/mp4");
            setRawHeader("Content-Length", QByteArray::number(data.size()));
            if (ranged)
                setRawHeader("Content-Range", "bytes 4-7/10");
            emit metaDataChanged();
            emit readyRead();
            setFinished(true);
            emit finished();
        });
    }
    void abort() override {}
    qint64 bytesAvailable() const override {
        return data.size() + QNetworkReply::bytesAvailable();
    }
    qint64 readData(char *destination, qint64 maximum) override {
        const auto size = qMin(maximum, qint64(data.size()));
        if (!size)
            return -1;
        memcpy(destination, data.constData(), size_t(size));
        data.remove(0, size);
        return size;
    }
};
class MediaNetwork : public QNetworkAccessManager {
  public:
    QList<QNetworkRequest> requests;

  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        requests.append(request);
        return new MediaReply(request, this);
    }
};
class AudioStreamTests : public QObject {
    Q_OBJECT
  private slots:
    void queuedErrorsKeepTheirOriginalRequestIdentity() {
        AudioStream stream;
        QSignalSpy errors(&stream, &AudioStream::requestError);
        QVERIFY(stream.start(QUrl("http://invalid.test/audio"), {}, 41).isEmpty());
        const auto source = stream.start(QUrl("https://fixture.googlevideo.com/audio"), {}, 42);
        QVERIFY(!source.isEmpty());
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(errors[0][0].toULongLong(), 41ULL);
        stream.stop();
    }
    void loopbackRangeRequestsAndCredentialIsolation() {
        MediaNetwork network;
        AudioStream stream(nullptr, &network);
        QVERIFY(stream.start(QUrl("http://fixture.googlevideo.com/audio"), {}).isEmpty());
        const auto local =
            stream.start(QUrl("https://fixture.googlevideo.com/audio"),
                         {{"User-Agent", "VeluneTest"}, {"Cookie", "secret"}, {"Authorization", "secret"}});
        QCOMPARE(local.host(), QString("127.0.0.1"));
        QTcpSocket client;
        client.connectToHost(local.host(), local.port());
        QVERIFY(client.waitForConnected(2000));
        client.write("GET " + local.path().toLatin1() + " HTTP/1.1\r\nHost: 127.0.0.1:" +
                     QByteArray::number(local.port()) + "\r\nRange: bytes=4-7\r\n\r\n");
        QByteArray response;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     response += client.readAll();
                                     return response.endsWith("4567");
                                 })(),
                                 3000);
        QVERIFY(response.startsWith("HTTP/1.1 206"));
        QVERIFY(response.contains("Content-Range: bytes 4-7/10"));
        QList<QNetworkRequest> requests;
        QMetaObject::invokeMethod(
            &network, [&] { requests = network.requests; }, Qt::BlockingQueuedConnection);
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests[0].rawHeader("Range"), QByteArray("bytes=4-7"));
        QCOMPARE(requests[0].rawHeader("User-Agent"), QByteArray("VeluneTest"));
        QVERIFY(!requests[0].hasRawHeader("Cookie"));
        QVERIFY(!requests[0].hasRawHeader("Authorization"));
        stream.stop();
        QTcpSocket closed;
        closed.connectToHost(local.host(), local.port());
        QVERIFY(!closed.waitForConnected(500));
    }
};
QTEST_MAIN(AudioStreamTests)
#include "AudioStreamTests.moc"
