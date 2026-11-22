#include "MainWindow.h"
#include "Theme.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QPainterPath>
#include <QPersistentModelIndex>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
namespace velune {
namespace {
QIcon trackThumbnail(const QPixmap &art, int side = 56) {
    QPixmap image(side, side);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, side, side), side / 7., side / 7.);
    painter.fillPath(clip, QColor("#141414"));
    painter.setClipPath(clip);
    if (art.isNull())
        drawIcon(painter, Icon::Library, QRectF(side * .3, side * .3, side * .4, side * .4));
    else {
        const auto scaled = art.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        painter.drawPixmap(0, 0, scaled, (scaled.width() - side) / 2, (scaled.height() - side) / 2, side,
                           side);
    }
    return QIcon(image);
}
QString searchDescription(const QJsonObject &entry) {
    const auto kind = entry["kind"].toString();
    const auto artist = Track::fromWire(entry).artist;
    if (kind == "artist")
        return "Artist";
    if (kind == "album" || kind == "playlist" || kind == "video")
        return kind.left(1).toUpper() + kind.mid(1) + (artist.isEmpty() ? "" : " · " + artist);
    return artist.isEmpty() ? entry["subtitle"].toString() : artist;
}
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
    side->removeWidget(nav[2]);
    side->addWidget(nav[2]);
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
    pages->addWidget(makeCollection());
    const auto sizeCurrentPage = [this] {
        for (int i = 0; i < pages->count(); ++i)
            pages->widget(i)->setSizePolicy(
                i == pages->currentIndex() ? QSizePolicy::Expanding : QSizePolicy::Ignored,
                i == pages->currentIndex() ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        pages->layout()->invalidate();
        pages->updateGeometry();
    };
    connect(pages, &QStackedWidget::currentChanged, this, [scroll, sizeCurrentPage] {
        sizeCurrentPage();
        scroll->verticalScrollBar()->setValue(0);
    });
    sizeCurrentPage();
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
    loginHint = new QLabel("Sign in with Google inside Velune\nto connect your music library.");
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
        librarySyncStarted = false;
        if (connected && pages->currentIndex() == 3) {
            librarySyncStarted = true;
            music->refreshCatalog({"liked", "songs", "albums", "artists", "playlists"});
        }
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
        loginHint->setText(pending
                               ? "Finish sign-in in the Velune window.\nVelune will connect automatically."
                               : "Sign in with Google inside Velune\nto connect your music library.");
    });
    connect(music, &MusicService::playbackChanged, this, &MainWindow::updatePlayback);
    connect(music, &MusicService::message, this, &MainWindow::showNotice);
    connect(remote, &ConnectClient::error, this, &MainWindow::showNotice);
    connect(library, &Library::error, this, &MainWindow::showNotice);
    connect(music, &MusicService::results, this, [this](QList<Track> tracks) {
        searchSuggestions->clear();
        searchSuggestions->hide();
        results = tracks;
        populateTrackList(searchResults, tracks);
        searchStatus->setText(tracks.isEmpty() ? "No songs found."
                                               : QString("%1 results").arg(tracks.size()));
    });
    connect(music, &MusicService::suggestionsReady, this, [this](const QJsonObject &response) {
        fullSearch = false;
        searchFilter->hide();
        searchMore->hide();
        searchSuggestions->clear();
        for (auto key : {"history", "queries"})
            for (auto query : response[key].toArray()) {
                auto item = new QListWidgetItem(query.toString(), searchSuggestions);
                item->setData(Qt::UserRole, query.toString());
                item->setData(Qt::UserRole + 1, QString(key) == "history");
                item->setToolTip(QString(key) == "history" ? "Recent search. Right-click to remove."
                                                           : "Search suggestion");
                item->setSizeHint(QSize(0, 44));
            }
        searchSuggestions->setFixedHeight(qMin(3, searchSuggestions->count()) * 44 + 2);
        searchSuggestions->setVisible(searchSuggestions->count() > 0);
        const auto items = response["items"].toArray();
        QList<Track> tracks;
        for (auto item : items)
            tracks << Track::fromWire(item.toObject());
        results = tracks;
        populateTrackList(searchResults, tracks);
        for (int i = 0; i < items.size(); ++i) {
            const auto entry = items[i].toObject();
            searchResults->item(i)->setData(Qt::UserRole + 3, entry);
            searchResults->item(i)->setText(entry["title"].toString() + "\n" + searchDescription(entry));
        }
        searchStatus->setText(
            response.contains("error")
                ? "Suggestions could not be loaded. Your recent searches are still available."
            : items.isEmpty() && searchSuggestions->count() == 0
                ? (searchInput->text().trimmed().isEmpty() ? "Type to search YouTube Music"
                                                           : "No suggestions found.")
                : "Suggested results");
    });
    auto engine = music->searchSystem();
    connect(engine, &SearchEngine::pageReady, this, &MainWindow::renderSearchPage);
    connect(engine, &SearchEngine::busyChanged, this, [this](bool busy) {
        searchMore->setEnabled(!busy);
        if (busy)
            searchStatus->setText("Searching…");
    });
    connect(engine, &SearchEngine::failed, this, [this](QString message) {
        searchStatus->setText(message);
        if (!searchPage["continuation"].toString().isEmpty()) {
            searchMore->setText("Retry loading more");
            searchMore->show();
        }
    });
    connect(engine, &SearchEngine::settingsChanged, this, [this] {
        if (pages->currentIndex() == 1 && !searchInput->isHidden())
            submitSearch(!fullSearch);
    });
    connect(music, &MusicService::collectionFailed, this, [this](QString token, QString reason) {
        if (token != pendingCollection)
            return;
        pendingCollection.clear();
        collectionCount->setText(QString("%1 saved songs").arg(collectionTracks.size()));
        showNotice(reason);
    });
    connect(music, &MusicService::collectionLoaded, this, [this](QString token, QList<Track> tracks) {
        if (token != pendingCollection || pages->currentWidget() != collectionPage)
            return;
        pendingCollection.clear();
        collectionTracks = tracks;
        renderCollection();
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
    QStringList labels{"Liked Songs", "Daily Mix 1",  "Daily Mix 2",  "Song Radio",  "Song Radio",
                       "Song Radio",  "No songs yet", "No songs yet", "No songs yet"};
    for (int i = 0; i < 9; ++i) {
        auto b = new QuickTile(labels[i],
                               i == 0            ? Icon::Heart
                               : i >= 3 && i < 6 ? Icon::Radio
                                                 : Icon::None,
                               grid);
        b->setObjectName(i == 0 ? "likedTile" : QString("quick%1").arg(i));
        quick << b;
        homeGrid->addWidget(b, i / 3, i % 3);
        connect(b, &QPushButton::clicked, this, [this, i] { openQuick(i); });
        connect(b, &QuickTile::playRequested, this, [this, i] { playQuick(i); });
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
        connect(card, &QPushButton::clicked, this, [this, card] { openTrack(card->track()); });
        connect(card, &SongCard::playRequested, this, [this, card] { music->play({card->track()}); });
    }
    layout->addLayout(row);
    layout->addSpacing(16);
    layout->addWidget(divider());
    layout->addStretch();
    return home;
}
QWidget *MainWindow::makeCollection() {
    collectionPage = new QWidget;
    collectionPage->setObjectName("collectionPage");
    auto layout = new QVBoxLayout(collectionPage);
    layout->setContentsMargins(48, 12, 48, 32);
    layout->setSpacing(20);
    auto back = new IconButton(Icon::Back, "Back", collectionPage);
    back->setObjectName("collectionBack");
    layout->addWidget(back, 0, Qt::AlignLeft);
    connect(back, &QPushButton::clicked, this, [this] {
        pendingCollection.clear();
        if (!collectionHistory.isEmpty()) {
            const auto previous = collectionHistory.takeLast();
            collectionName = previous.name;
            collectionDescription = previous.description;
            collectionLiked = previous.liked;
            collectionQuick = previous.quick;
            collectionTracks = previous.quick == 0 ? library->tracks(true) : previous.tracks;
            collectionTitle->setText(previous.name);
            collectionSubtitle->setText(previous.description);
            collectionKind->setText(previous.kind);
            renderCollection();
        } else {
            collectionQuick = -1;
            pages->setCurrentIndex(returnPage);
        }
    });
    auto headerWidget = new QWidget;
    headerWidget->setFixedHeight(180);
    auto header = new QHBoxLayout(headerWidget);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(30);
    collectionCover = new QLabel;
    collectionCover->setObjectName("collectionCover");
    collectionCover->setFixedSize(180, 180);
    header->addWidget(collectionCover);
    auto metadata = new QVBoxLayout;
    metadata->setSpacing(10);
    metadata->addStretch();
    collectionKind = new QLabel;
    collectionKind->setStyleSheet("color: #A3A3A3; font-size: 15px; letter-spacing: 2px;");
    collectionTitle = heading("");
    collectionTitle->setObjectName("collectionTitle");
    collectionTitle->setWordWrap(true);
    collectionSubtitle = new QLabel;
    collectionSubtitle->setWordWrap(true);
    collectionSubtitle->setStyleSheet("color: #A3A3A3; font-size: 21px;");
    collectionCount = new QLabel;
    collectionCount->setObjectName("collectionCount");
    collectionCount->setStyleSheet("color: #A3A3A3; font-size: 16px;");
    for (auto label : {collectionKind, collectionTitle, collectionSubtitle, collectionCount})
        metadata->addWidget(label);
    metadata->addStretch();
    header->addLayout(metadata, 1);
    layout->addWidget(headerWidget);
    layout->addWidget(divider());
    collectionRows = new QVBoxLayout;
    collectionRows->setSpacing(12);
    layout->addLayout(collectionRows);
    layout->addStretch();
    auto content = collectionPage;
    collectionPage = new QWidget;
    collectionPage->setObjectName("collectionPage");
    collectionPage->setFocusPolicy(Qt::StrongFocus);
    content->setObjectName("collectionContent");
    auto wrapper = new QVBoxLayout(collectionPage);
    wrapper->setContentsMargins(0, 0, 0, 0);
    auto scroll = new QScrollArea(collectionPage);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { loadVisibleArtwork(); });
    wrapper->addWidget(scroll);
    return collectionPage;
}
void MainWindow::populateTrackList(QListWidget *list, const QList<Track> &tracks) {
    list->clear();
    list->setIconSize(QSize(56, 56));
    list->setUniformItemSizes(true);
    for (const auto &track : tracks) {
        auto item = new QListWidgetItem(trackThumbnail(artworkCache.value(track.thumbnail)),
                                        track.title + "\n" + track.artist, list);
        item->setData(Qt::UserRole, track.id);
        item->setSizeHint(QSize(0, 88));
        item->setData(Qt::UserRole + 1, track.thumbnail);
        item->setData(Qt::UserRole + 3, track.wire());
    }
    if (!list->property("artworkConnected").toBool()) {
        list->setProperty("artworkConnected", true);
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QWidget::customContextMenuRequested, this,
                [this, list](QPoint position) { libraryContextMenu(list, position); });
        if (auto trackList = qobject_cast<TrackListWidget *>(list)) {
            connect(trackList, &TrackListWidget::playRequested, this, [this, list](QListWidgetItem *item) {
                const auto entry = item->data(Qt::UserRole + 3).toJsonObject();
                const auto kind = entry["kind"].toString();
                if (kind == "section")
                    return;
                if (kind == "auto") {
                    QList<Track> tracks;
                    for (auto value : entry["localTracks"].toArray())
                        tracks << Track::fromWire(value.toObject());
                    if (!tracks.isEmpty())
                        music->play(tracks);
                    return;
                }
                if (entry.contains("localTracks") &&
                    (entry["browseId"].toString().isEmpty() || entry["preferLocal"].toBool())) {
                    QList<Track> tracks;
                    for (auto value : entry["localTracks"].toArray())
                        tracks << Track::fromWire(value.toObject());
                    music->play(tracks);
                    return;
                }
                if (kind == "album" || kind == "artist" || kind == "playlist") {
                    music->playBrowse(entry["browseId"].toString(), entry["playlistId"].toString());
                    return;
                }
                QList<Track> queue;
                int selected = -1;
                for (int i = 0; i < list->count(); ++i) {
                    const auto candidate = list->item(i)->data(Qt::UserRole + 3).toJsonObject();
                    const auto type = candidate["kind"].toString();
                    if (!type.isEmpty() && type != "song" && type != "video")
                        continue;
                    if (list->item(i) == item)
                        selected = queue.size();
                    queue << Track::fromWire(candidate);
                }
                if (selected >= 0)
                    music->play(queue, selected);
            });
            connect(trackList, &TrackListWidget::openRequested, this, [this, list](QListWidgetItem *item) {
                openSearchResult(item->data(Qt::UserRole + 3).toJsonObject());
                if (list->window() != this)
                    list->window()->close();
            });
        }
        connect(list->verticalScrollBar(), &QScrollBar::valueChanged, list,
                [this, list] { loadListArtwork(list); });
        connect(list->verticalScrollBar(), &QScrollBar::rangeChanged, list,
                [this, list] { QTimer::singleShot(0, list, [this, list] { loadListArtwork(list); }); });
    }
    QTimer::singleShot(0, list, [this, list] { loadListArtwork(list); });
}
void MainWindow::loadListArtwork(QListWidget *list) {
    if (!list->isVisible())
        return;
    for (int row = 0; row < list->count(); ++row) {
        auto item = list->item(row);
        if (item->data(Qt::UserRole + 8).toBool())
            continue;
        if (!list->visualItemRect(item).intersects(list->viewport()->rect()) ||
            item->data(Qt::UserRole + 2).toBool())
            continue;
        item->setData(Qt::UserRole + 2, true);
        const auto url = item->data(Qt::UserRole + 1).toString();
        const auto id = item->data(Qt::UserRole).toString();
        const QPersistentModelIndex index(list->model()->index(row, 0));
        const auto entry = item->data(Qt::UserRole + 3).toJsonObject();
        if (url.isEmpty() && entry.contains("localTracks") && entry["section"] != "liked") {
            QList<Track> covers;
            const auto tracks = entry["localTracks"].toArray();
            for (int i = 0; i < qMin(4, tracks.size()); ++i)
                covers << Track::fromWire(tracks[i].toObject());
            for (const auto &cover : covers)
                loadArtwork(cover.thumbnail, list, [this, list, index, id, covers](QPixmap) {
                    if (!index.isValid())
                        return;
                    auto current = list->item(index.row());
                    if (current && current->data(Qt::UserRole).toString() == id)
                        current->setIcon(QIcon(collectionArtwork(covers, false)));
                });
            continue;
        }
        loadArtwork(url, list, [list, index, id](QPixmap art) {
            if (!index.isValid())
                return;
            auto item = list->item(index.row());
            if (item && item->data(Qt::UserRole).toString() == id)
                item->setIcon(trackThumbnail(art, list->property("libraryGrid").toBool() ? 180 : 56));
        });
    }
}

void MainWindow::loadArtwork(QString url, QObject *owner, std::function<void(QPixmap)> apply) {
    if (url.isEmpty())
        return;
    if (artworkCache.contains(url)) {
        apply(artworkCache.value(url));
        return;
    }
    const QUrl source(url.startsWith("//") ? "https:" + url : url);
    if (source.isLocalFile()) {
        QTimer::singleShot(0, owner, [this, source, url, apply] {
            const QFileInfo file(source.toLocalFile());
            if (!file.isFile() || file.size() > 2 * 1024 * 1024)
                return;
            QPixmap image(source.toLocalFile());
            if (image.isNull())
                return;
            image = image.scaled(600, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            artworkCache.insert(url, image);
            apply(image);
        });
        return;
    }
    if (source.scheme() != "https")
        return;
    auto reply = artworkRequests.value(url, nullptr);
    if (!reply) {
        QNetworkRequest request{source};
        request.setTransferTimeout(15000);
        reply = artworkNetwork.get(request);
        artworkRequests.insert(url, reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
            if (reply->error() == QNetworkReply::NoError && reply->bytesAvailable() <= 2 * 1024 * 1024) {
                QPixmap pixmap;
                if (pixmap.loadFromData(reply->readAll()))
                    artworkCache.insert(
                        url, pixmap.scaled(600, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
            artworkRequests.remove(url);
            reply->deleteLater();
        });
    }
    connect(reply, &QNetworkReply::finished, owner, [this, url, apply] {
        if (artworkCache.contains(url))
            apply(artworkCache.value(url));
    });
}
QPixmap MainWindow::collectionArtwork(const QList<Track> &tracks, bool liked) const {
    QPixmap image(180, 180);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, 180, 180), 18, 18);
    painter.fillPath(clip, QColor("#141414"));
    painter.setClipPath(clip);
    if (liked)
        drawIcon(painter, Icon::Heart, QRectF(36, 36, 108, 108), Qt::white, true);
    else {
        const int count = qMin(4, int(tracks.size()));
        if (!count)
            drawIcon(painter, Icon::Library, QRectF(58, 58, 64, 64));
        for (int i = 0; i < (count > 1 ? 4 : count); ++i) {
            QRect area = count == 1 ? QRect(0, 0, 180, 180) : QRect((i % 2) * 90, (i / 2) * 90, 90, 90);
            auto art = artworkCache.value(tracks[i % count].thumbnail);
            if (art.isNull())
                drawIcon(painter, Icon::Library,
                         QRectF(area).adjusted(area.width() / 3., area.height() / 3., -area.width() / 3.,
                                               -area.height() / 3.));
            else {
                auto scaled =
                    art.scaled(area.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                painter.drawPixmap(area, scaled,
                                   QRect((scaled.width() - area.width()) / 2,
                                         (scaled.height() - area.height()) / 2, area.width(), area.height()));
            }
        }
    }
    return image;
}
QList<Track> MainWindow::quickTracks(int index) const {
    if (index == 0)
        return library->tracks(true);
    if (index < 3)
        return mixes[index - 1];
    auto tracks = library->tracks();
    int at = index < 6 ? index - 3 : index - 6;
    return at < tracks.size() ? QList<Track>{tracks[at]} : QList<Track>{};
}
void MainWindow::playQuick(int index) {
    auto tracks = quickTracks(index);
    if (tracks.isEmpty()) {
        showNotice("There are no songs available yet.");
        return;
    }
    if (index >= 3 && index < 6)
        music->radio(tracks.first());
    else
        music->play(tracks);
}
void MainWindow::openQuick(int index) {
    auto tracks = quickTracks(index);
    if (index == 0)
        showCollection("Liked Songs", "Your collection", tracks, true);
    else if (index < 3) {
        QStringList artists;
        for (const auto &track : tracks)
            if (!track.artist.isEmpty() && !artists.contains(track.artist))
                artists << track.artist;
        showCollection(QString("Daily Mix %1").arg(index),
                       artists.isEmpty() ? "From your library" : artists.mid(0, 3).join(", "), tracks, false,
                       "FROM YOUR LIBRARY");
    } else if (!tracks.isEmpty() && index < 6) {
        auto seed = tracks.first();
        showCollection(seed.title + " Radio", seed.artist, {}, false, "SONG RADIO");
        collectionCover->setPixmap(collectionArtwork({seed}, false));
        pendingCollection = QUuid::createUuid().toString();
        collectionCount->setText("Loading song radio…");
        music->browseCollection(pendingCollection, seed, true);
    } else if (!tracks.isEmpty()) {
        openTrack(tracks.first());
        return;
    }
    collectionQuick = index;
}
void MainWindow::openSearchResult(const QJsonObject &entry) {
    const auto kind = entry["kind"].toString();
    auto track = Track::fromWire(entry);
    if (kind == "section")
        return;
    if (kind == "auto") {
        QList<Track> tracks;
        for (auto value : entry["localTracks"].toArray())
            tracks << Track::fromWire(value.toObject());
        showCollection(track.title, "", tracks, entry["section"] == "liked", "PLAYLIST");
        if (entry["section"] == "liked")
            collectionQuick = 0;
        if (!entry["note"].toString().isEmpty())
            showNotice(entry["note"].toString());
        return;
    }
    if (entry.contains("localTracks") &&
        (entry["browseId"].toString().isEmpty() || entry["preferLocal"].toBool())) {
        QList<Track> tracks;
        for (auto value : entry["localTracks"].toArray())
            tracks << Track::fromWire(value.toObject());
        showCollection(track.title, track.artist, tracks, false, kind.toUpper());
        return;
    }
    if (kind != "artist" && kind != "album" && kind != "playlist") {
        openTrack(track);
        return;
    }
    QList<Track> cachedTracks;
    for (auto value : entry["localTracks"].toArray())
        cachedTracks << Track::fromWire(value.toObject());
    showCollection(track.title, track.artist, cachedTracks, false, kind.toUpper());
    pendingCollection = QUuid::createUuid().toString();
    collectionCount->setText("Loading…");
    const auto name = track.title;
    loadArtwork(track.thumbnail, collectionPage, [this, name](QPixmap image) {
        if (collectionName == name)
            collectionCover->setPixmap(
                image.scaled(180, 180, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    });
    track.albumId = entry["browseId"].toString();
    music->browseCollection(pendingCollection, track, false);
}
void MainWindow::openTrack(const Track &track) {
    if (track.id.isEmpty())
        return;
    QList<Track> tracks;
    if (!track.albumId.isEmpty()) {
        for (const auto &item : library->tracks())
            if (item.albumId == track.albumId)
                tracks << item;
    } else
        tracks << track;
    showCollection(track.album.isEmpty() ? track.title : track.album, track.artist, tracks, false,
                   track.albumId.isEmpty() ? "SONG" : "ALBUM");
    if (!track.albumId.isEmpty()) {
        pendingCollection = QUuid::createUuid().toString();
        collectionCount->setText("Loading album…");
        music->browseCollection(pendingCollection, track, false);
    }
}
void MainWindow::showCollection(QString name, QString description, QList<Track> tracks, bool liked,
                                QString kind) {
    if (pages->currentWidget() != collectionPage) {
        returnPage = pages->currentIndex();
        collectionHistory.clear();
    } else {
        collectionHistory.append({collectionName, collectionDescription, collectionKind->text(),
                                  collectionTracks, collectionLiked, collectionQuick});
    }
    pendingCollection.clear();
    collectionQuick = -1;
    collectionName = name;
    collectionDescription = description;
    collectionTracks = tracks;
    collectionLiked = liked;
    collectionTitle->setText(name);
    collectionSubtitle->setText(description);
    collectionKind->setText(kind);
    pages->setCurrentWidget(collectionPage);
    collectionPage->findChild<QScrollArea *>()->verticalScrollBar()->setValue(0);
    renderCollection();
}
void MainWindow::renderCollection() {
    auto scroll = collectionPage->findChild<QScrollArea *>();
    const int position = scroll->verticalScrollBar()->value();
    if (auto focused = QApplication::focusWidget(); focused && collectionPage->isAncestorOf(focused))
        collectionPage->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, scroll, [scroll, position] { scroll->verticalScrollBar()->setValue(position); });
    while (auto item = collectionRows->takeAt(0)) {
        if (auto widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    collectionCover->setPixmap(collectionArtwork(collectionTracks, collectionLiked));
    collectionCount->setText(
        QString("%1 song%2").arg(collectionTracks.size()).arg(collectionTracks.size() == 1 ? "" : "s"));
    if (collectionTracks.isEmpty()) {
        auto empty = new QLabel(collectionLiked ? "Songs you like will appear here."
                                                : "No songs available in this collection.");
        empty->setStyleSheet("color: #A3A3A3; padding: 16px 0;");
        collectionRows->addWidget(empty);
    }
    for (int i = 0; i < collectionTracks.size(); ++i) {
        const auto track = collectionTracks[i];
        auto row = new QWidget(collectionPage);
        row->setObjectName("collectionRow");
        row->setProperty("trackId", track.id);
        row->setFixedHeight(82);
        auto line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(12);
        auto body = new QuickTile(track.title, Icon::None, row);
        body->setObjectName("collectionTrack");
        body->setProperty("artworkUrl", track.thumbnail);
        body->setContent(track.title,
                         track.album.isEmpty() ? track.artist : track.artist + " · " + track.album);
        body->setFixedHeight(82);
        body->setArtwork(artworkCache.value(track.thumbnail));
        line->addWidget(body, 1);
        connect(body, &QPushButton::clicked, this, [this, track] { openTrack(track); });
        connect(body, &QuickTile::playRequested, this, [this, id = track.id] { playCollectionTrack(id); });
        if (track.duration > 0) {
            auto length = new QLabel(timeLabel(qint64(track.duration) * 1000), row);
            length->setStyleSheet("color: #A3A3A3;");
            line->addWidget(length);
        }
        auto heart = new LikeButton(library, track, row);
        heart->setObjectName("likeButton");
        heart->setProperty("trackId", track.id);
        line->addWidget(heart);
        auto more = new IconButton(Icon::More, "More options for " + track.title, row);
        line->addWidget(more);
        connect(more, &QPushButton::clicked, this, [this, more, track] {
            QMenu menu(this);
            auto playAction = menu.addAction("Play");
            auto openAction = menu.addAction(track.albumId.isEmpty() ? "Open song" : "Open album");
            auto likeAction =
                menu.addAction(library->liked(track.id) ? "Remove from liked songs" : "Like song");
            auto selected = menu.exec(more->mapToGlobal(QPoint(0, more->height())));
            if (selected == playAction)
                playCollectionTrack(track.id);
            else if (selected == openAction)
                openTrack(track);
            else if (selected == likeAction)
                library->setLiked(track, !library->liked(track.id));
        });
        collectionRows->addWidget(row);
    }
    for (const auto &track : collectionTracks.mid(0, 4))
        loadArtwork(track.thumbnail, collectionPage, [this, name = collectionName](QPixmap) {
            if (collectionName == name)
                collectionCover->setPixmap(collectionArtwork(collectionTracks, collectionLiked));
        });
    QTimer::singleShot(0, this, &MainWindow::loadVisibleArtwork);
}
void MainWindow::playCollectionTrack(QString id) {
    for (int i = 0; i < collectionTracks.size(); ++i)
        if (collectionTracks[i].id == id) {
            music->play(collectionTracks, i);
            return;
        }
}
void MainWindow::refreshCollectionLikes() {
    if (collectionQuick == 0) {
        const auto liked = library->tracks(true);
        QSet<QString> oldIds, newIds;
        for (const auto &track : collectionTracks)
            oldIds.insert(track.id);
        for (const auto &track : liked)
            newIds.insert(track.id);
        collectionTracks = liked;
        if (!(newIds - oldIds).isEmpty() || liked.isEmpty()) {
            renderCollection();
            return;
        }
        auto scroll = collectionPage->findChild<QScrollArea *>();
        const int position = scroll->verticalScrollBar()->value();
        for (int i = collectionRows->count() - 1; i >= 0; --i) {
            auto row = collectionRows->itemAt(i)->widget();
            if (!row || newIds.contains(row->property("trackId").toString()))
                continue;
            if (auto focused = QApplication::focusWidget(); focused && row->isAncestorOf(focused))
                collectionPage->setFocus(Qt::OtherFocusReason);
            delete collectionRows->takeAt(i);
            row->hide();
            row->deleteLater();
        }
        collectionCount->setText(QString("%1 song%2").arg(liked.size()).arg(liked.size() == 1 ? "" : "s"));
        QTimer::singleShot(0, scroll,
                           [scroll, position] { scroll->verticalScrollBar()->setValue(position); });
    }
}
void MainWindow::loadVisibleArtwork() {
    if (pages->currentWidget() != collectionPage)
        return;
    for (auto body : collectionPage->findChildren<QuickTile *>("collectionTrack")) {
        if (!body->isVisible() ||
            !QRect(body->mapTo(collectionPage, QPoint()), body->size()).intersects(collectionPage->rect()))
            continue;
        const auto url = body->property("artworkUrl").toString();
        if (body->property("artworkRequested").toBool())
            continue;
        body->setProperty("artworkRequested", true);
        loadArtwork(url, body, [body](QPixmap image) { body->setArtwork(image); });
    }
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
    cover->setObjectName("playerCover");
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
    favorite = new LikeButton(library);
    favorite->setObjectName("playerLike");
    info->addWidget(favorite);
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
    play->setHoverFill(true);
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
    seekBar = new SeekSlider;
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
    connect(seekBar, &QSlider::valueChanged, this, [this](int position) { music->seek(position); });
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
    page->setObjectName("searchPage");
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
    auto choices = new QHBoxLayout;
    searchScope = new QComboBox;
    searchScope->setObjectName("searchScope");
    searchScope->addItem("YouTube Music", "online");
    searchScope->addItem("Library", "local");
    searchScope->addItem("Offline files", "offline");
    searchFilter = new QComboBox;
    searchFilter->setObjectName("searchFilter");
    for (auto pair :
         {qMakePair("All", "all"), qMakePair("Songs", "songs"), qMakePair("Videos", "videos"),
          qMakePair("Albums", "albums"), qMakePair("Artists", "artists"),
          qMakePair("Featured playlists", "featured"), qMakePair("Community playlists", "community")})
        searchFilter->addItem(pair.first, pair.second);
    searchFilter->hide();
    auto preferences = new QPushButton("Search preferences");
    preferences->setObjectName("searchPreferences");
    choices->addWidget(searchScope);
    choices->addWidget(searchFilter);
    choices->addStretch();
    choices->addWidget(preferences);
    col->addLayout(choices);
    connect(preferences, &QPushButton::clicked, this, &MainWindow::showSearchPreferences);
    connect(searchFilter, &QComboBox::currentIndexChanged, this, [this] {
        if (fullSearch)
            submitSearch(false);
    });
    connect(searchScope, &QComboBox::currentIndexChanged, this, [this] {
        QSignalBlocker blocker(searchFilter);
        const bool local = searchScope->currentData().toString() != "online";
        searchFilter->clear();
        searchFilter->addItem("All", "all");
        searchFilter->addItem("Songs", "songs");
        if (!local)
            searchFilter->addItem("Videos", "videos");
        searchFilter->addItem("Albums", "albums");
        searchFilter->addItem("Artists", "artists");
        if (!local)
            searchFilter->addItem("Featured playlists", "featured");
        searchFilter->addItem(local ? "Playlists" : "Community playlists", "community");
        submitSearch(!fullSearch);
    });
    searchSuggestions = new QListWidget;
    searchSuggestions->setObjectName("searchSuggestions");
    searchSuggestions->setAccessibleName("Search query suggestions");
    searchSuggestions->setStyleSheet("QListWidget::item { padding: 8px 16px; border: none; } "
                                     "QListWidget::item:hover { background: #202020; }");
    searchSuggestions->setMouseTracking(true);
    searchSuggestions->hide();
    col->addWidget(searchSuggestions);
    const auto chooseQuery = [this](QListWidgetItem *item) {
        const auto query = item->data(Qt::UserRole).toString();
        searchInput->setText(query.isEmpty() ? item->text() : query);
        submitSearch(false);
    };
    connect(searchSuggestions, &QListWidget::itemClicked, this, chooseQuery);
    connect(searchSuggestions, &QListWidget::itemActivated, this, chooseQuery);
    searchSuggestions->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(searchSuggestions, &QWidget::customContextMenuRequested, this, [this](QPoint position) {
        auto item = searchSuggestions->itemAt(position);
        if (!item || !item->data(Qt::UserRole + 1).toBool())
            return;
        QMenu menu(this);
        auto remove = menu.addAction("Remove recent search");
        const auto query = item->data(Qt::UserRole).toString();
        if (menu.exec(searchSuggestions->viewport()->mapToGlobal(position)) == remove) {
            music->searchSystem()->removeHistory(query);
            delete searchSuggestions->takeItem(searchSuggestions->row(item));
            searchSuggestions->setVisible(searchSuggestions->count() > 0);
        }
    });
    searchStatus = new QLabel("Type to search YouTube Music");
    searchStatus->setObjectName("searchStatus");
    searchStatus->setStyleSheet("color: #A3A3A3; font-size: 15px;");
    col->addWidget(searchStatus);
    searchResults = new TrackListWidget;
    searchResults->setObjectName("searchResults");
    searchResults->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    searchResults->setAccessibleName("Search results");
    col->addWidget(searchResults, 1);
    searchMore = new QPushButton("Load more results");
    searchMore->setObjectName("searchMore");
    searchMore->hide();
    col->addWidget(searchMore, 0, Qt::AlignHCenter);
    connect(searchMore, &QPushButton::clicked, music->searchSystem(), &SearchEngine::loadMore);
    searchDebounce = new QTimer(this);
    searchDebounce->setSingleShot(true);
    searchDebounce->setInterval(200);
    connect(searchDebounce, &QTimer::timeout, this, [this] { submitSearch(true); });
    connect(searchInput, &QLineEdit::returnPressed, this, [this] { submitSearch(false); });
    connect(searchInput, &QLineEdit::textChanged, this, [this](const QString &text) {
        searchDebounce->stop();
        music->cancelSearch();
        fullSearch = false;
        searchPage = {};
        searchFilter->hide();
        {
            QSignalBlocker blocker(searchFilter);
            searchFilter->setCurrentIndex(0);
        }
        searchMore->hide();
        results.clear();
        searchResults->clear();
        searchSuggestions->clear();
        searchSuggestions->hide();
        const bool empty = text.trimmed().isEmpty();
        searchStatus->setText(empty ? "Type to search YouTube Music" : "Searching…");
        if (pages->currentIndex() == 1 && !searchInput->isHidden()) {
            if (empty) {
                if (searchScope->currentData().toString() == "online")
                    music->suggest({});
                else
                    searchStatus->setText("Type to search your library");
            } else
                searchDebounce->start();
        }
    });
    connect(searchResults->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (fullSearch && value > 0 && value >= searchResults->verticalScrollBar()->maximum() - 96 &&
            searchMore->isVisible() && searchMore->isEnabled())
            music->searchSystem()->loadMore();
    });
    return page;
}
void MainWindow::submitSearch(bool suggestionsOnly) {
    searchDebounce->stop();
    if (pages->currentIndex() != 1 || searchInput->isHidden())
        return;
    const auto query = searchInput->text().trimmed();
    if (query.isEmpty()) {
        if (searchScope->currentData().toString() == "online")
            music->suggest({});
        else {
            music->cancelSearch();
            searchResults->clear();
            searchSuggestions->hide();
            searchMore->hide();
            searchStatus->setText("Type to search your library");
        }
        return;
    }
    const auto scope = searchScope->currentData().toString();
    if (suggestionsOnly && scope == "online") {
        music->suggest(query);
        return;
    }
    fullSearch = true;
    searchPage = {};
    if (scope == "online") {
        results.clear();
        populateTrackList(searchResults, {});
    }
    searchMore->hide();
    searchSuggestions->hide();
    searchFilter->show();
    searchStatus->setText("Searching…");
    if (scope != "online") {
        music->cancelSearch();
        if (!suggestionsOnly)
            music->searchSystem()->remember(query);
        auto page = SearchEngine::localPage(query, searchFilter->currentData().toString(),
                                            music->localSearchItems(scope == "offline"));
        const auto settings = music->searchSystem()->settings();
        page["items"] = SearchParser::visible(page["items"].toArray(), settings);
        QJsonArray groups;
        for (auto value : page["groups"].toArray()) {
            auto group = value.toObject();
            group["items"] = SearchParser::visible(group["items"].toArray(), settings);
            if (!group["items"].toArray().isEmpty())
                groups.append(group);
        }
        page["groups"] = groups;
        renderSearchPage(page, false);
    } else if (searchFilter->currentData().toString() == "all")
        music->search(query);
    else {
        music->cancelSearch();
        music->searchSystem()->search(query, searchFilter->currentData().toString());
    }
}
void MainWindow::renderSearchPage(QJsonObject page, bool append) {
    if (pages->currentIndex() != 1 || searchInput->isHidden())
        return;
    fullSearch = true;
    searchPage = page;
    searchSuggestions->hide();
    searchFilter->show();
    QJsonArray entries;
    for (auto value : page["groups"].toArray()) {
        const auto group = value.toObject();
        entries.append(QJsonObject{{"kind", "section"}, {"title", group["title"]}});
        for (auto item : group["items"].toArray())
            entries.append(item);
    }
    for (auto item : page["items"].toArray())
        entries.append(item);
    int start = append ? searchResults->count() : 0;
    if (!append)
        populateTrackList(searchResults, {});
    for (int i = start; i < entries.size(); ++i) {
        const auto entry = entries[i].toObject();
        const auto track = Track::fromWire(entry);
        auto item = new QListWidgetItem(trackThumbnail(artworkCache.value(track.thumbnail)),
                                        track.title + "\n" + searchDescription(entry), searchResults);
        item->setData(Qt::UserRole, track.id);
        item->setData(Qt::UserRole + 1, track.thumbnail);
        item->setData(Qt::UserRole + 3, entry);
        const bool section = entry["kind"] == "section";
        item->setData(Qt::UserRole + 8, section);
        item->setSizeHint(QSize(0, section ? 42 : 88));
        if (section)
            item->setFlags(Qt::NoItemFlags);
    }
    searchResults->setUniformItemSizes(false);
    int count = 0;
    for (auto value : entries)
        if (value.toObject()["kind"] != "section")
            ++count;
    searchStatus->setText(count ? QString("%1 results").arg(count) : "No results found.");
    searchMore->setText("Load more results");
    searchMore->setVisible(!page["continuation"].toString().isEmpty());
    searchMore->setEnabled(true);
    QTimer::singleShot(0, searchResults, [this] {
        loadListArtwork(searchResults);
        if (fullSearch && searchMore->isVisible() && searchMore->isEnabled() &&
            searchResults->verticalScrollBar()->maximum() == 0)
            music->searchSystem()->loadMore();
    });
}
void MainWindow::showSearchPreferences() {
    auto dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Search preferences");
    auto form = new QFormLayout(dialog);
    const auto settings = music->searchSystem()->settings();
    auto language = new QLineEdit(settings["language"].toString());
    language->setObjectName("searchLanguage");
    auto country = new QLineEdit(settings["country"].toString());
    country->setObjectName("searchCountry");
    form->addRow("Content language (ar / en)", language);
    form->addRow("Content country (EG / US)", country);
    QHash<QString, QCheckBox *> checks;
    for (auto pair :
         {qMakePair("hideExplicit", "Hide explicit content"), qMakePair("hideVideos", "Hide music videos"),
          qMakePair("pauseHistory", "Pause search history"),
          qMakePair("useLogin", "Use signed-in account for full search")}) {
        auto check = new QCheckBox(pair.second);
        check->setChecked(settings[pair.first].toBool());
        checks[pair.first] = check;
        form->addRow(check);
    }
    auto clear = new QPushButton("Clear search history");
    form->addRow(clear);
    connect(clear, &QPushButton::clicked, music->searchSystem(), &SearchEngine::clearHistory);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (searchInput->text().trimmed().isEmpty())
            music->suggest({});
    });
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, language, country, checks] {
        QJsonObject preferences{{"language", language->text().trimmed()},
                                {"country", country->text().trimmed().toUpper()}};
        for (auto key : checks.keys())
            preferences[key] = checks[key]->isChecked();
        music->searchSystem()->setSettings(preferences);
        if (music->searchSystem()->settings() == preferences)
            dialog->accept();
    });
    dialog->show();
}
QWidget *MainWindow::makeLibrary() {
    auto page = new QWidget;
    page->setObjectName("libraryPage");
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(48, 20, 48, 24);
    layout->setSpacing(14);
    auto header = new QHBoxLayout;
    libraryHeading = heading("Library");
    header->addWidget(libraryHeading);
    header->addStretch();
    for (auto section : {QString("history"), QString("stats")}) {
        auto button = new QPushButton(section == "history" ? "History" : "Stats");
        button->setObjectName("library" + section);
        connect(button, &QPushButton::clicked, this, [this, section] { setLibrarySection(section); });
        header->addWidget(button);
    }
    auto settings = new IconButton(Icon::Settings, "Library preferences");
    settings->setObjectName("libraryPreferences");
    connect(settings, &QPushButton::clicked, this, &MainWindow::showLibraryPreferences);
    header->addWidget(settings);
    layout->addLayout(header);
    auto tabs = new QHBoxLayout;
    for (auto pair : {qMakePair("All", "all"), qMakePair("Songs", "songs"), qMakePair("Albums", "albums"),
                      qMakePair("Artists", "artists"), qMakePair("Playlists", "playlists")}) {
        auto button = new QPushButton(pair.first);
        button->setCheckable(true);
        button->setObjectName("libraryTab_" + QString(pair.second));
        button->setStyleSheet(
            "QPushButton:checked { background: #FFFFFF; color: #000000; border-color: #FFFFFF; }");
        libraryTabs[pair.second] = button;
        connect(button, &QPushButton::clicked, this, [this, section = QString(pair.second)] {
            setLibrarySection(
                music->libraryCatalog()->preferences()["section"].toString() == section ? "all" : section);
        });
        tabs->addWidget(button);
    }
    tabs->addStretch();
    layout->addLayout(tabs);
    auto tools = new QHBoxLayout;
    libraryScope = new QComboBox;
    libraryScope->setObjectName("libraryScope");
    librarySort = new QComboBox;
    librarySort->setObjectName("librarySort");
    libraryDirection = new QPushButton("↓");
    libraryDirection->setFixedWidth(44);
    libraryDirection->setToolTip("Reverse sort order");
    libraryView = new QComboBox;
    libraryView->setObjectName("libraryView");
    libraryView->addItem("List", "list");
    libraryView->addItem("Grid", "grid");
    libraryTags = new QPushButton("Tags");
    libraryRefresh = new QPushButton("Refresh");
    libraryRefresh->setObjectName("libraryRefresh");
    auto create = new QPushButton("New playlist");
    create->setObjectName("libraryNewPlaylist");
    for (auto widget :
         QList<QWidget *>{libraryScope, librarySort, libraryDirection, libraryView, libraryTags})
        tools->addWidget(widget);
    tools->addStretch();
    tools->addWidget(create);
    tools->addWidget(libraryRefresh);
    layout->addLayout(tools);
    libraryStatus = new QLabel;
    libraryStatus->setObjectName("libraryStatus");
    libraryStatus->setWordWrap(true);
    libraryStatus->setStyleSheet("color: #A3A3A3; font-size: 15px;");
    layout->addWidget(libraryStatus);
    libraryList = new TrackListWidget;
    libraryList->setObjectName("musicLibrary");
    libraryList->setAccessibleName("Music library");
    libraryList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    layout->addWidget(libraryList, 1);
    connect(libraryScope, &QComboBox::currentIndexChanged, this,
            [this] { setLibraryOptions({{"scope", libraryScope->currentData().toString()}}); });
    connect(librarySort, &QComboBox::currentIndexChanged, this,
            [this] { setLibraryOptions({{"sort", librarySort->currentData().toString()}}); });
    connect(libraryDirection, &QPushButton::clicked, this, [this] {
        setLibraryOptions({{"descending", !music->libraryCatalog()->preferences()["descending"].toBool()}});
    });
    connect(libraryView, &QComboBox::currentIndexChanged, this, [this] {
        music->libraryCatalog()->setPreferences({{"view", libraryView->currentData().toString()}});
    });
    connect(libraryTags, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        const auto prefs = music->libraryCatalog()->preferences();
        auto selected = prefs["selectedTags"].toArray();
        QHash<QAction *, QString> actions;
        auto clear = menu.addAction("All tags");
        menu.addSeparator();
        for (auto tag : music->libraryCatalog()->tags()) {
            auto action = menu.addAction(tag);
            action->setCheckable(true);
            action->setChecked(selected.contains(tag));
            actions[action] = tag;
        }
        auto chosen = menu.exec(libraryTags->mapToGlobal(QPoint(0, libraryTags->height())));
        if (!chosen)
            return;
        if (chosen == clear)
            selected = {};
        else {
            const auto tag = actions[chosen];
            if (selected.contains(tag)) {
                QJsonArray next;
                for (auto value : selected)
                    if (value.toString() != tag)
                        next.append(value);
                selected = next;
            } else
                selected.append(tag);
        }
        music->libraryCatalog()->setPreferences({{"selectedTags", selected}});
    });
    connect(create, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const auto name =
            QInputDialog::getText(this, "New playlist", "Playlist name", QLineEdit::Normal, {}, &ok);
        if (ok && !name.trimmed().isEmpty())
            library->putPlaylist({}, name, {});
    });
    connect(libraryRefresh, &QPushButton::clicked, this, [this] {
        const auto section = music->libraryCatalog()->preferences()["section"].toString();
        const auto scope = music->libraryCatalog()->preferences()["scope"].toString();
        if (scope == "offline" || scope == "fullOffline" || section == "history" || section == "stats") {
            renderLibrary();
            return;
        }
        music->refreshCatalog(section == "songs"     ? QStringList{scope == "liked" ? "liked" : "songs"}
                              : section == "albums"  ? QStringList{"albums"}
                              : section == "artists" ? QStringList{"artists"}
                              : section == "playlists"
                                  ? QStringList{"playlists"}
                                  : QStringList{"liked", "songs", "albums", "artists", "playlists"});
    });
    connect(music->libraryCatalog(), &LibraryCatalog::changed, this, &MainWindow::renderLibrary);
    connect(music->libraryCatalog(), &LibraryCatalog::busyChanged, this, [this](bool busy) {
        libraryRefresh->setEnabled(!busy);
        libraryRefresh->setText(busy ? "Refreshing…" : "Refresh");
    });
    connect(libraryList->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        QTimer::singleShot(0, libraryList, [this] {
            QStringList ids;
            for (int i = 0; i < libraryList->count(); ++i) {
                const auto entry = libraryList->item(i)->data(Qt::UserRole + 3).toJsonObject();
                if (entry["kind"] == "playlist")
                    ids.append(entry["id"].toString());
            }
            music->libraryCatalog()->setPlaylistOrder(ids);
        });
    });
    populateTrackList(libraryList, {});
    QTimer::singleShot(0, page, [this] { renderLibrary(); });
    return page;
}
void MainWindow::setLibraryOptions(QJsonObject changes) {
    auto prefs = music->libraryCatalog()->preferences();
    auto options = prefs["sectionOptions"].toObject();
    auto current = options[prefs["section"].toString()].toObject();
    for (auto key : changes.keys())
        current[key] = changes[key];
    options[prefs["section"].toString()] = current;
    changes["sectionOptions"] = options;
    music->libraryCatalog()->setPreferences(changes);
}
void MainWindow::setLibrarySection(QString section) {
    const auto prefs = music->libraryCatalog()->preferences();
    const auto options = prefs["sectionOptions"].toObject()[section].toObject();
    music->libraryCatalog()->setPreferences(
        {{"section", section},
         {"scope", options["scope"].toString("liked")},
         {"sort", options["sort"].toString(section == "playlists" ? "custom" : "added")},
         {"descending", options["descending"].toBool(true)}});
}
void MainWindow::renderLibrary() {
    const auto prefs = music->libraryCatalog()->preferences();
    const auto section = prefs["section"].toString("all"), scope = prefs["scope"].toString("liked"),
               sort = prefs["sort"].toString("added");
    const auto signature = QString::fromUtf8(QJsonDocument(prefs).toJson(QJsonDocument::Compact));
    const auto scroll = signature == librarySignature ? libraryList->verticalScrollBar()->value() : 0;
    librarySignature = signature;
    for (auto key : libraryTabs.keys())
        libraryTabs[key]->setChecked(section == key);
    libraryHeading->setText(section == "history" ? "History"
                            : section == "stats" ? "Listening stats"
                                                 : "Library");
    {
        QSignalBlocker block(libraryScope);
        libraryScope->clear();
        if (section == "songs" || section == "albums" || section == "artists") {
            libraryScope->addItem("Liked", "liked");
            libraryScope->addItem("Library", "library");
            if (section != "artists")
                libraryScope->addItem("Offline", "offline");
            if (section == "albums")
                libraryScope->addItem("Fully offline", "fullOffline");
        }
        libraryScope->setCurrentIndex(qMax(0, libraryScope->findData(scope)));
        libraryScope->setVisible(libraryScope->count() > 0);
    }
    {
        QSignalBlocker block(librarySort);
        librarySort->clear();
        for (auto pair : {qMakePair("Added order", "added"), qMakePair("Name", "name")})
            librarySort->addItem(pair.first, pair.second);
        if (section == "songs" || section == "albums")
            librarySort->addItem("Artist", "artist");
        if (section == "albums" || section == "artists" || section == "playlists")
            librarySort->addItem("Song count", "songCount");
        if (section == "albums") {
            librarySort->addItem("Year", "year");
            librarySort->addItem("Duration", "length");
        }
        if (section == "songs" || section == "albums" || section == "artists")
            librarySort->addItem("Listening time", "playTime");
        if (section == "all" || section == "playlists")
            librarySort->addItem("Last updated", "updated");
        if (section == "playlists")
            librarySort->addItem("Custom order", "custom");
        librarySort->setCurrentIndex(qMax(0, librarySort->findData(sort)));
        librarySort->setVisible(section != "history" && section != "stats");
    }
    libraryDirection->setText(prefs["descending"].toBool() ? "↓" : "↑");
    libraryDirection->setVisible(section != "history" && section != "stats" && sort != "custom");
    {
        QSignalBlocker block(libraryView);
        libraryView->setCurrentIndex(prefs["view"] == "grid" ? 1 : 0);
    }
    const bool grid = prefs["view"] == "grid" && section != "history" && section != "stats";
    libraryList->setProperty("libraryGrid", grid);
    libraryList->setViewMode(grid ? QListView::IconMode : QListView::ListMode);
    libraryList->setResizeMode(QListView::Adjust);
    libraryList->setMovement(QListView::Static);
    libraryList->setWrapping(grid);
    libraryList->setSpacing(grid ? 12 : 0);
    if (grid)
        libraryList->setGridSize(prefs["gridSize"] == "small" ? QSize(152, 198) : QSize(220, 260));
    else
        libraryList->setGridSize({});
    auto entries = music->libraryCatalog()->entries(section, scope, sort, prefs["descending"].toBool(),
                                                    music->searchSystem()->settings());
    QList<Track> tracks;
    for (auto value : entries)
        tracks.append(Track::fromWire(value.toObject()));
    populateTrackList(libraryList, tracks);
    libraryList->setIconSize(grid ? QSize(180, 180) : QSize(56, 56));
    for (int i = 0; i < entries.size(); ++i) {
        auto entry = entries[i].toObject();
        if (scope == "offline" || scope == "fullOffline") {
            QJsonArray local;
            for (auto value : entry["localTracks"].toArray()) {
                auto id = value.toObject()["id"].toString();
                if (QFileInfo(library->mediaPath(id)).isFile())
                    local.append(value);
            }
            if (entry.contains("localTracks"))
                entry["localTracks"] = local;
            entry["preferLocal"] = true;
        }
        auto item = libraryList->item(i);
        const auto kind = entry["kind"].toString();
        item->setData(Qt::UserRole + 3, entry);
        item->setIcon(trackThumbnail({}, grid ? 180 : 56));
        item->setSizeHint(grid ? libraryList->gridSize() : QSize(0, 88));
        QString detail = kind == "song" || kind == "video"
                             ? Track::fromWire(entry).artist
                             : QString("%1 songs").arg(entry["songCount"].toInt());
        if (section == "history")
            detail = entry["timestamp"].toString() + " · " + detail;
        if ((kind == "album" || kind == "artist" || kind == "playlist") &&
            entry["browseId"].toString().size() && entry["localTracks"].toArray().isEmpty())
            detail = searchDescription(entry);
        item->setText(entry["title"].toString() + "\n" + detail);
        const bool canPlay = !entry["localTracks"].toArray().isEmpty() ||
                             !entry["browseId"].toString().isEmpty() || kind == "song" || kind == "video";
        item->setData(Qt::UserRole + 10, canPlay);
        auto local = entry["localTracks"].toArray();
        QList<Track> covers;
        for (int coverIndex = 0; coverIndex < qMin(4, local.size()); ++coverIndex)
            covers << Track::fromWire(local[coverIndex].toObject());
        if (entry["section"] == "liked")
            item->setIcon(QIcon(collectionArtwork({}, true)));
        else if (entry["thumbnail"].toString().isEmpty() && !covers.isEmpty())
            item->setIcon(QIcon(collectionArtwork(covers, false)));
        else if (artworkCache.contains(entry["thumbnail"].toString()))
            item->setIcon(trackThumbnail(artworkCache.value(entry["thumbnail"].toString()), grid ? 180 : 56));
        if (kind == "playlist" && sort == "custom")
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        else
            item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    }
    libraryList->setDragDropMode(section == "playlists" && sort == "custom" &&
                                         prefs["selectedTags"].toArray().isEmpty() && !grid
                                     ? QAbstractItemView::InternalMove
                                     : QAbstractItemView::NoDragDrop);
    libraryTags->setVisible(section == "all" || section == "playlists");
    libraryTags->setText(QString("Tags (%1)").arg(prefs["selectedTags"].toArray().size()));
    if (section == "stats") {
        const auto stats = music->libraryCatalog()->statistics();
        libraryStatus->setText(QString("%1 songs heard · %2 minutes listened · %3 listening events")
                                   .arg(stats["songs"].toInt())
                                   .arg(qint64(stats["listeningMs"].toDouble()) / 60000)
                                   .arg(stats["events"].toInt()));
    } else
        libraryStatus->setText(entries.isEmpty()
                                   ? (scope == "fullOffline"
                                          ? "No albums with a known complete offline track list."
                                          : "No items in this view. Change the filter, save items, or "
                                            "refresh your online library.")
                                   : QString("%1 items").arg(entries.size()));
    QTimer::singleShot(0, libraryList, [this, scroll] {
        libraryList->verticalScrollBar()->setValue(scroll);
        loadListArtwork(libraryList);
    });
}
void MainWindow::addSongToPlaylist(const Track &track) {
    QStringList names, ids;
    names << "New playlist…";
    ids << QString{};
    for (auto value : library->snapshot()["playlists"].toArray()) {
        auto entry = value.toObject();
        if (!entry["removed"].toBool() && entry["browseId"].toString().isEmpty()) {
            names << entry["name"].toString();
            ids << entry["id"].toString();
        }
    }
    bool ok = false;
    auto choice = QInputDialog::getItem(this, "Add to playlist", "Playlist", names, 0, false, &ok);
    if (!ok)
        return;
    auto id = ids.value(names.indexOf(choice));
    QString name = choice;
    QList<Track> tracks;
    if (id.isEmpty()) {
        name = QInputDialog::getText(this, "New playlist", "Playlist name", QLineEdit::Normal, {}, &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
    } else
        for (auto value : library->snapshot()["playlists"].toArray())
            if (value.toObject()["id"].toString() == id)
                for (auto song : value.toObject()["songs"].toArray())
                    tracks << Track::fromWire(song.toObject());
    for (const auto &song : tracks)
        if (song.id == track.id)
            return;
    tracks << track;
    library->putPlaylist(id, name, tracks);
}
void MainWindow::libraryContextMenu(QListWidget *list, QPoint position) {
    auto item = list->itemAt(position);
    if (!item)
        return;
    const auto entry = item->data(Qt::UserRole + 3).toJsonObject();
    const auto kind = entry["kind"].toString();
    if (kind == "section")
        return;
    const auto track = Track::fromWire(entry);
    QMenu menu(this);
    auto open = menu.addAction("Open details");
    auto play = menu.addAction("Play");
    play->setEnabled(!item->data(Qt::UserRole + 10).isValid() || item->data(Qt::UserRole + 10).toBool());
    QAction *like = nullptr, *add = nullptr, *save = nullptr, *rename = nullptr, *remove = nullptr,
            *tags = nullptr;
    if (kind == "song" || kind == "video" || kind.isEmpty()) {
        like = menu.addAction(library->liked(track.id) ? "Remove from Liked Songs" : "Like song");
        add = menu.addAction("Add to playlist…");
    }
    if (kind == "album" || kind == "artist" || (kind == "playlist" && !entry["editable"].toBool()))
        save = menu.addAction(music->libraryCatalog()->bookmarked(track.id) ? "Remove from saved library"
                                                                            : "Save to library");
    if (kind == "playlist") {
        tags = menu.addAction("Edit tags…");
        if (entry["editable"].toBool()) {
            rename = menu.addAction("Rename…");
            remove = menu.addAction("Delete local playlist");
        }
    }
    const auto selected = menu.exec(list->viewport()->mapToGlobal(position));
    if (!selected)
        return;
    if (selected == open)
        openSearchResult(entry);
    else if (selected == play) {
        if (entry.contains("localTracks") &&
            (kind == "auto" || entry["browseId"].toString().isEmpty() || entry["preferLocal"].toBool())) {
            QList<Track> tracks;
            for (auto value : entry["localTracks"].toArray())
                tracks << Track::fromWire(value.toObject());
            music->play(tracks);
        } else if (kind == "album" || kind == "artist" || kind == "playlist")
            music->playBrowse(entry["browseId"].toString(), entry["playlistId"].toString());
        else
            music->play({track});
    } else if (selected == like)
        library->setLiked(track, !library->liked(track.id));
    else if (selected == add)
        addSongToPlaylist(track);
    else if (selected == save)
        music->libraryCatalog()->bookmark(entry, !music->libraryCatalog()->bookmarked(track.id));
    else if (selected == tags) {
        bool ok = false;
        QStringList tags;
        for (auto value : entry["tags"].toArray())
            tags << value.toString();
        const auto text = QInputDialog::getText(this, "Playlist tags", "Tags separated by commas",
                                                QLineEdit::Normal, tags.join(", "), &ok);
        if (ok)
            music->libraryCatalog()->setTags(track.id, text.split(','));
    } else if (selected == rename) {
        bool ok = false;
        const auto name = QInputDialog::getText(this, "Rename playlist", "Playlist name", QLineEdit::Normal,
                                                track.title, &ok);
        if (ok && !name.trimmed().isEmpty()) {
            QList<Track> tracks;
            for (auto value : entry["localTracks"].toArray())
                tracks << Track::fromWire(value.toObject());
            library->putPlaylist(track.id, name, tracks);
        }
    } else if (selected == remove)
        library->removePlaylist(track.id);
}
void MainWindow::showLibraryPreferences() {
    auto dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Library preferences");
    auto form = new QFormLayout(dialog);
    const auto prefs = music->libraryCatalog()->preferences();
    QHash<QString, QCheckBox *> checks;
    for (auto pair : {qMakePair("showLiked", "Show Liked Songs"), qMakePair("showOffline", "Show Offline"),
                      qMakePair("showTop", "Show My Top"), qMakePair("showCached", "Show Cached")}) {
        auto check = new QCheckBox(pair.second);
        check->setChecked(prefs[pair.first].toBool());
        checks[pair.first] = check;
        form->addRow(check);
    }
    auto top = new QSpinBox;
    top->setRange(1, 500);
    top->setValue(prefs["topSize"].toInt(50));
    form->addRow("My Top size", top);
    auto size = new QComboBox;
    size->addItem("Big", "big");
    size->addItem("Small", "small");
    size->setCurrentIndex(prefs["gridSize"] == "small" ? 1 : 0);
    form->addRow("Grid covers", size);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, checks, top, size] {
        QJsonObject changes{{"topSize", top->value()}, {"gridSize", size->currentData().toString()}};
        for (auto key : checks.keys())
            changes[key] = checks[key]->isChecked();
        music->libraryCatalog()->setPreferences(changes);
        dialog->accept();
    });
    dialog->show();
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
    searchDebounce->stop();
    music->cancelSearch();
    searchSuggestions->clear();
    searchSuggestions->hide();
    pendingCollection.clear();
    collectionQuick = -1;
    resultsHeading->setText(index == 2 ? "Explore" : "Search");
    searchInput->setVisible(index != 2);
    if (index == 2) {
        searchFilter->hide();
        searchMore->hide();
        pages->setCurrentIndex(1);
        results.clear();
        searchResults->clear();
        searchStatus->setText("Loading Explore…");
        music->explore();
        searchInput->setPlaceholderText("Explore YouTube Music");
    } else {
        pages->setCurrentIndex(index);
        if (index == 1) {
            searchInput->setPlaceholderText("Search songs, artists and albums");
            searchInput->setFocus(Qt::OtherFocusReason);
            results.clear();
            searchResults->clear();
            searchStatus->setText(searchInput->text().trimmed().isEmpty() ? "Type to search YouTube Music"
                                                                          : "Searching…");
            if (!searchInput->text().trimmed().isEmpty())
                searchDebounce->start();
            else
                submitSearch(true);
        }
    }
    nav[index]->setChecked(true);
    if (index == 3) {
        renderLibrary();
        if (!librarySyncStarted && music->loggedIn()) {
            librarySyncStarted = true;
            music->refreshCatalog({"liked", "songs", "albums", "artists", "playlists"});
        }
    }
}
void MainWindow::updateLibrary() {
    auto tracks = library->tracks();
    for (int mix = 0; mix < 2; ++mix) {
        mixes[mix] = tracks;
        // Stable within a day: browsing and playing always use the same collection.
        const auto salt = QDate::currentDate().toString(Qt::ISODate) + QString::number(mix);
        std::sort(mixes[mix].begin(), mixes[mix].end(), [salt](const Track &a, const Track &b) {
            return QCryptographicHash::hash((salt + a.id).toUtf8(), QCryptographicHash::Sha256) <
                   QCryptographicHash::hash((salt + b.id).toUtf8(), QCryptographicHash::Sha256);
        });
        if (mixes[mix].size() > 30)
            mixes[mix] = mixes[mix].mid(0, 30);
    }
    quick[0]->setContent("Liked Songs", "Your collection", Icon::Heart);
    for (int i = 1; i < quick.size(); ++i) {
        auto items = quickTracks(i);
        auto seed = items.value(0);
        if (i < 3)
            quick[i]->setContent(QString("Daily Mix %1").arg(i), "From your library");
        else
            quick[i]->setContent(seed.title.isEmpty() ? "No songs yet" : seed.title,
                                 i < 6 ? "Song Radio" : seed.artist, i < 6 ? Icon::Radio : Icon::None);
        quick[i]->setEnabled(!items.isEmpty());
        quick[i]->setArtwork(collectionArtwork(items, false));
        for (const auto &track : items.mid(0, i < 3 ? 4 : 1))
            loadArtwork(track.thumbnail, quick[i], [this, i](QPixmap) {
                quick[i]->setArtwork(collectionArtwork(quickTracks(i), false));
            });
    }
    for (int i = 0; i < cards.size(); ++i) {
        auto card = cards[i];
        auto track = tracks.value(i);
        card->setTrack(track);
        card->setAccessibleName("Open " + track.title);
        card->setEnabled(!track.id.isEmpty());
        card->setArtwork(artworkCache.value(track.thumbnail));
        loadArtwork(track.thumbnail, card, [card, id = track.id](QPixmap pixmap) {
            if (card->track().id == id)
                card->setArtwork(pixmap);
        });
    }
    if (pages->currentWidget() == collectionPage)
        refreshCollectionLikes();
    auto song = Track::fromWire(playback["song"].toObject());
    favorite->setTrack(song);
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
    music->libraryCatalog()->observePlayback(s, !remote->hasTarget());
    const auto previousSong = playback["song"].toObject();
    const auto previousId = previousSong["id"].toString();
    playback = s;
    auto track = Track::fromWire(s["song"].toObject());
    if (track.id != previousId || track.thumbnail != previousSong["thumbnail"].toString()) {
        QPixmap placeholder(76, 76);
        placeholder.fill(Qt::black);
        QPainter painter(&placeholder);
        drawIcon(painter, Icon::Logo, QRectF(22, 22, 32, 32));
        painter.end();
        cover->setPixmap(placeholder);
        loadArtwork(track.thumbnail, cover, [this, id = track.id, url = track.thumbnail](QPixmap art) {
            const auto song = playback["song"].toObject();
            if (song["id"].toString() != id || song["thumbnail"].toString() != url)
                return;
            const auto scaled = art.scaled(76, 76, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            cover->setPixmap(scaled.copy((scaled.width() - 76) / 2, (scaled.height() - 76) / 2, 76, 76));
        });
    }
    title->setText(track.title.isEmpty() ? "No song playing" : track.title);
    title->setToolTip(track.title);
    artist->setText(track.artist);
    play->setGlyph(s["playing"].toBool() ? Icon::Pause : Icon::Play);
    favorite->setTrack(track);
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
    auto list = new TrackListWidget;
    list->setObjectName("queueTracks");
    QList<Track> tracks;
    for (auto v : playback["queue"].toArray()) {
        auto t = Track::fromWire(v.toObject());
        tracks << t;
    }
    populateTrackList(list, tracks);
    list->setCurrentRow(playback["index"].toInt(-1));
    layout->addWidget(list);
    dialog->show();
}
void MainWindow::updateGate() {
    bool locked = !music->loggedIn() && !preview;
    if (locked && !blur) {
        blur = new QGraphicsBlurEffect(shell);
        blur->setBlurRadius(13);
        blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
        shell->setGraphicsEffect(blur);
    } else if (!locked && blur) {
        shell->setGraphicsEffect(nullptr);
        blur = nullptr;
    }
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
        b->setFixedHeight(qBound(72, width() / 21, 80));
    if (auto page = pages->findChild<QWidget *>("libraryPage")) {
        auto layout = qobject_cast<QVBoxLayout *>(page->layout());
        layout->setContentsMargins(pad, 20, pad, 20);
        layout->setSpacing(width() < 1200 ? 8 : 14);
    }
    if (auto page = pages->findChild<QWidget *>("searchPage")) {
        auto layout = qobject_cast<QVBoxLayout *>(page->layout());
        layout->setContentsMargins(pad, 20, pad, 20);
        layout->setSpacing(qBound(12, width() / 90, 20));
    }
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
