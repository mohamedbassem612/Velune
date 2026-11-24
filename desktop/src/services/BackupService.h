#pragma once
#include <QJsonObject>
#include <QObject>
namespace velune {
class BackupService : public QObject {
    Q_OBJECT
  public:
    explicit BackupService(QString directory, QObject *parent = nullptr)
        : QObject(parent), root(std::move(directory)) {}
    bool exportTo(QString path);
    bool inspect(QString path);
    bool stageRestore();
    QJsonObject preview() const {
        return summary;
    }
    static bool applyPending(const QString &directory, QString *error = nullptr);
  signals:
    void message(QString text);
    void previewReady();

  private:
    QString root;
    QJsonObject candidate, summary;
    static bool validate(const QJsonObject &, QString *error);
};
} // namespace velune
