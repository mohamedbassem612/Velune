#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTcpServer>
#include <QThread>
#include <QUrl>
namespace velune {
class AudioStreamWorker;
// Loopback HTTP relay preserves byte-range requests and required media headers.
class AudioStream : public QObject {
    Q_OBJECT
  public:
    explicit AudioStream(QObject *parent = nullptr, QNetworkAccessManager *transport = nullptr);
    ~AudioStream() override;
    QUrl start(const QUrl &source, const QJsonObject &headers, quint64 request = 0);
    void stop();
  signals:
    void requestError(quint64 request, QString message);

  private:
    QThread thread;
    AudioStreamWorker *worker;
};
} // namespace velune
