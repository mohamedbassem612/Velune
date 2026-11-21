#pragma once
#include "Components.h"
#include "services/Discovery.h"
#include "services/MusicService.h"
#include <QGraphicsBlurEffect>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSlider>
#include <QStackedWidget>
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
    IconButton *play, *favorite, *shuffleButton, *repeatButton, *volumeButton;
    QSlider *seekBar, *volumeSlider;
    QLineEdit *searchInput;
    QListWidget *searchResults, *libraryList, *devicesList;
    QGraphicsBlurEffect *blur;
    QList<Track> results;
    QJsonObject playback;
    QNetworkAccessManager artworkNetwork;
    bool preview = false;
    int repeatMode = 0;
    int previousVolume = 75;
    QWidget *makeHome();
    QWidget *makePlayer();
    QWidget *makeResults();
    QWidget *makeLibrary();
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
