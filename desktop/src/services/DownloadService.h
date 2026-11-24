#pragma once
#include "AudioResolver.h"
#include "Mp3Tags.h"
#include <QDirIterator>
#include <QFileSystemWatcher>
#include <QLockFile>
#include <QPointer>
#include <QSaveFile>
#include <QTemporaryDir>
namespace velune {
class DownloadService : public QObject {
    Q_OBJECT
  public:
    explicit DownloadService(Library *, QObject *parent = nullptr,
                             QNetworkAccessManager *transport = nullptr);
    ~DownloadService() override;
    void setCookies(std::function<QByteArray()> fn) {
        cookies = std::move(fn);
    }
    QJsonObject state() const;
    bool busy() const { return !active.id.isEmpty() || !pending.isEmpty(); }
    bool setFolder(const QString &path);
    void scan();
    void enqueue(const QList<Track> &tracks);
    void cancel(const QString &id);
    bool contains(const QString &id) const;
    QString cover(const QString &id) const;
  signals:
    void changed();
    void message(QString);
    void folderRequired();

  private:
    struct Saved {
        Track track;
        QString path;
        qint64 size;
    };
    Library *library;
    QString folder, stage, errorText;
    QMap<QString, Saved> saved, found;
    QHash<QString, Track> knownTracks;
    QList<Track> pending;
    QMap<QString, Track> failed;
    Track active;
    AudioResolver resolver;
    QNetworkAccessManager network;
    QNetworkAccessManager *transport;
    QPointer<QNetworkReply> reply;
    QPointer<QProcess> converter;
    QTimer scanTick, timeout, rescanTimer;
    QFileSystemWatcher watcher;
    std::unique_ptr<QDirIterator> iterator;
    std::unique_ptr<QLockFile> lock;
    std::unique_ptr<QTemporaryDir> work;
    std::unique_ptr<QSaveFile> sourceFile;
    QUrl sourceUrl;
    QJsonObject sourceHeaders;
    qint64 received = 0, total = -1;
    int scanned = 0;
    std::function<QByteArray()> cookies;
    QString sourcePath() const;
    QString cacheSource(const QString &) const;
    void scanStep();
    void startNext();
    void fetchChunk();
    void convert(const QString &input);
    void fetchCover(const QString &input);
    void runConverter(const QString &input, bool withCover);
    void finish(bool success, QString error = {});
    void stopWork();
    void remember(const Mp3Info &, const QString &path, QMap<QString, Saved> &into);
};
} // namespace velune
