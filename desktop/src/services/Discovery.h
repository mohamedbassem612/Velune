#pragma once
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
namespace velune {
struct Device {
    QString name, host, id;
    quint16 port = 0;
};
class Discovery : public QObject {
    Q_OBJECT
  public:
    explicit Discovery(QObject *parent = nullptr);
    QList<Device> devices() const;
    void refresh();
    void ingest(const QByteArray &packet);
  signals:
    void changed();

  private:
    QUdpSocket udp;
    QTimer timer;
    struct Record {
        QString target, name, id, host;
        quint16 port = 0;
        int protocol = 0;
        qint64 seen = 0;
    };
    QMap<QString, Record> records;
    QMap<QString, QString> addresses;
    void query(const QString &name, quint16 type);
};
} // namespace velune
