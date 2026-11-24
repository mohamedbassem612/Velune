#include "core/Library.h"
#include "services/BackupService.h"
#include "services/CacheStorage.h"
#include "services/LibraryCatalog.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
using namespace velune;
namespace {
bool write(QString path, QByteArray data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}
QByteArray read(QString path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace
class StorageFeatureTests : public QObject {
    Q_OBJECT
  private slots:
    void cacheLimitsRespectActivityAndPreserveDownloads() {
        QTemporaryDir data, downloads;
        QVERIFY(QDir().mkpath(data.filePath("connect/media")));
        const auto a = data.filePath("connect/media/") + QString(64, 'a');
        const auto b = data.filePath("connect/media/") + QString(64, 'b');
        QFile older(a + ".audio");
        QVERIFY(older.open(QIODevice::WriteOnly));
        QVERIFY(older.resize(100ll * 1024 * 1024));
        older.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2), QFileDevice::FileModificationTime);
        older.close();
        QVERIFY(write(a + ".json", "{}"));
        QFile newer(b + ".audio");
        QVERIFY(newer.open(QIODevice::WriteOnly));
        QVERIFY(newer.resize(80ll * 1024 * 1024));
        newer.close();
        QVERIFY(write(b + ".json", "{}"));
        const auto song = downloads.filePath("song.mp3");
        QVERIFY(write(song, "user music"));
        QVERIFY(QDir().mkpath(data.filePath("downloads")));
        QVERIFY(write(data.filePath("downloads/abcdefghijk.source"), "complete conversion source"));
        QVERIFY(write(data.filePath("downloads/user.mp3"), "also user music"));
        bool busy = true;
        CacheStorage cache(data.path());
        cache.setBusyCheck([&] { return busy; });
        QVERIFY(cache.setLimits(128, 16));
        QVERIFY(QFileInfo::exists(a + ".audio"));
        QVERIFY(!cache.clear("audio"));
        busy = false;
        CacheStorage::trim(data.path());
        QVERIFY(!QFileInfo::exists(a + ".audio"));
        QVERIFY(!QFileInfo::exists(a + ".json"));
        QVERIFY(QFileInfo::exists(b + ".audio"));
        QVERIFY(cache.clear("audio"));
        QVERIFY(!QFileInfo::exists(b + ".audio"));
        QVERIFY(cache.clear("temporary"));
        QVERIFY(!QFileInfo::exists(data.filePath("downloads/abcdefghijk.source")));
        QCOMPARE(read(song), QByteArray("user music"));
        QCOMPARE(read(data.filePath("downloads/user.mp3")), QByteArray("also user music"));
        CacheStorage again(data.path());
        QCOMPARE(again.state()["audioMiB"].toInt(), 128);
        QVERIFY(!cache.clear("../"));
    }
    void cacheDoesNotFollowSymlinks() {
#ifdef Q_OS_UNIX
        QTemporaryDir data, external;
        QVERIFY(QDir().mkpath(data.filePath("connect")));
        auto audio = external.filePath(QString(64, 'a') + ".audio");
        QVERIFY(write(audio, "keep me"));
        QVERIFY(QFile::link(external.path(), data.filePath("connect/media")));
        CacheStorage cache(data.path());
        QVERIFY(cache.clear("audio"));
        QCOMPARE(read(audio), QByteArray("keep me"));
#endif
    }
    void backupRoundTripExcludesSecretsAndMusic() {
        QTemporaryDir data, backupDir;
        QString device;
        Track first;
        first.id = "abcdefghijk";
        first.title = "Original";
        {
            Library library(data.path());
            device = library.deviceId();
            library.setLiked(first, true);
            library.putPlaylist("my-playlist", "My playlist", {first});
            LibraryCatalog catalog(&library, {});
            catalog.setPreferences({{"crossfadeSeconds", 4}});
        }
        QVERIFY(write(data.filePath("session.json"), "SECRET_SESSION"));
        QVERIFY(write(data.filePath("downloads.json"), "PRIVATE_FOLDER"));
        BackupService service(data.path());
        const auto path = backupDir.filePath("library.velune.json");
        QVERIFY(service.exportTo(path));
        auto bytes = read(path);
        QVERIFY(!bytes.contains("SECRET_SESSION"));
        QVERIFY(!bytes.contains("PRIVATE_FOLDER"));
        QVERIFY(!bytes.contains("identity-key"));
        QVERIFY(service.inspect(path));
        QCOMPARE(service.preview()["likes"].toInt(), 1);
        QCOMPARE(service.preview()["playlists"].toInt(), 1);
        {
            Library library(data.path());
            library.setLiked(first, false);
            LibraryCatalog catalog(&library, {});
            catalog.setPreferences({{"crossfadeSeconds", 8}});
        }
        QVERIFY(service.stageRestore());
        QString error;
        QVERIFY2(BackupService::applyPending(data.path(), &error), qPrintable(error));
        {
            Library restored(data.path());
            QCOMPARE(restored.deviceId(), device);
            QVERIFY(restored.liked(first.id));
            LibraryCatalog catalog(&restored, {});
            QCOMPARE(catalog.preferences()["crossfadeSeconds"].toInt(), 4);
        }
        QCOMPARE(read(data.filePath("session.json")), QByteArray("SECRET_SESSION"));
        QCOMPARE(read(data.filePath("downloads.json")), QByteArray("PRIVATE_FOLDER"));
        QVERIFY(!QFileInfo::exists(data.filePath(".restore-journal.json")));
        QVERIFY(!QFileInfo::exists(data.filePath(".restore-request.json")));
        QVERIFY(!QDir(data.path()).entryList({"restore-*"}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    }
    void malformedBackupAndInterruptedRestore() {
        QTemporaryDir data, backupDir;
        Library library(data.path());
        QVERIFY(library.checkpoint());
        const auto before = read(data.filePath("library.json"));
        BackupService service(data.path());
        const auto path = backupDir.filePath("invalid.json");
        QVERIFY(write(path, "{\"format\":\"velune-desktop-backup\",\"version\":99}"));
        QVERIFY(!service.inspect(path));
        QVERIFY(!service.stageRestore());
        QCOMPARE(read(data.filePath("library.json")), before);
        const auto folder = "restore-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(QDir().mkpath(data.filePath(folder)));
        QVERIFY(write(data.filePath(folder + "/library.json"), before));
        QVERIFY(write(data.filePath("library.json"), "interrupted write"));
        QJsonObject journal{{"folder", folder}, {"previous", QJsonObject{{"library.json", true}}}};
        QVERIFY(write(data.filePath(".restore-journal.json"), QJsonDocument(journal).toJson()));
        QString error;
        QVERIFY2(BackupService::applyPending(data.path(), &error), qPrintable(error));
        QCOMPARE(read(data.filePath("library.json")), before);
    }
};
QTEST_GUILESS_MAIN(StorageFeatureTests)
#include "StorageFeatureTests.moc"
