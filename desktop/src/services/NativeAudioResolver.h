#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace velune {
// Native equivalent of Android's player request / audio selection / range probe.
// Ciphered URLs and account-restricted tracks fall back to AudioResolver's helper.
class NativeAudioResolver : public QObject {
    Q_OBJECT
  public:
    explicit NativeAudioResolver(QObject *parent = nullptr, QNetworkAccessManager *transport = nullptr);
    ~NativeAudioResolver() override {
        cancel();
    }
    void resolve(const QString &videoId);
    void cancel();
    bool busy() const {
        return running;
    }
    static void setVisitorData(const QString &value);
  signals:
    void ready(QUrl url, QJsonObject headers, qint64 duration, qint64 lifetime);
    void unavailable();

  private:
    QNetworkAccessManager network;
    QNetworkAccessManager *transport;
    QPointer<QNetworkReply> reply;
    QTimer deadline;
    QString videoId, visitorData;
    QStringList clients;
    int clientIndex = 0;
    quint64 generation = 0;
    bool running = false, persist, visitorRetried = false;
    void nextClient();
    void finishUnavailable();
};
} // namespace velune
