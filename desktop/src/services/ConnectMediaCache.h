#pragma once
#include "ConnectClient.h"
#include <QSaveFile>
#include <memory>
namespace velune {
// A completed peer encoding has its own cache; it never overwrites imported audio.
class ConnectMediaCache : public QObject {
    Q_OBJECT
  public:
    ConnectMediaCache(Library *, ConnectClient *, QObject *parent);
    ~ConnectMediaCache() override;
    QString path(QString song) const;
    QString cover(QString song) const;
    QString lyrics(QString song) const;
    void fetch(const Track &, std::function<void(QString, QString)> done);
    void fetchAssets(const Track &);
    void cancel();
    bool busy() const {
        return bool(current) || !assetPending.isEmpty();
    }
  signals:
    void assetsChanged(QString song);

  private:
    struct Fetch {
        Track track;
        QStringList providers;
        QString provider, handle, request;
        int candidate = 0;
        bool cachedOnly = true;
        qint64 expected = -1, received = 0;
        std::unique_ptr<QSaveFile> file;
        std::function<void(QString, QString)> done;
    };
    Library *library;
    ConnectClient *remote;
    QString root;
    std::shared_ptr<Fetch> current;
    QSet<QString> assetPending;
    QString stem(QString song) const;
    void openNext(const std::shared_ptr<Fetch> &);
    void readNext(const std::shared_ptr<Fetch> &);
    void finish(const std::shared_ptr<Fetch> &, QString error = {});
    void close(const std::shared_ptr<Fetch> &);
    void trim();
};
} // namespace velune
