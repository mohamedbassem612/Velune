#pragma once
#include "Components.h"
#include "services/Discovery.h"
#include "services/MusicService.h"
#include <QComboBox>
#include <QGraphicsBlurEffect>
#include <QGridLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>
namespace velune {
class MainWindow : public QWidget {
    Q_OBJECT
  public:
    MainWindow(Library *library, MusicService *music, ConnectClient *connect, Discovery *discovery,
               bool preview = false);
    void setPreview(bool value);

  protected:
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

  private:
    Library *library;
    MusicService *music;
    ConnectClient *remote;
    Discovery *discovery;
    QWidget *shell, *sidebar, *titlebar, *home, *player, *gate;
    QStackedWidget *pages;
    QGridLayout *homeGrid;
    QList<NavButton *> nav;
    QList<QuickTile *> quick;
    QList<SongCard *> cards;
    QLabel *resultsHeading, *elapsed, *duration, *cover, *notice;
    QLabel *loginHint, *accountLabel;
    QPushButton *loginButton, *settingsSignIn;
    ElidedLabel *title, *artist;
    IconButton *play, *shuffleButton, *repeatButton, *volumeButton;
    LikeButton *favorite;
    QSlider *seekBar, *volumeSlider;
    QLineEdit *searchInput;
    QTimer *searchDebounce;
    QLabel *searchStatus;
    QListWidget *searchSuggestions;
    QComboBox *searchFilter, *searchScope;
    QPushButton *searchMore;
    bool fullSearch = false;
    QJsonObject searchPage;
    void submitSearch(bool suggestionsOnly);
    void renderSearchPage(QJsonObject page, bool append);
    void showSearchPreferences();
    void openSearchResult(const QJsonObject &entry);
    QListWidget *searchResults, *libraryList, *devicesList;
    QGraphicsBlurEffect *blur;
    QList<Track> results;
    QJsonObject playback;
    QNetworkAccessManager artworkNetwork;
    bool preview = false;
    int repeatMode = 0;
    int previousVolume = 75;
    QWidget *makeCollection();
    void openQuick(int index);
    void playQuick(int index);
    void openTrack(const Track &track);
    void showCollection(QString name, QString description, QList<Track> tracks, bool liked = false,
                        QString kind = "PLAYLIST");
    void renderCollection();
    void refreshCollectionLikes();
    void playCollectionTrack(QString id);
    void loadVisibleArtwork();
    void populateTrackList(QListWidget *list, const QList<Track> &tracks);
    void loadListArtwork(QListWidget *list);
    void loadArtwork(QString url, QObject *owner, std::function<void(QPixmap)> apply);
    QPixmap collectionArtwork(const QList<Track> &tracks, bool liked) const;
    QList<Track> quickTracks(int index) const;
    QList<Track> mixes[2];
    QHash<QString, QPixmap> artworkCache;
    QHash<QString, QNetworkReply *> artworkRequests;
    QWidget *collectionPage;
    QLabel *collectionCover, *collectionTitle, *collectionSubtitle, *collectionCount, *collectionKind;
    QVBoxLayout *collectionRows;
    QList<Track> collectionTracks;
    QString collectionName, collectionDescription, pendingCollection;
    bool collectionLiked = false;
    int collectionQuick = -1;
    int returnPage = 0;
    struct CollectionState {
        QString name, description, kind;
        QList<Track> tracks;
        bool liked;
        int quick;
    };
    QList<CollectionState> collectionHistory;
    QWidget *makeHome();
    QWidget *makePlayer();
    QWidget *makeResults();
    QWidget *makeLibrary();
    void renderLibrary();
    void setLibrarySection(QString section);
    void setLibraryOptions(QJsonObject changes);
    void libraryContextMenu(QListWidget *list, QPoint position);
    void showLibraryPreferences();
    void addSongToPlaylist(const Track &track);
    QHash<QString, QPushButton *> libraryTabs;
    QComboBox *libraryScope, *librarySort, *libraryView;
    QPushButton *libraryDirection, *libraryRefresh, *libraryTags;
    QLabel *libraryHeading, *libraryStatus;
    QString librarySignature;
    bool librarySyncStarted = false;
    QWidget *makeDevices();
    QWidget *makeSettings();
    void navigate(int index);
    void updateLibrary();
    void updateDevices();
    void showNotice(QString message);
    void updatePlayback(QJsonObject state);
    void showQueue();
    void updateGate();
};
} // namespace velune
