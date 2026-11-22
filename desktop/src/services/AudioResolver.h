#pragma once
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
    explicit AudioResolver(QObject *parent = nullptr, QString executable = {}, QString runtime = {});
    ~AudioResolver() override;
    void resolve(const QString &videoId, const QByteArray &cookies = {}, bool thorough = false);
    // Cancels metadata resolution; audio streams are owned separately.
    void cancel();
    bool busy() const {
        return process && process->state() != QProcess::NotRunning;
    }
    static QString toolPath(const QString &name);
  signals:
    void cacheHit();
    void ready(QUrl audio, QJsonObject headers, qint64 duration);
    void error(QString message);

  private:
    QProcess *process = nullptr;
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
