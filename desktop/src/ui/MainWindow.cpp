#include "MainWindow.h"
#include "Theme.h"
#include <QApplication>
#include <QButtonGroup>
#include <QDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
namespace velune {
namespace {
QLabel *heading(QString text, QWidget *parent = nullptr) {
    auto l = new QLabel(text, parent);
    auto f = l->font();
    f.setPixelSize(42);
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    l->setStyleSheet("font-size: 42px; font-weight: 600; background: transparent;");
    return l;
}
QFrame *divider() {
    auto f = new QFrame;
    f->setFixedHeight(1);
    f->setStyleSheet("background: #303030;");
    return f;
}
QString timeLabel(qint64 ms) {
    auto s = ms / 1000;
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}
} // namespace
MainWindow::MainWindow(Library *l, MusicService *m, ConnectClient *c, Discovery *d, bool demo)
    : library(l), music(m), remote(c), discovery(d), preview(demo) {
    setWindowTitle("Velune");
    setObjectName("veluneWindow");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(1000, 680);
    resize(1660, 938);
    setStyleSheet(theme::style());
    setMouseTracking(true);
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);
    // Window chrome stays usable while the authenticated content is blurred.
    titlebar = new QWidget(this);
    titlebar->setObjectName("titlebar");
    titlebar->setFixedHeight(36);
    auto chrome = new QHBoxLayout(titlebar);
    chrome->setContentsMargins(0, 0, 4, 0);
    chrome->setSpacing(0);
    chrome->addStretch();
    for (auto pair :
         {qMakePair(Icon::Minimize, QString("Minimize")), qMakePair(Icon::Maximize, QString("Maximize")),
          qMakePair(Icon::Close, QString("Close"))}) {
        auto b = new IconButton(pair.first, pair.second, titlebar);
        b->setFixedSize(52, 32);
        chrome->addWidget(b);
        if (pair.first == Icon::Minimize)
            connect(b, &QPushButton::clicked, this, &QWidget::showMinimized);
        else if (pair.first == Icon::Maximize)
            connect(b, &QPushButton::clicked, this,
                    [this] { isMaximized() ? showNormal() : showMaximized(); });
        else
            connect(b, &QPushButton::clicked, this, &QWidget::close);
    }
    root->addWidget(titlebar);
    shell = new QWidget(this);
    auto shellLayout = new QVBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    root->addWidget(shell, 1);
    auto top = new QWidget(shell);
    auto topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);
    shellLayout->addWidget(top, 1);
    sidebar = new QWidget(top);
    sidebar->setObjectName("sidebar");
    sidebar->setStyleSheet("#sidebar { border-right: 1px solid #303030; }");
    auto side = new QVBoxLayout(sidebar);
    side->setContentsMargins(22, 20, 20, 18);
    side->setSpacing(5);
    auto logo = new QWidget;
    logo->setObjectName("brand");
    logo->setFixedHeight(68);
    auto brand = new QHBoxLayout(logo);
    brand->setContentsMargins(14, 0, 0, 0);
    brand->setSpacing(12);
    auto logoIcon = new IconButton(Icon::Logo, "Velune", logo);
    logoIcon->setFocusPolicy(Qt::NoFocus);
    logoIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
    brand->addWidget(logoIcon);
    auto brandText = new QLabel("Velune");
    auto brandFont = brandText->font();
    brandFont.setPixelSize(30);
    brandFont.setWeight(QFont::DemiBold);
    brandText->setFont(brandFont);
    brandText->setStyleSheet("font-size: 30px; font-weight: 600;");
    brand->addWidget(brandText);
    brand->addStretch();
    side->addWidget(logo);
    side->addSpacing(18);
    auto group = new QButtonGroup(this);
    group->setExclusive(true);
    int index = 0;
    for (auto pair :
         {qMakePair(Icon::Home, QString("Home")), qMakePair(Icon::Search, QString("Search")),
          qMakePair(Icon::Explore, QString("Explore")), qMakePair(Icon::Library, QString("Library"))}) {
        auto b = new NavButton(pair.first, pair.second, sidebar);
        int target = index++;
        nav << b;
        group->addButton(b);
        side->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, target] { navigate(target); });
    }
    side->addSpacing(12);
    side->addWidget(divider());
    side->addSpacing(12);
    auto devices = new NavButton(Icon::Devices, "Available devices", sidebar);
    nav << devices;
    group->addButton(devices);
    side->addWidget(devices);
    connect(devices, &QPushButton::clicked, this, [this] { navigate(4); });
    side->addStretch();
    side->addWidget(divider());
    auto settings = new NavButton(Icon::Settings, "Settings", sidebar);
    nav << settings;
    group->addButton(settings);
    side->addWidget(settings);
    connect(settings, &QPushButton::clicked, this, [this] { navigate(5); });
    topLayout->addWidget(sidebar);
    auto scroll = new QScrollArea(top);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pages = new QStackedWidget;
    pages->setObjectName("pages");
    pages->addWidget(makeHome());
    pages->addWidget(makeResults());
    pages->addWidget(new QWidget);
    pages->addWidget(makeLibrary());
    pages->addWidget(makeDevices());
    pages->addWidget(makeSettings());
    scroll->setWidget(pages);
    topLayout->addWidget(scroll, 1);
    shellLayout->addWidget(makePlayer());
    blur = new QGraphicsBlurEffect(shell);
    blur->setBlurRadius(13);
    blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
    shell->setGraphicsEffect(blur);
    gate = new QWidget(this);
    gate->setObjectName("loginGate");
    gate->setStyleSheet("#loginGate { background: rgba(0,0,0,90); }");
    auto gateLayout = new QVBoxLayout(gate);
    gateLayout->addStretch();
    auto panel = new QWidget(gate);
    panel->setObjectName("signInPanel");
    panel->setMaximumWidth(510);
    panel->setStyleSheet(
        "#signInPanel { background: #0A0A0A; border: 1px solid #303030; border-radius: 24px; }");
    auto panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(38, 36, 38, 36);
    panelLayout->setSpacing(18);
    auto mark = new IconButton(Icon::Logo, "Velune", panel);
    mark->setFixedSize(56, 56);
    mark->setFocusPolicy(Qt::NoFocus);
    mark->setAttribute(Qt::WA_TransparentForMouseEvents);
    panelLayout->addWidget(mark, 0, Qt::AlignHCenter);
    auto welcome = heading("Your music. Your space.");
    welcome->setStyleSheet("font-size: 30px; background: transparent;");
    welcome->setAlignment(Qt::AlignCenter);
    panelLayout->addWidget(welcome);
    loginHint = new QLabel("Sign in with Google in your browser\nto connect your music library.");
    loginHint->setStyleSheet("color: #A3A3A3; font-size: 17px; background: transparent;");
    loginHint->setAlignment(Qt::AlignCenter);
    panelLayout->addWidget(loginHint);
    loginButton = new QPushButton("Continue with Google");
    loginButton->setObjectName("googleLogin");
    loginButton->setMinimumHeight(54);
    loginButton->setStyleSheet(
        "QPushButton { background: white; color: black; border-radius: 13px; font-weight: "
        "600; } QPushButton:hover { background: #DDDDDD; }");
    panelLayout->addWidget(loginButton);
    connect(loginButton, &QPushButton::clicked, this, [this] { music->login(this); });
    gateLayout->addWidget(panel, 0, Qt::AlignHCenter);
    gateLayout->addStretch();
    notice = new QLabel(this);
    notice->setObjectName("notice");
    notice->setWordWrap(true);
    notice->setAlignment(Qt::AlignCenter);
    notice->setStyleSheet(
        "background: #141414; color: white; border: 1px solid #484848; border-radius: 12px; padding: 14px;");
    notice->hide();
    connect(library, &Library::changed, this, &MainWindow::updateLibrary);
    connect(discovery, &Discovery::changed, this, &MainWindow::updateDevices);
    connect(music, &MusicService::authenticationChanged, this, [this](bool connected) {
        updateGate();
        accountLabel->setText(connected ? "Google account\n" + music->accountEmail()
                                        : "Google account\nNot signed in");
        if (connected) {
            raise();
            activateWindow();
        }
    });
    connect(music, &MusicService::loginPendingChanged, this, [this](bool pending) {
        loginButton->setText(pending ? "Cancel sign-in" : "Continue with Google");
        settingsSignIn->setText(pending ? "Cancel sign-in" : "Sign in with Google");
        loginHint->setText(pending ? "Finish sign-in in your browser.\nVelune will connect automatically."
                                   : "Sign in with Google in your browser\nto connect your music library.");
    });
    connect(music, &MusicService::playbackChanged, this, &MainWindow::updatePlayback);
    connect(music, &MusicService::message, this, &MainWindow::showNotice);
    connect(remote, &ConnectClient::error, this, &MainWindow::showNotice);
    connect(library, &Library::error, this, &MainWindow::showNotice);
    connect(music, &MusicService::results, this, [this](QList<Track> tracks) {
        results = tracks;
        searchResults->clear();
        for (const auto &t : tracks)
            searchResults->addItem(t.title + "\n" + t.artist);
        if (tracks.isEmpty())
            showNotice("No songs found.");
    });
    auto space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(space, &QShortcut::activated, this, [this] {
        if (music->loggedIn() || preview)
            music->action(playback["playing"].toBool() ? "pause" : "resume");
    });
    auto full = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(full, &QShortcut::activated, this, [this] { isFullScreen() ? showNormal() : showFullScreen(); });
    auto find = new QShortcut(QKeySequence::Find, this);
    connect(find, &QShortcut::activated, this, [this] {
        if (music->loggedIn() || preview) {
            navigate(1);
            searchInput->setFocus();
        }
    });
    nav[0]->setChecked(true);
    nav[0]->setFocus();
    updateLibrary();
    updateDevices();
    updateGate();
    qApp->installEventFilter(this);
}
QWidget *MainWindow::makeHome() {
    home = new QWidget;
    home->setObjectName("home");
    auto layout = new QVBoxLayout(home);
    layout->setContentsMargins(48, 28, 48, 32);
    layout->setSpacing(0);
    layout->addWidget(heading("Home"));
    layout->addSpacing(24);
    auto grid = new QWidget;
    homeGrid = new QGridLayout(grid);
    homeGrid->setContentsMargins(0, 0, 0, 0);
    homeGrid->setHorizontalSpacing(32);
    homeGrid->setVerticalSpacing(14);
    QStringList labels{"Liked", "Rand", "Rand",       "Song Radio", "Song radio",
                       "Song",  "Song", "Song radio", "Song"};
    for (int i = 0; i < 9; ++i) {
        auto b = new QuickTile(labels[i], i < 3 ? Icon::Play : i == 3 ? Icon::Radio : Icon::None, grid);
        b->setObjectName(i == 0 ? "likedTile" : QString("quick%1").arg(i));
        quick << b;
        homeGrid->addWidget(b, i / 3, i % 3);
        connect(b, &QPushButton::clicked, this, [this, i] {
            auto tracks = library->tracks(i == 0);
            if (tracks.isEmpty()) {
                showNotice(
                    "Your library is empty. Sign in to import music, or import local audio in Settings.");
                return;
            }
            int at = i == 0 ? 0 : QRandomGenerator::global()->bounded(tracks.size());
            if (i == 3 || i == 4 || i == 7)
                music->radio(tracks[at]);
            else
                music->play(tracks, at);
        });
    }
    for (int c = 0; c < 3; ++c)
        homeGrid->setColumnStretch(c, 1);
    layout->addWidget(grid);
    layout->addSpacing(4);
    layout->addWidget(divider());
    layout->addSpacing(14);
    auto row = new QHBoxLayout;
    row->setSpacing(30);
    for (int i = 0; i < 3; ++i) {
        auto card = new SongCard(i, home);
        card->setObjectName(QString("featured%1").arg(i));
        cards << card;
        row->addWidget(card, 1);
        connect(card, &QPushButton::clicked, this, [this, card] {
            auto tracks = library->tracks();
            for (int i = 0; i < tracks.size(); ++i)
                if (tracks[i].id == card->track().id) {
                    music->play(tracks, i);
                    return;
                }
            showNotice("Import your music library to play songs.");
        });
    }
    layout->addLayout(row);
    layout->addSpacing(16);
    layout->addWidget(divider());
    layout->addStretch();
    return home;
}
QWidget *MainWindow::makePlayer() {
    player = new QWidget;
    player->setObjectName("bottomPlayer");
    player->setFixedHeight(128);
    player->setStyleSheet(
        "#bottomPlayer { background: #0A0A0A; border: 1px solid #303030; border-top-left-radius: 24px; "
        "border-top-right-radius: 24px; } #bottomPlayer QWidget { background: transparent; }");
    auto row = new QHBoxLayout(player);
    row->setContentsMargins(32, 14, 32, 14);
    row->setSpacing(24);
    auto left = new QWidget;
    left->setMinimumWidth(260);
    left->setMaximumWidth(410);
    auto info = new QHBoxLayout(left);
    info->setContentsMargins(0, 0, 20, 0);
    info->setSpacing(18);
    cover = new QLabel;
    cover->setFixedSize(76, 76);
    cover->setStyleSheet("background: black; border: 1px solid #303030; border-radius: 10px;");
    QPixmap pix(76, 76);
    pix.fill(Qt::black);
    QPainter p(&pix);
    drawIcon(p, Icon::Logo, QRectF(22, 22, 32, 32));
    p.end();
    cover->setPixmap(pix);
    info->addWidget(cover);
    auto names = new QVBoxLayout;
    names->setSpacing(4);
    names->addStretch();
    title = new ElidedLabel("No song playing");
    title->setObjectName("trackTitle");
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title->setStyleSheet("font-size: 22px; font-weight: 600; background: transparent;");
    artist = new ElidedLabel("Choose something to listen to");
    artist->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    artist->setStyleSheet("font-size: 16px; color: #A3A3A3; background: transparent;");
    names->addWidget(title);
    names->addWidget(artist);
    names->addStretch();
    info->addLayout(names, 1);
    favorite = new IconButton(Icon::Heart, "Like song");
    favorite->setCheckable(true);
    info->addWidget(favorite);
    connect(favorite, &QPushButton::clicked, this, [this](bool liked) {
        auto song = Track::fromWire(playback["song"].toObject());
        if (!song.id.isEmpty())
            library->setLiked(song, liked);
    });
    auto sep = new QFrame;
    sep->setFixedSize(1, 48);
    sep->setStyleSheet("background: #303030;");
    info->addWidget(sep);
    row->addWidget(left, 3);
    auto center = new QWidget;
    center->setMinimumWidth(340);
    auto centerCol = new QVBoxLayout(center);
    centerCol->setContentsMargins(0, 0, 0, 0);
    centerCol->setSpacing(7);
    auto controls = new QHBoxLayout;
    controls->setSpacing(22);
    controls->addStretch();
    shuffleButton = new IconButton(Icon::Shuffle, "Shuffle");
    shuffleButton->setCheckable(true);
    controls->addWidget(shuffleButton);
    auto prev = new IconButton(Icon::Previous, "Previous");
    controls->addWidget(prev);
    play = new IconButton(Icon::Play, "Play or pause");
    play->setObjectName("playPause");
    play->setCircle(true);
    play->setFixedSize(58, 58);
    controls->addWidget(play);
    auto next = new IconButton(Icon::Next, "Next");
    controls->addWidget(next);
    repeatButton = new IconButton(Icon::Repeat, "Repeat: off");
    repeatButton->setCheckable(true);
    controls->addWidget(repeatButton);
    controls->addStretch();
    centerCol->addLayout(controls);
    auto seek = new QHBoxLayout;
    seek->setSpacing(12);
    elapsed = new QLabel("0:00");
    duration = new QLabel("0:00");
    for (auto l : {elapsed, duration})
        l->setStyleSheet("font-size: 14px; color: #A3A3A3; background: transparent;");
    seekBar = new QSlider(Qt::Horizontal);
    seekBar->setObjectName("seekBar");
    seekBar->setAccessibleName("Playback position");
    seekBar->setRange(0, 0);
    seekBar->setSingleStep(5000);
    seek->addWidget(elapsed);
    seek->addWidget(seekBar, 1);
    seek->addWidget(duration);
    centerCol->addLayout(seek);
    row->addWidget(center, 6);
    auto right = new QWidget;
    right->setMinimumWidth(250);
    auto extras = new QHBoxLayout(right);
    extras->setContentsMargins(0, 0, 0, 0);
    extras->setSpacing(14);
    auto queue = new IconButton(Icon::Queue, "Queue");
    auto fullscreen = new IconButton(Icon::Fullscreen, "Fullscreen");
    volumeButton = new IconButton(Icon::Volume, "Mute");
    volumeSlider = new QSlider(Qt::Horizontal);
    volumeSlider->setObjectName("volumeSlider");
    volumeSlider->setAccessibleName("Volume");
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(75);
    volumeSlider->setMinimumWidth(72);
    volumeSlider->setMaximumWidth(125);
    extras->addStretch();
    for (auto b : {queue, fullscreen, volumeButton})
        extras->addWidget(b);
    extras->addWidget(volumeSlider);
    row->addWidget(right, 3);
    connect(play, &QPushButton::clicked, this,
            [this] { music->action(playback["playing"].toBool() ? "pause" : "resume"); });
    connect(prev, &QPushButton::clicked, this, [this] { music->action("previous"); });
    connect(next, &QPushButton::clicked, this, [this] { music->action("next"); });
    connect(shuffleButton, &QPushButton::toggled, music, &MusicService::shuffle);
    connect(repeatButton, &QPushButton::clicked, this, [this] {
        repeatMode = (repeatMode + 1) % 3;
        repeatButton->setChecked(repeatMode != 0);
        repeatButton->setToolTip(QString("Repeat: %1")
                                     .arg(repeatMode == 0   ? "off"
                                          : repeatMode == 1 ? "one"
                                                            : "all"));
        music->repeat(repeatMode);
    });
    connect(seekBar, &QSlider::sliderReleased, this, [this] { music->seek(seekBar->value()); });
    connect(seekBar, &QSlider::actionTriggered, this, [this](int action) {
        if (action != QAbstractSlider::SliderMove && action != QAbstractSlider::SliderNoAction)
            music->seek(seekBar->sliderPosition());
    });
    connect(volumeSlider, &QSlider::valueChanged, this, [this](int v) {
        volumeButton->setGlyph(v == 0 ? Icon::Muted : Icon::Volume);
        music->volume(v);
    });
    connect(volumeButton, &QPushButton::clicked, this, [this] {
        if (volumeSlider->value()) {
            previousVolume = volumeSlider->value();
            volumeSlider->setValue(0);
        } else
            volumeSlider->setValue(qMax(1, previousVolume));
    });
    connect(queue, &QPushButton::clicked, this, &MainWindow::showQueue);
    connect(fullscreen, &QPushButton::clicked, this,
            [this] { isFullScreen() ? showNormal() : showFullScreen(); });
    return player;
}
QWidget *MainWindow::makeResults() {
    auto page = new QWidget;
    auto col = new QVBoxLayout(page);
    col->setContentsMargins(48, 28, 48, 32);
    col->setSpacing(24);
    resultsHeading = heading("Search");
    col->addWidget(resultsHeading);
    searchInput = new QLineEdit;
    searchInput->setPlaceholderText("Search songs, artists and albums");
    searchInput->setClearButtonEnabled(true);
    searchInput->setObjectName("searchInput");
    col->addWidget(searchInput);
    searchResults = new QListWidget;
    searchResults->setAccessibleName("Search results");
    col->addWidget(searchResults, 1);
    connect(searchInput, &QLineEdit::returnPressed, this, [this] {
        if (!searchInput->text().trimmed().isEmpty()) {
            showNotice("Searching YouTube Music…");
            music->search(searchInput->text().trimmed());
        }
    });
    connect(searchResults, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *i) { music->play(results, searchResults->row(i)); });
    return page;
}
QWidget *MainWindow::makeLibrary() {
    auto p = new QWidget;
    auto col = new QVBoxLayout(p);
    col->setContentsMargins(48, 28, 48, 32);
    col->setSpacing(24);
    col->addWidget(heading("Library"));
    libraryList = new QListWidget;
    libraryList->setAccessibleName("Music library");
    col->addWidget(libraryList, 1);
    connect(libraryList, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *i) { music->play(library->tracks(), libraryList->row(i)); });
    return p;
}
QWidget *MainWindow::makeDevices() {
    auto p = new QWidget;
    auto col = new QVBoxLayout(p);
    col->setContentsMargins(48, 28, 48, 32);
    col->setSpacing(24);
    col->addWidget(heading("Available devices"));
    auto text = new QLabel("Select a Velune device on the same network.");
    text->setStyleSheet("color: #A3A3A3;");
    col->addWidget(text);
    devicesList = new QListWidget;
    devicesList->setAccessibleName("Available output devices");
    col->addWidget(devicesList, 1);
    connect(devicesList, &QListWidget::itemClicked, this, [this](QListWidgetItem *i) {
        int row = devicesList->row(i);
        if (row == 0) {
            remote->detach();
            return;
        }
        auto devices = discovery->devices();
        if (row - 1 < devices.size()) {
            auto d = devices[row - 1];
            remote->attach(d.host, d.port, d.id);
        }
    });
    return p;
}
QWidget *MainWindow::makeSettings() {
    auto p = new QWidget;
    auto col = new QVBoxLayout(p);
    col->setContentsMargins(48, 28, 48, 32);
    col->setSpacing(24);
    col->addWidget(heading("Settings"));
    accountLabel = new QLabel("Google account\nNot signed in");
    col->addWidget(accountLabel);
    settingsSignIn = new QPushButton("Sign in with Google");
    auto signOut = new QPushButton("Sign out");
    auto import = new QPushButton("Import local audio");
    for (auto b : {settingsSignIn, signOut, import}) {
        b->setMaximumWidth(320);
        col->addWidget(b);
    }
    connect(settingsSignIn, &QPushButton::clicked, this, [this] { music->login(this); });
    connect(signOut, &QPushButton::clicked, music, &MusicService::logout);
    connect(import, &QPushButton::clicked, this, [this] { music->importAudio(this); });
    auto label = new QLabel("Velune Desktop · C++ / Qt\nPure AMOLED monochrome\n\nConnect library mirroring "
                            "does not sign you in to Google.");
    label->setStyleSheet("color: #A3A3A3; line-height: 1.5;");
    col->addWidget(label);
    col->addStretch();
    return p;
}
void MainWindow::navigate(int index) {
    resultsHeading->setText(index == 2 ? "Explore" : "Search");
    searchInput->setVisible(index != 2);
    if (index == 2) {
        pages->setCurrentIndex(1);
        music->explore();
        searchInput->setPlaceholderText("Explore YouTube Music");
    } else {
        pages->setCurrentIndex(index);
        if (index == 1)
            searchInput->setPlaceholderText("Search songs, artists and albums");
    }
    nav[index]->setChecked(true);
}
void MainWindow::updateLibrary() {
    auto tracks = library->tracks();
    libraryList->clear();
    for (const auto &t : tracks)
        libraryList->addItem(t.title + "\n" + t.artist);
    for (int i = 0; i < cards.size(); ++i) {
        cards[i]->setTrack(i < tracks.size() ? tracks[i] : Track{});
        cards[i]->setArtwork({});
        if (i < tracks.size() && QUrl(tracks[i].thumbnail).scheme() == "https") {
            auto card = cards[i];
            auto id = tracks[i].id;
            QNetworkRequest request(QUrl(tracks[i].thumbnail));
            request.setTransferTimeout(15000);
            auto reply = artworkNetwork.get(request);
            connect(reply, &QNetworkReply::finished, card, [reply, card, id] {
                if (reply->error() == QNetworkReply::NoError && reply->bytesAvailable() <= 2 * 1024 * 1024 &&
                    card->track().id == id) {
                    QPixmap p;
                    p.loadFromData(reply->readAll());
                    card->setArtwork(p);
                }
                reply->deleteLater();
            });
        }
    }
    auto song = Track::fromWire(playback["song"].toObject());
    favorite->setChecked(library->liked(song.id));
}
void MainWindow::updateDevices() {
    auto current = devicesList->currentRow();
    devicesList->clear();
    devicesList->addItem(remote->hasTarget() ? "This computer" : "This computer · Current output");
    for (const auto &d : discovery->devices())
        devicesList->addItem(d.name +
                             (remote->connected() && remote->peerName() == d.name ? " · Connected" : ""));
    if (current >= 0)
        devicesList->setCurrentRow(current);
}
void MainWindow::showNotice(QString message) {
    notice->setText(message);
    notice->setFixedWidth(qMin(680, width() - 80));
    notice->adjustSize();
    notice->move((width() - notice->width()) / 2, height() - player->height() - notice->height() - 22);
    notice->show();
    notice->raise();
    QTimer::singleShot(7500, notice, &QWidget::hide);
}
void MainWindow::updatePlayback(QJsonObject s) {
    const auto previousId = playback["song"].toObject()["id"].toString();
    playback = s;
    auto track = Track::fromWire(s["song"].toObject());
    if (track.id != previousId && QUrl(track.thumbnail).scheme() == "https") {
        QNetworkRequest request(QUrl(track.thumbnail));
        request.setTransferTimeout(15000);
        auto reply = artworkNetwork.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, id = track.id] {
            if (reply->error() == QNetworkReply::NoError && reply->bytesAvailable() <= 2 * 1024 * 1024 &&
                playback["song"].toObject()["id"].toString() == id) {
                QPixmap pixmap;
                if (pixmap.loadFromData(reply->readAll()))
                    cover->setPixmap(
                        pixmap.scaled(76, 76, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            }
            reply->deleteLater();
        });
    }
    title->setText(track.title.isEmpty() ? "No song playing" : track.title);
    title->setToolTip(track.title);
    artist->setText(track.artist);
    play->setGlyph(s["playing"].toBool() ? Icon::Pause : Icon::Play);
    favorite->setEnabled(!track.id.isEmpty());
    favorite->setChecked(library->liked(track.id));
    {
        QSignalBlocker b(shuffleButton);
        shuffleButton->setChecked(s["shuffle"].toBool());
    }
    repeatMode = s["repeat"].toInt();
    repeatButton->setChecked(repeatMode != 0);
    if (!seekBar->isSliderDown()) {
        QSignalBlocker b(seekBar);
        seekBar->setRange(0, qMin<double>(s["duration"].toDouble(), 2147483647));
        seekBar->setValue(int(s["position"].toDouble()));
    }
    elapsed->setText(timeLabel(qint64(s["position"].toDouble())));
    duration->setText(timeLabel(qint64(s["duration"].toDouble())));
    auto v = s["deviceVolume"].toObject();
    if (!v.isEmpty() && !volumeSlider->isSliderDown()) {
        QSignalBlocker b(volumeSlider);
        int range = v["max"].toInt() - v["min"].toInt();
        volumeSlider->setEnabled(range > 0 && !v["fixed"].toBool());
        if (range > 0)
            volumeSlider->setValue(
                v["muted"].toBool() ? 0 : (v["level"].toInt() - v["min"].toInt()) * 100 / range);
    }
    updateDevices();
}
void MainWindow::showQueue() {
    auto dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Queue");
    dialog->resize(480, 580);
    auto layout = new QVBoxLayout(dialog);
    layout->addWidget(heading("Queue"));
    auto list = new QListWidget;
    QList<Track> tracks;
    for (auto v : playback["queue"].toArray()) {
        auto t = Track::fromWire(v.toObject());
        tracks << t;
        list->addItem(t.title + "\n" + t.artist);
    }
    list->setCurrentRow(playback["index"].toInt(-1));
    layout->addWidget(list);
    connect(list, &QListWidget::itemActivated, dialog,
            [this, list, tracks](QListWidgetItem *item) { music->play(tracks, list->row(item)); });
    dialog->show();
}
void MainWindow::updateGate() {
    bool locked = !music->loggedIn() && !preview;
    blur->setEnabled(locked);
    shell->setEnabled(!locked);
    gate->setVisible(locked);
    gate->setGeometry(0, titlebar->height(), width(), height() - titlebar->height());
    if (locked)
        gate->raise();
}
void MainWindow::setPreview(bool value) {
    preview = value;
    updateGate();
}
void MainWindow::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    sidebar->setFixedWidth(qBound(260, qRound(width() * .21), 348));
    auto side = qobject_cast<QVBoxLayout *>(sidebar->layout());
    side->setContentsMargins(22, qBound(0, (width() - 1280) / 19, 20), 20, 18);
    sidebar->findChild<QWidget *>("brand")->setFixedHeight(qBound(52, 52 + (width() - 1280) / 24, 68));
    for (auto button : nav)
        button->setFixedHeight(qBound(44, width() / 30, 56));
    int pad = qBound(28, qRound(width() * .029), 48);
    auto v = qobject_cast<QVBoxLayout *>(home->layout());
    v->setContentsMargins(pad, qBound(20, 20 + (width() - 1280) / 14, 48), pad, 30);
    for (auto b : quick)
        b->setFixedHeight(qBound(54, width() / 23, 72));
    homeGrid->setHorizontalSpacing(qBound(20, width() / 52, 32));
    for (auto c : cards)
        c->setFixedHeight(qBound(220, qRound((width() - sidebar->width() - pad * 2 - 60) / 3. * .78), 350));
    gate->setGeometry(0, titlebar->height(), width(), height() - titlebar->height());
}
bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    auto target = qobject_cast<QWidget *>(object);
    if (!target || target->window() != this)
        return false;
    if (event->type() == QEvent::MouseButtonDblClick && object == titlebar) {
        isMaximized() ? showNormal() : showMaximized();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && windowHandle()) {
            auto pos = target->mapTo(this, mouse->position().toPoint());
            Qt::Edges edges;
            if (pos.x() < 7)
                edges |= Qt::LeftEdge;
            if (pos.x() > width() - 7)
                edges |= Qt::RightEdge;
            if (pos.y() < 7)
                edges |= Qt::TopEdge;
            if (pos.y() > height() - 7)
                edges |= Qt::BottomEdge;
            if (edges) {
                windowHandle()->startSystemResize(edges);
                return true;
            }
            if (object == titlebar) {
                windowHandle()->startSystemMove();
                return true;
            }
        }
    }
    return QWidget::eventFilter(object, event);
}
} // namespace velune
