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
    ~Discovery() override;
    QList<Device> devices() const;
    void refresh();
    void ingest(const QByteArray &packet);
    void advertise(QString identity, quint16 port, QString name);
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
    QString localId, localName, localInstance;
    quint16 localPort = 0;
    void announce(quint32 ttl = 120);
    void query(const QString &name, quint16 type);
};
} // namespace velune
