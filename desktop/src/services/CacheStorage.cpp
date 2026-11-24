#include "CacheStorage.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
namespace velune {
namespace {
QMultiMap<QString, CacheStorage *> managers;
constexpr qint64 MiB = 1024 * 1024;
QString safeDir(const QString &root, const QString &relative) {
    QString path = QFileInfo(root).canonicalFilePath();
    if (path.isEmpty())
        return {};
    for (const auto &part : relative.split('/')) {
        path = QDir(path).filePath(part);
        if (QFileInfo(path).isSymLink())
            return {};
    }
    return path;
}
QFileInfoList cacheFiles(const QString &root, const QString &kind) {
    const auto path = safeDir(root, kind == "temporary" ? "downloads" : "connect/media");
    if (path.isEmpty())
        return {};
    QStringList filters = kind == "audio"    ? QStringList{"*.audio"}
                          : kind == "images" ? QStringList{"*.cover"}
                                             : QStringList{"*.source"};
    auto files = QDir(path).entryInfoList(filters, QDir::Files | QDir::NoSymLinks, QDir::Time);
    QFileInfoList result;
    const QRegularExpression hash("^[a-f0-9]{64}$"), id("^[A-Za-z0-9_-]{11}$");
    for (const auto &file : files)
        if ((kind == "temporary" ? id : hash).match(file.completeBaseName()).hasMatch())
            result.append(file);
    return result;
}
QJsonObject limits(const QString &root) {
    QJsonObject result{{"audioMiB", 1024}, {"imageMiB", 256}};
    QFile f(root + "/storage.json");
    if (f.open(QIODevice::ReadOnly)) {
        auto saved = QJsonDocument::fromJson(f.read(8192)).object();
        result["audioMiB"] = qBound(128, saved["audioMiB"].toInt(1024), 16384);
        result["imageMiB"] = qBound(16, saved["imageMiB"].toInt(256), 2048);
    }
    return result;
}
bool remove(const QFileInfo &file, const QString &kind) {
    if (!QFile::remove(file.filePath()))
        return false;
    if (kind == "audio")
        QFile::remove(file.absolutePath() + '/' + file.completeBaseName() + ".json");
    return true;
}
} // namespace
CacheStorage::CacheStorage(QString directory, QObject *parent)
    : QObject(parent), root(QDir(directory).absolutePath()) {
    managers.insert(root, this);
    timer.setInterval(15000);
    connect(&timer, &QTimer::timeout, this, [this] {
        trim(root);
        refresh();
    });
    timer.start();
    refresh();
}
CacheStorage::~CacheStorage() {
    managers.remove(root, this);
}
void CacheStorage::refresh() {
    stats = limits(root);
    for (auto kind : {QString("audio"), QString("images"), QString("temporary")}) {
        qint64 size = 0;
        const auto files = cacheFiles(root, kind);
        for (const auto &file : files)
            size += file.size();
        stats[kind + "Bytes"] = double(size);
        stats[kind + "Files"] = files.size();
    }
    stats["busy"] = busy && busy();
    emit changed();
}
bool CacheStorage::setLimits(int audioMiB, int imageMiB) {
    QSaveFile f(root + "/storage.json");
    auto bytes = QJsonDocument(QJsonObject{{"audioMiB", qBound(128, audioMiB, 16384)},
                                           {"imageMiB", qBound(16, imageMiB, 2048)}})
                     .toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        emit message("Could not save cache limits.");
        return false;
    }
    trim(root);
    refresh();
    return true;
}
void CacheStorage::trim(const QString &directory) {
    const auto root = QDir(directory).absolutePath();
    // Every active service for this data directory must agree it is safe.
    for (auto *manager : managers.values(root))
        if (manager->busy && manager->busy())
            return;
    const auto config = limits(root);
    for (auto kind : {QString("audio"), QString("images")}) {
        qint64 total = 0;
        const auto maximum = qint64(config[kind == "audio" ? "audioMiB" : "imageMiB"].toInt()) * MiB;
        for (const auto &file : cacheFiles(root, kind)) {
            total += file.size();
            if (total > maximum)
                remove(file, kind);
        }
    }
}
bool CacheStorage::clear(QString kind) {
    if (kind != "audio" && kind != "images" && kind != "temporary")
        return false;
    if (busy && busy()) {
        emit message("Wait for playback, downloads and device transfers to stop before clearing cache.");
        return false;
    }
    bool ok = true;
    for (const auto &file : cacheFiles(root, kind))
        ok = remove(file, kind) && ok;
    refresh();
    emit message(ok ? "Cache cleared. Downloaded MP3s are kept."
                    : "Some cache files could not be removed. Check folder permissions.");
    return ok;
}
} // namespace velune
