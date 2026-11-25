#pragma once
#include "services/BackupService.h"
#include "services/Discovery.h"
#include "services/MusicService.h"
#include <QVariantMap>
namespace velune {
class DesktopController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
  public:
    DesktopController(Library *, MusicService *, ConnectClient *, Discovery *, QObject *parent = nullptr);
    QVariantMap state() const {
        return view.toVariantMap();
    }
    Q_INVOKABLE void action(QString name, QVariantMap args = {});
    Q_INVOKABLE QString artwork(QVariantMap track) const;
  signals:
    void changed();
    void notification(QString text);
    void downloadFolderRequired();
    void restorePreviewReady();

  private:
    Library *library;
    MusicService *music;
    ConnectClient *connectClient;
    Discovery *discovery;
    BackupService *backup;
    QJsonObject view;
    QString section = "songs", scope = "all", sort = "added", query, filter = "songs", source = "online";
    bool descending = true;
    QJsonArray display;
    QJsonArray decorate(QJsonArray) const;
    void refresh();
    void search();
    void open(QJsonObject);
    QList<Track> tracks(QJsonArray) const;
};
} // namespace velune
