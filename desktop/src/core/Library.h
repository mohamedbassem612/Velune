#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
namespace velune {
struct Track {
    QString id, title, artist, thumbnail;
    int duration = 0;
    QString album, albumId;
    bool explicitContent = false, musicVideo = false;
    QJsonArray artistRefs;
    QJsonObject wire() const;
    static Track fromWire(const QJsonObject &json);
};
class Library : public QObject {
    Q_OBJECT
  public:
    explicit Library(QString directory, QObject *parent = nullptr);
    QString deviceId() const {
        return data.value("device").toString();
    }
    QJsonObject snapshot() const {
        return data;
    }
    QList<Track> tracks(bool likedOnly = false) const;
    bool liked(const QString &id) const;
    void setLiked(const Track &track, bool liked);
    bool merge(const QJsonObject &remote);
    void importTracks(const QList<Track> &tracks, bool liked = true);
    void rememberTracks(const QList<Track> &tracks, bool newTracksLiked = false);
    QString putPlaylist(QString id, QString name, const QList<Track> &tracks, QString browseId = {},
                        QString thumbnail = {});
    bool removePlaylist(QString id);
    void recordListening(const Track &track, qint64 milliseconds, QString timestamp);
    QString mediaPath(const QString &id) const;
    void registerMedia(const Track &track, const QString &path);
    QString directory() const {
        return root;
    }
  signals:
    void changed();
    void error(QString message);

  private:
    QString root;
    QJsonObject data, files;
    bool save();
};
bool newerVersion(const QJsonObject &a, const QJsonObject &b);
} // namespace velune
