#pragma once
#include "SearchEngine.h"
#include "core/Library.h"
#include <QElapsedTimer>
namespace velune {
// Port of LibraryScreen, LibraryMixViewModel and per-type Android library filters.
class LibraryCatalog : public QObject {
    Q_OBJECT
  public:
    LibraryCatalog(Library *library, SearchEngine::Transport transport, QObject *parent = nullptr);
    ~LibraryCatalog() override;
    QJsonObject preferences() const {
        return prefs;
    }
    void setPreferences(QJsonObject patch);
    QJsonArray entries(QString section = "all", QString scope = "liked", QString sort = "added",
                       bool descending = true, QJsonObject contentSettings = {}) const;
    QJsonObject statistics() const;
    bool bookmarked(QString id) const;
    void bookmark(QJsonObject entry, bool value);
    QStringList tags() const;
    void setTags(QString id, QStringList tags);
    void setPlaylistOrder(QStringList ids);
    void refresh(QStringList kinds);
    void cancel();
    bool busy() const {
        return refreshing;
    }
    static QJsonObject parsePage(QJsonObject response);
    void observePlayback(QJsonObject state, bool localOutput);
    void clearListeningHistory();
    void checkpoint() { flushListen(); }
  signals:
    void changed();
    void busyChanged(bool busy);
    void message(QString message);

  private:
    Library *library;
    SearchEngine::Transport transport;
    QJsonObject prefs, saved;
    QJsonArray remoteEntries;
    QJsonObject overrides, playlistTags;
    QList<QPointer<QNetworkReply>> requests;
    quint64 generation = 0;
    bool refreshing = false;
    QElapsedTimer clock;
    Track listening;
    qint64 pendingTime = 0;
    bool wasPlaying = false;
    bool save();
    QJsonArray entities() const;
    QJsonArray songs() const;
    QJsonArray history() const;
    void syncNext(QStringList kinds, int index, quint64 ticket);
    void syncPage(QString kind, QString token, QJsonArray accumulated, QSet<QString> seen, QStringList kinds,
                  int index, quint64 ticket);
    void flushListen();
    void hydrateAlbums(QStringList kinds, int index, int cursor, quint64 ticket);
    void albumTracks(QString playlistId, QJsonObject album, QString token, QJsonArray tracks,
                     QSet<QString> seen, QStringList kinds, int index, int cursor, quint64 ticket);
};
} // namespace velune
