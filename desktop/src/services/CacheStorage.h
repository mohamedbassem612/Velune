#pragma once
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <functional>
namespace velune {
class CacheStorage : public QObject {
    Q_OBJECT
  public:
    explicit CacheStorage(QString directory, QObject *parent = nullptr);
    ~CacheStorage() override;
    void setBusyCheck(std::function<bool()> fn) {
        busy = std::move(fn);
    }
    QJsonObject state() const {
        return stats;
    }
    bool setLimits(int audioMiB, int imageMiB);
    bool clear(QString kind);
    void refresh();
    static void trim(const QString &directory);
  signals:
    void changed();
    void message(QString text);

  private:
    QString root;
    QJsonObject stats;
    QTimer timer;
    std::function<bool()> busy;
};
} // namespace velune
