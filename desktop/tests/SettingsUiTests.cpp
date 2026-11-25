#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
class SettingsBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
  public:
    QVariantMap values{{"preferences", QVariantMap{{"defaultPage", "Home"},
                                                   {"view", "list"},
                                                   {"rememberPlayback", true},
                                                   {"lyricsFontSize", 18}}},
                       {"downloads", QVariantMap{{"folder", ""}, {"canChangeFolder", true}}},
                       {"settings", QVariantMap{{"language", "en"}, {"country", "US"}}},
                       {"loggedIn", false},
                       {"loginPending", false},
                       {"refreshing", false}};
    QVariantMap state() const {
        return values;
    }
    QString lastAction;
    QVariantMap lastArgs;
    Q_INVOKABLE void action(QString name, QVariantMap args = {}) {
        lastAction = name;
        lastArgs = args;
        if (name == "preferences") {
            auto prefs = values["preferences"].toMap();
            for (auto i = args.begin(); i != args.end(); ++i)
                prefs[i.key()] = i.value();
            values["preferences"] = prefs;
            emit changed();
        }
        if (name == "downloadFolder") {
            auto downloads = values["downloads"].toMap();
            downloads["folder"] = QUrl(args["url"].toString()).toLocalFile();
            values["downloads"] = downloads;
            emit changed();
        }
    }
    Q_INVOKABLE QString artwork(QVariantMap) const {
        return {};
    }
  signals:
    void changed();
    void notification(QString text);
    void downloadFolderRequired();
    void restorePreviewReady();
};
class SettingsUiTests : public QObject {
    Q_OBJECT
  private slots:
    void folderLivesInSettingsAndFirstDownloadRoutesThere() {
        SettingsBackend backend;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl::fromLocalFile(QString(VELUNE_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(), "Main QML must load successfully");
        auto *root = engine.rootObjects().first();
        QVERIFY(QMetaObject::invokeMethod(root, "navigate", Q_ARG(QVariant, QVariant("Downloads"))));
        QCoreApplication::processEvents();
        QVERIFY(!root->findChild<QObject *>("chooseDownloadFolder"));
        QVariantList tracks{QVariantMap{{"id", "abcdefghijk"}, {"title", "Test song"}}};
        QVERIFY(QMetaObject::invokeMethod(root, "downloadItems", Q_ARG(QVariant, QVariant(tracks))));
        QTRY_VERIFY(root->findChild<QObject *>("desktopSettings"));
        QCOMPARE(root->property("page").toString(), "Settings");
        QCOMPARE(root->property("settingsCategory").toString(), "Storage");
        auto *settings = root->findChild<QObject *>("desktopSettings");
        const QStringList removed{"Remember last playback", "Pause when volume reaches zero",
                                  "Download liked songs automatically", "Default playback speed",
                                  "Sleep timer"};
        for (auto *child : settings->findChildren<QObject *>())
            QVERIFY(!removed.contains(child->property("text").toString()));
        QVERIFY(settings->findChild<QObject *>("cacheSettings"));
        QVERIFY(settings->findChild<QObject *>("backupSettings"));
        auto *scroll = settings->property("contentItem").value<QObject *>();
        QVERIFY(scroll);
        QTRY_VERIFY(scroll->property("contentY").toDouble() > 0);
        auto *folder = settings->findChild<QQuickItem *>("chooseDownloadFolder");
        QVERIFY(folder);
        QVERIFY(folder->isVisible());
        QVERIFY(folder->isEnabled());
        if (!qEnvironmentVariableIsEmpty("VELUNE_SETTINGS_CAPTURE")) {
            auto *window = qobject_cast<QQuickWindow *>(root);
            QTest::qWait(150);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("VELUNE_SETTINGS_CAPTURE")));
        }
        auto *appearance = settings->findChild<QQuickItem *>("appearanceSettings");
        QVERIFY(appearance);
        QVERIFY(appearance->isVisible());
        QVERIFY(appearance->y() < settings->findChild<QQuickItem *>("storageSettings")->y());
        auto *search = settings->findChild<QObject *>("settingsSearch");
        QVERIFY(search);
        search->setProperty("text", "font");
        QCoreApplication::processEvents();
        QVERIFY(appearance->isVisible());
        QVERIFY(!folder->isVisible());
        search->setProperty("text", "");
        settings->setProperty("category", "Privacy");
        QCoreApplication::processEvents();
        QVERIFY(settings->findChild<QQuickItem *>("privacySettings")->isVisible());
        QVERIFY(appearance->isVisible());
        // Preference changes come back through the backend, rather than living only in controls.
        QVERIFY(QMetaObject::invokeMethod(settings, "preference",
                                          Q_ARG(QVariant, QVariant("hidePlayerArtwork")),
                                          Q_ARG(QVariant, QVariant(true))));
        QVERIFY(backend.state()["preferences"].toMap()["hidePlayerArtwork"].toBool());
        QTemporaryDir selected;
        QVERIFY(QMetaObject::invokeMethod(
            root, "saveDownloadFolder",
            Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(selected.path()).toString()))));
        QCOMPARE(backend.lastAction, "downloadFolder");
        QCOMPARE(backend.lastArgs["items"].toList().size(), 1);
        QTRY_COMPARE(root->property("page").toString(), "Downloads");
        auto *window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        window->close();
    }
    void defaultPageUsesSavedPreference() {
        SettingsBackend backend;
        auto prefs = backend.values["preferences"].toMap();
        prefs["defaultPage"] = "Downloads";
        backend.values["preferences"] = prefs;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl::fromLocalFile(QString(VELUNE_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QCOMPARE(engine.rootObjects().first()->property("page").toString(), "Downloads");
        qobject_cast<QQuickWindow *>(engine.rootObjects().first())->close();
    }
};
QTEST_MAIN(SettingsUiTests)
#include "SettingsUiTests.moc"
