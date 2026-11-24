#pragma once
#include "AudioResolver.h"
#include "core/Library.h"
#include <QFile>
#include <QPointer>
#include <QSaveFile>
#include <memory>
namespace velune {
class ConnectMediaProvider : public QObject {
    Q_OBJECT
  public:
    using Reply = std::function<void(QJsonObject)>;
    ConnectMediaProvider(Library *, QObject *);
    ~ConnectMediaProvider() override;
    void serve(QString peer, QJsonObject, Reply);
    void disconnectPeer(QString);
    bool busy() const { return !fills.isEmpty() || !readers.isEmpty() || !pending.isEmpty(); }
    void setCookies(std::function<QByteArray()> fn) {
        cookies = std::move(fn);
    }

  private:
    struct Reader {
        QString peer, request;
        std::shared_ptr<QFile> file;
        qint64 remaining;
    };
    struct Fill {
        QString song;
        AudioResolver *resolver = nullptr;
        QPointer<QNetworkReply> reply;
        QUrl url;
        QJsonObject headers;
        std::unique_ptr<QSaveFile> file;
        qint64 received = 0, total = -1;
        QList<std::function<void(QString)>> done;
    };
    Library *library;
    QNetworkAccessManager network;
    QMap<QString, Reader> readers;
    QSet<QString> pending;
    QMap<QString, std::shared_ptr<Fill>> fills;
    std::function<QByteArray()> cookies;
    QString stem(QString) const;
    QString cached(QString) const;
    void complete(QString, std::function<void(QString)>);
    void chunk(std::shared_ptr<Fill>);
    void finish(std::shared_ptr<Fill>, bool);
    void trim();
};
} // namespace velune
