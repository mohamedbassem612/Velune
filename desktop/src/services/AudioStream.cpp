#include "AudioStream.h"
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <memory>
namespace velune {
class AudioStreamWorker : public QObject {
    Q_OBJECT
  public:
    QTcpServer server;
    QNetworkAccessManager network;
    QNetworkAccessManager *transport;
    QUrl source;
    QByteArray path;
    quint64 request = 0;
    QJsonObject headers;
    explicit AudioStreamWorker(QNetworkAccessManager *manager) : transport(manager ? manager : &network) {
        server.setParent(this);
        network.setParent(this);
        connect(&server, &QTcpServer::newConnection, this, &AudioStreamWorker::accept);
    }
    QUrl start(const QUrl &url, const QJsonObject &values, quint64 id);
    void stop();
    void accept();
  signals:
    void error(quint64 request, QString message);
};
AudioStream::AudioStream(QObject *parent, QNetworkAccessManager *manager)
    : QObject(parent), worker(new AudioStreamWorker(manager)) {
    if (manager)
        manager->moveToThread(&thread);
    worker->moveToThread(&thread);
    connect(worker, &AudioStreamWorker::error, this, [this](quint64 id, QString message) {
        emit requestError(id, message);
    });
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    thread.start();
}
AudioStream::~AudioStream() {
    stop();
    // Optional injected transports belong to the caller and return to its thread.
    QMetaObject::invokeMethod(
        worker,
        [this] {
            if (worker->transport != &worker->network)
                worker->transport->moveToThread(QObject::thread());
        },
        Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
}
QUrl AudioStream::start(const QUrl &url, const QJsonObject &headers, quint64 request) {
    QUrl result;
    QMetaObject::invokeMethod(
        worker, [&] { result = worker->start(url, headers, request); }, Qt::BlockingQueuedConnection);
    return result;
}
void AudioStream::stop() {
    QMetaObject::invokeMethod(worker, [this] { worker->stop(); }, Qt::BlockingQueuedConnection);
}
void AudioStreamWorker::stop() {
    server.close();
    path.clear();
    source.clear();
    headers = {};
    for (auto socket : server.findChildren<QTcpSocket *>()) {
        socket->abort();
        socket->deleteLater();
    }
}
QUrl AudioStreamWorker::start(const QUrl &url, const QJsonObject &values, quint64 id) {
    stop();
    request = id;
    if (url.scheme() != "https" || url.port(443) != 443 ||
        !(url.host() == "googlevideo.com" || url.host().endsWith(".googlevideo.com"))) {
        emit error(request, "YouTube did not return a supported audio source.");
        return {};
    }
    source = url;
    headers = values;
    path = "/audio/" + QUuid::createUuid().toByteArray(QUuid::Id128);
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        emit error(request, "Could not start the local audio stream.");
        return {};
    }
    return QUrl(QString("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(QString::fromLatin1(path)));
}
void AudioStreamWorker::accept() {
    while (server.hasPendingConnections()) {
        auto socket = server.nextPendingConnection();
        if (!socket->peerAddress().isLoopback() || server.findChildren<QTcpSocket *>().size() > 8) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        struct State {
            QByteArray input;
            QPointer<QNetworkReply> reply;
            bool requested = false, sent = false, finished = false;
        };
        auto state = std::make_shared<State>();
        auto reject = [socket](int status) {
            socket->write("HTTP/1.1 " + QByteArray::number(status) +
                          " Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
        };
        connect(socket, &QTcpSocket::disconnected, socket, [socket, state] {
            if (state->reply) {
                state->reply->abort();
                state->reply->deleteLater();
            }
            socket->deleteLater();
        });
        QTimer::singleShot(5000, socket, [socket, state] {
            if (!state->requested)
                socket->abort();
        });
        connect(
            socket, &QTcpSocket::readyRead, socket, [this, socket, state, reject, streamRequest = request] {
                if (state->requested)
                    return;
                state->input += socket->readAll();
                if (state->input.size() > 16384) {
                    reject(413);
                    return;
                }
                if (!state->input.contains("\r\n\r\n"))
                    return;
                state->requested = true;
                auto lines = state->input.split('\n');
                auto first = lines.value(0).trimmed().split(' ');
                if (first.size() != 3 || (first[0] != "GET" && first[0] != "HEAD") || first[1] != path) {
                    reject(404);
                    return;
                }
                QByteArray range, host;
                for (auto line : lines) {
                    if (line.toLower().startsWith("range:"))
                        range = line.mid(6).trimmed();
                    if (line.toLower().startsWith("host:"))
                        host = line.mid(5).trimmed();
                }
                if (host != "127.0.0.1:" + QByteArray::number(server.serverPort()) ||
                    (!range.isEmpty() && !QRegularExpression("^bytes=(?:[0-9]+-[0-9]*|-[0-9]+)$")
                                              .match(QString::fromLatin1(range))
                                              .hasMatch())) {
                    reject(400);
                    return;
                }
                QNetworkRequest request(source);
                request.setTransferTimeout(20000);
                request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
                request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                     QNetworkRequest::ManualRedirectPolicy);
                for (const auto &name :
                     {QString("User-Agent"), QString("Referer"), QString("Origin"), QString("Accept"),
                      QString("Accept-Language"), QString("Sec-Fetch-Mode")}) {
                    const auto value = headers.value(name).toString().toUtf8();
                    if (!value.isEmpty() && !value.contains('\r') && !value.contains('\n'))
                        request.setRawHeader(name.toLatin1(), value);
                }
                request.setRawHeader("Accept-Encoding", "identity");
                if (!range.isEmpty())
                    request.setRawHeader("Range", range);
                auto reply = first[0] == "HEAD" ? transport->head(request) : transport->get(request);
                state->reply = reply;
                reply->setParent(socket);
                reply->setReadBufferSize(256 * 1024);
                auto sendHeaders = [this, socket, state, reply, reject, streamRequest] {
                    if (state->sent || socket->state() != QAbstractSocket::ConnectedState)
                        return;
                    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    if (!code)
                        return;
                    state->sent = true;
                    if (code != 200 && code != 206 && code != 416) {
                        qWarning("Audio CDN returned HTTP %d (network error %d)", code, int(reply->error()));
                        reject(502);
                        emit error(streamRequest, "YouTube rejected this audio stream. Try the song again.");
                        return;
                    }
                    QByteArray head = "HTTP/1.1 " + QByteArray::number(code) + " OK\r\n";
                    for (auto name : {"Content-Type", "Content-Length", "Content-Range"}) {
                        const auto value = reply->rawHeader(name);
                        if (!value.isEmpty() && !value.contains('\r') && !value.contains('\n'))
                            head += QByteArray(name) + ": " + value + "\r\n";
                    }
                    head += "Accept-Ranges: bytes\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
                    socket->write(head);
                };
                auto drain = [socket, state, reply] {
                    if (socket->state() != QAbstractSocket::ConnectedState || !state->sent)
                        return;
                    while (reply->bytesAvailable() > 0 && socket->bytesToWrite() < 256 * 1024)
                        socket->write(reply->read(64 * 1024));
                    if (state->finished && !reply->bytesAvailable())
                        socket->disconnectFromHost();
                };
                connect(reply, &QNetworkReply::metaDataChanged, socket, sendHeaders);
                connect(reply, &QNetworkReply::readyRead, socket, [sendHeaders, drain] {
                    sendHeaders();
                    drain();
                });
                connect(socket, &QTcpSocket::bytesWritten, socket, [drain](qint64) { drain(); });
                connect(reply, &QNetworkReply::finished, socket, [this, state, reply, reject, sendHeaders, drain, streamRequest] {
                    state->finished = true;
                    sendHeaders();
                    if (!state->sent) {
                        qWarning("Audio stream failed before headers (network error %d)",
                                 int(reply->error()));
                        state->sent = true;
                        reject(502);
                        emit error(streamRequest, "The audio connection failed. Try the song again.");
                        return;
                    }
                    drain();
                });
            });
    }
}
} // namespace velune

#include "AudioStream.moc"
