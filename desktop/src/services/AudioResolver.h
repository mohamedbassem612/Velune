#pragma once
#include "NativeAudioResolver.h"
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <memory>
namespace velune {
class AudioResolver : public QObject {
    Q_OBJECT
  public:
    explicit AudioResolver(QObject *parent = nullptr, QString executable = {}, QString runtime = {},
                           QNetworkAccessManager *transport = nullptr);
    ~AudioResolver() override;
    void resolve(const QString &videoId, const QByteArray &cookies = {}, bool thorough = false);
    // Cancels metadata resolution; audio streams are owned separately.
    void cancel();
    bool busy() const {
        return direct.busy() || (process && process->state() != QProcess::NotRunning);
    }
    bool matches(const QString &id, const QByteArray &cookies = {}) const;
    static QString toolPath(const QString &name);
  signals:
    void cacheHit();
    void stage(QString name);
    void ready(QUrl audio, QJsonObject headers, qint64 duration);
    void error(QString message);

  private:
    QProcess *process = nullptr;
    NativeAudioResolver direct;
    bool useDirect;
    void publish(const QUrl &url, const QJsonObject &headers, qint64 duration, qint64 lifetime);
    void createProcess();
    QTimer timeout;
    QString executable, runtime, cacheKey, videoId;
    QByteArray accountCookies;
    bool fastAttempt = true;
    void launch(bool fast);
    QByteArray output, diagnostic;
    std::unique_ptr<QTemporaryDir> work;
    void failed(QString message);
};
} // namespace velune
