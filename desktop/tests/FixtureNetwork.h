#pragma once
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <functional>
// Test-only CDN transport. Production AudioStream validation and HTTP relay are unchanged.
class FixtureReply final : public QNetworkReply {
    QByteArray data;
    qint64 offset = 0, released = 0;
    QTimer *producer = nullptr;

  public:
    FixtureReply(QNetworkAccessManager::Operation operation, const QNetworkRequest &request, QByteArray bytes,
                 int delay, QObject *parent, int chunkBytes = 0, int chunkMs = 50,
                 std::function<bool()> blocked = {})
        : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(
            delay, this, [this, operation, request, bytes = std::move(bytes), chunkBytes, chunkMs, blocked] {
                if (isFinished())
                    return;
                qint64 first = 0, last = bytes.size() - 1;
                const auto range = request.rawHeader("Range");
                const auto match =
                    QRegularExpression("^bytes=([0-9]*)-([0-9]*)$").match(QString::fromLatin1(range));
                int status = 200;
                if (match.hasMatch()) {
                    status = 206;
                    if (match.captured(1).isEmpty())
                        first = qMax(qint64(0), qint64(bytes.size()) - match.captured(2).toLongLong());
                    else
                        first = match.captured(1).toLongLong();
                    if (!match.captured(1).isEmpty() && !match.captured(2).isEmpty())
                        last = qMin(last, match.captured(2).toLongLong());
                    if (first > last)
                        status = 416;
                }
                setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
                setRawHeader("Content-Type", "application/octet-stream");
                if (status == 206)
                    setRawHeader("Content-Range", "bytes " + QByteArray::number(first) + "-" +
                                                      QByteArray::number(last) + "/" +
                                                      QByteArray::number(bytes.size()));
                if (status == 416)
                    setRawHeader("Content-Range", "bytes */" + QByteArray::number(bytes.size()));
                const auto body = status == 416 ? QByteArray() : bytes.mid(first, last - first + 1);
                setRawHeader("Content-Length", QByteArray::number(body.size()));
                if (operation != QNetworkAccessManager::HeadOperation)
                    data = body;
                emit metaDataChanged();
                if (chunkBytes > 0 && !data.isEmpty()) {
                    producer = new QTimer(this);
                    producer->setTimerType(Qt::PreciseTimer);
                    connect(producer, &QTimer::timeout, this, [this, chunkBytes, blocked] {
                        if (blocked && blocked())
                            return;
                        released = qMin(qint64(data.size()), released + chunkBytes);
                        emit readyRead();
                        if (released == data.size()) {
                            producer->stop();
                            setFinished(true);
                            emit finished();
                        }
                    });
                    producer->start(chunkMs);
                    return;
                }
                released = data.size();
                if (!data.isEmpty())
                    emit readyRead();
                setFinished(true);
                emit finished();
            });
    }
    void abort() override {
        if (!isFinished()) {
            if (producer)
                producer->stop();
            setError(OperationCanceledError, "Fixture cancelled");
            setFinished(true);
            emit finished();
        }
    }
    qint64 bytesAvailable() const override {
        return released - offset + QNetworkReply::bytesAvailable();
    }
    qint64 readData(char *destination, qint64 maximum) override {
        const auto size = qMin(maximum, released - offset);
        if (!size)
            return isFinished() ? -1 : 0;
        memcpy(destination, data.constData() + offset, size_t(size));
        offset += size;
        return size;
    }
};
class FixtureNetwork final : public QNetworkAccessManager {
  public:
    QByteArray bytes;
    int delay = 0;
    int chunkBytes = 0, chunkMs = 50;
    QElapsedTimer clock;
    qint64 blockedUntil = 0;
    FixtureNetwork() {
        clock.start();
    }
    // Called on the transport's worker thread, including all current/future range replies.
    void blockFor(int milliseconds) {
        blockedUntil = clock.elapsed() + milliseconds;
    }

  protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *) override {
        return new FixtureReply(op, request, bytes, delay, this, chunkBytes, chunkMs,
                                [this] { return clock.elapsed() < blockedUntil; });
    }
};
