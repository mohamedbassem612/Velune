#include "services/DownloadService.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace velune;
namespace {
QByteArray sizeBytes(quint32 n, bool sync = false) {
    QByteArray b(4, 0);
    for (int i = 3; i >= 0; --i) {
        b[i] = char(n & (sync ? 127 : 255));
        n >>= sync ? 7 : 8;
    }
    return b;
}
QByteArray frame(QByteArray key, QByteArray value, bool v4 = false) {
    return key + sizeBytes(value.size(), v4) + QByteArray(2, 0) + value;
}
QByteArray utf16(QString s) {
    QByteArray b("\xff\xfe", 2);
    for (auto ch : s) {
        b.append(char(ch.unicode() & 255));
        b.append(char(ch.unicode() >> 8));
    }
    return b;
}
QByteArray androidMp3(QString id, bool v4 = false) {
    // Android jaudiotagger-kt FieldKey.CUSTOM1 uses COMM, description Songs-DB_Custom1.
    QByteArray comment("\1eng", 4);
    comment += utf16("Songs-DB_Custom1") + QByteArray(2, 0) + utf16("velune:" + id);
    auto body = frame("COMM", comment, v4) +
                frame("TIT2", QByteArray(1, 3) + QString::fromUtf8("أغنية اختبار").toUtf8(), v4);
    QByteArray header("ID3", 3);
    header.append(v4 ? 4 : 3);
    header.append(QByteArray(2, 0));
    header += sizeBytes(body.size(), true);
    auto audio = QByteArray::fromHex("fffb9000") + QByteArray(413, 0);
    return header + body + audio + audio;
}
bool write(QString path, QByteArray bytes) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
int mp3Count(QString folder) {
    return QDir(folder).entryList({"*.mp3"}, QDir::Files).size();
}
class FixtureReply : public QNetworkReply {
    QByteArray bytes;
    qint64 offset = 0;

  public:
    FixtureReply(QNetworkRequest request, QByteArray data, int status, QByteArray range, QObject *parent)
        : QNetworkReply(parent), bytes(std::move(data)) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (!range.isEmpty())
            setRawHeader("Content-Range", range);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            emit readyRead();
            if (!isFinished()) {
                setFinished(true);
                emit finished();
            }
        });
    }
    qint64 bytesAvailable() const override {
        return bytes.size() - offset + QNetworkReply::bytesAvailable();
    }
    void abort() override {
        if (!isFinished()) {
            setFinished(true);
            setError(OperationCanceledError, "Cancelled");
            emit finished();
        }
    }

  protected:
    qint64 readData(char *dest, qint64 max) override {
        auto n = qMin(max, qint64(bytes.size() - offset));
        if (n <= 0)
            return -1;
        memcpy(dest, bytes.constData() + offset, size_t(n));
        offset += n;
        return n;
    }
};
class FixtureTransport : public QNetworkAccessManager {
  public:
    QByteArray audio;
    int downloads = 0;
    bool breakRanges = false;
    bool leakedCookies = false;

  protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoing) override {
        if (op == PostOperation) {
            const auto id = QJsonDocument::fromJson(outgoing->readAll()).object()["videoId"].toString();
            QJsonObject format{{"mimeType", "audio/mp4"},
                               {"bitrate", 128000},
                               {"url", "https://fixture.googlevideo.com/" + id}};
            auto data = QJsonDocument(QJsonObject{{"playabilityStatus", QJsonObject{{"status", "OK"}}},
                                                  {"videoDetails", QJsonObject{{"lengthSeconds", "8"}}},
                                                  {"streamingData",
                                                   QJsonObject{{"expiresInSeconds", 3600},
                                                               {"adaptiveFormats", QJsonArray{format}}}}})
                            .toJson();
            return new FixtureReply(request, data, 200, {}, this);
        }
        leakedCookies |= request.hasRawHeader("Cookie") || request.hasRawHeader("Authorization");
        auto range = request.rawHeader("Range").mid(6).split('-');
        const qint64 start = range.value(0).toLongLong(),
                     end = qMin(range.value(1).toLongLong(), qint64(audio.size() - 1));
        const bool probe = request.rawHeader("Range") == "bytes=0-0";
        if (!probe)
            ++downloads;
        auto header = "bytes " + QByteArray::number(start) + '-' + QByteArray::number(end) + '/' +
                      QByteArray::number(audio.size());
        return new FixtureReply(request, audio.mid(start, end - start + 1), breakRanges && !probe ? 200 : 206,
                                header, this);
    }
};
} // namespace
class DownloadFeatureTests : public QObject {
    Q_OBJECT
  private slots:
    void androidTagsAndRename() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (bool v4 : {false, true}) {
            auto path = dir.filePath("anything.mp3");
            QVERIFY(write(path, androidMp3("aB_123-xYz9", v4)));
            auto info = readVeluneMp3(path);
            QVERIFY(info.valid);
            QCOMPARE(info.track.id, "aB_123-xYz9");
            QCOMPARE(info.track.title, QString::fromUtf8("أغنية اختبار"));
            auto renamed = dir.filePath("renamed.mp3");
            QVERIFY(QFile::rename(path, renamed));
            QCOMPARE(readVeluneMp3(renamed).track.id, "aB_123-xYz9");
            QFile::remove(renamed);
        }
    }
    void rejectsIncompleteAndUnrelatedFiles() {
        QTemporaryDir dir;
        auto path = dir.filePath("x.mp3");
        auto bytes = androidMp3("abcdefghijk");
        QVERIFY(write(path, bytes.left(bytes.size() - 500)));
        QVERIFY(!readVeluneMp3(path).valid);
        QVERIFY(write(path, androidMp3("not-an-id")));
        QVERIFY(!readVeluneMp3(path).valid);
        QVERIFY(write(path, QByteArray("velune:abcdefghijk")));
        QVERIFY(!readVeluneMp3(path).valid);
        auto bad = bytes;
        bad.replace(6, 4, QByteArray::fromHex("7f7f7f7f"));
        QVERIFY(write(path, bad));
        QVERIFY(!readVeluneMp3(path).valid);
    }
    void writerMatchesAndroidField() {
        QTemporaryDir dir;
        auto path = dir.filePath("x.mp3");
        auto audio = QByteArray::fromHex("fffb9000") + QByteArray(413, 0);
        QVERIFY(write(path, QByteArray::fromHex("49443303000000000000") + audio + audio));
        QVERIFY(writeVeluneId(path, "abcdefghijk"));
        auto info = readVeluneMp3(path);
        QVERIFY(info.valid);
        QCOMPARE(info.track.id, "abcdefghijk");
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto bytes = f.readAll();
        QVERIFY(bytes.contains(QByteArray("COMM")));
        QVERIFY(bytes.contains(QByteArray("\0engSongs-DB_Custom1\0velune:abcdefghijk", 39)));
        QVERIFY(!writeVeluneId(path, "../invalid"));
    }
    void filenamesArePortable() {
        Track t;
        t.id = "abcdefghijk";
        t.title = "CON";
        QVERIFY(downloadFileName(t).startsWith('_'));
        t.title = "bad:/\\*?\"<>|[] name";
        auto name = downloadFileName(t);
        QVERIFY(name.endsWith("-abcdefghijk.mp3"));
        QVERIFY(!name.contains(QRegularExpression("[\\\\/:*?\"<>|\\[\\]]")));
    }
    void restoreAfterReinstallAndDriveMove() {
        QTemporaryDir oldFolder, movedFolder, firstData, newData;
        QVERIFY(QDir().mkpath(oldFolder.filePath("albums")));
        QVERIFY(write(oldFolder.filePath("albums/renamed.mp3"), androidMp3("abcdefghijk")));
        QVERIFY(write(oldFolder.filePath("unfinished.part"), androidMp3("12345678901")));
        {
            Library library(firstData.path());
            DownloadService service(&library);
            QVERIFY(service.setFolder(oldFolder.path()));
            QTRY_VERIFY(!service.state()["scanning"].toBool());
            QVERIFY(service.contains("abcdefghijk"));
            QVERIFY(!service.contains("12345678901"));
            QVERIFY(!library.liked("abcdefghijk"));
            QVERIFY(!library.mediaPath("abcdefghijk").isEmpty());
        }
        QVERIFY(QFile::copy(oldFolder.filePath("albums/renamed.mp3"), movedFolder.filePath("new-name.mp3")));
        Library library(newData.path());
        DownloadService service(&library);
        QVERIFY(service.setFolder(movedFolder.path()));
        QTRY_VERIFY(!service.state()["scanning"].toBool());
        QVERIFY(service.contains("abcdefghijk"));
        QCOMPARE(library.mediaPath("abcdefghijk"), movedFolder.filePath("new-name.mp3"));
        Track t;
        t.id = "abcdefghijk";
        service.enqueue({t, t});
        QTRY_VERIFY(!service.state()["scanning"].toBool());
        QCOMPARE(service.state()["jobs"].toArray().size(), 0);
        QCOMPARE(mp3Count(movedFolder.path()), 1);
        QVERIFY(QFile::remove(movedFolder.filePath("new-name.mp3")));
        service.scan();
        QTRY_VERIFY(!service.state()["scanning"].toBool());
        QVERIFY(!service.contains(t.id));
        QVERIFY(library.mediaPath(t.id).isEmpty());
    }
    void cachedConversionAndDuplicatePrevention() {
        const auto ffmpeg = AudioResolver::toolPath("ffmpeg");
        QVERIFY2(!ffmpeg.isEmpty(), "Provide ffmpeg on PATH or in the test executable's tools folder.");
        QTemporaryDir folder, data;
        Library library(data.path());
        DownloadService service(&library);
        Track t;
        t.id = "abcdefghijk";
        t.title = QString::fromUtf8("تجربة");
        t.artist = "Artist";
        t.duration = 2;
        const auto stem =
            data.filePath("connect/media/") +
            QString::fromLatin1(QCryptographicHash::hash(t.id.toUtf8(), QCryptographicHash::Sha256).toHex());
        QVERIFY(QDir().mkpath(QFileInfo(stem).absolutePath()));
        QProcess generate;
        generate.start(ffmpeg, {"-nostdin", "-v", "error", "-f", "lavfi", "-i",
                                "sine=frequency=440:duration=2", "-f", "wav", stem + ".audio"});
        QVERIFY(generate.waitForFinished(15000));
        QCOMPARE(generate.exitCode(), 0);
        QVERIFY(write(
            stem + ".json",
            QJsonDocument(QJsonObject{{"id", t.id}, {"length", double(QFileInfo(stem + ".audio").size())}})
                .toJson()));
        QVERIFY(service.setFolder(folder.path()));
        QTRY_VERIFY(!service.state()["scanning"].toBool());
        service.enqueue({t, t});
        QTRY_VERIFY_WITH_TIMEOUT(service.contains(t.id), 30000);
        QCOMPARE(mp3Count(folder.path()), 1);
        const auto result = readVeluneMp3(library.mediaPath(t.id));
        QVERIFY(result.valid);
        QCOMPARE(result.track.title, t.title);
        QCOMPARE(result.track.artist, t.artist);
        QCOMPARE(result.track.duration, 2);
        QVERIFY(!library.liked(t.id));
        service.enqueue({t});
        QTRY_VERIFY(!service.state()["scanning"].toBool());
        QCOMPARE(mp3Count(folder.path()), 1);
        // Cancellation during an asynchronous scan must remove the queued job.
        Track cancelled = t;
        cancelled.id = "12345678901";
        service.enqueue({cancelled});
        service.cancel(cancelled.id);
        QTRY_VERIFY(!service.state()["scanning"].toBool());
        QVERIFY(!service.state()["busy"].toBool());
        QCOMPARE(service.state()["jobs"].toArray().size(), 0);
    }
    void networkDownloadAndInterruptedResponse() {
        const auto ffmpeg = AudioResolver::toolPath("ffmpeg");
        QVERIFY(!ffmpeg.isEmpty());
        QTemporaryDir fixture;
        auto source = fixture.filePath("source.wav");
        QProcess generate;
        generate.start(ffmpeg, {"-nostdin", "-v", "error", "-f", "lavfi", "-i",
                                "sine=frequency=440:duration=8", "-f", "wav", source});
        QVERIFY(generate.waitForFinished(15000));
        QCOMPARE(generate.exitCode(), 0);
        QFile f(source);
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto bytes = f.readAll();
        QVERIFY(bytes.size() > 512 * 1024);
        for (bool broken : {false, true}) {
            QTemporaryDir folder, data;
            Library library(data.path());
            FixtureTransport transport;
            transport.audio = bytes;
            transport.breakRanges = broken;
            DownloadService service(&library, nullptr, &transport);
            service.setCookies([] { return QByteArray("private-cookie"); });
            QVERIFY(service.setFolder(folder.path()));
            QTRY_VERIFY(!service.state()["scanning"].toBool());
            Track t;
            t.id = broken ? "broken12345" : "success1234";
            t.title = "Network fixture";
            service.enqueue({t});
            if (broken) {
                QTRY_VERIFY(!service.state()["error"].toString().isEmpty());
                QVERIFY(!service.contains(t.id));
                QCOMPARE(mp3Count(folder.path()), 0);
                QVERIFY(!QFileInfo::exists(data.filePath("downloads/" + t.id + ".source")));
                transport.breakRanges = false;
                service.enqueue({t});
            }
            QTRY_VERIFY_WITH_TIMEOUT(service.contains(t.id), 30000);
            QVERIFY(transport.downloads >= 2);
            QVERIFY(!transport.leakedCookies);
            QCOMPARE(mp3Count(folder.path()), 1);
            QVERIFY(!QFileInfo::exists(data.filePath("downloads/" + t.id + ".source")));
        }
    }
};
QTEST_GUILESS_MAIN(DownloadFeatureTests)
#include "DownloadFeatureTests.moc"
