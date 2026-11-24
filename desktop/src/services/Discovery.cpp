#include "Discovery.h"
#include "ConnectClient.h"
#include <QDateTime>
#include <QNetworkInterface>
#include <QRegularExpression>
namespace velune {
namespace {
quint16 number(const QByteArray &p, int o) {
    return o + 2 <= p.size() ? (quint8(p[o]) << 8) | quint8(p[o + 1]) : 0;
}
QString dnsName(const QByteArray &p, int &offset, bool canonical = true) {
    QStringList labels;
    int pos = offset, depth = 0;
    bool jumped = false;
    while (pos < p.size() && ++depth < 128) {
        int n = quint8(p[pos++]);
        if (!n) {
            if (!jumped)
                offset = pos;
            return canonical ? labels.join('.').toLower() : labels.join('.');
        }
        if ((n & 0xc0) == 0xc0) {
            if (pos >= p.size())
                return {};
            int target = ((n & 0x3f) << 8) | quint8(p[pos++]);
            if (!jumped)
                offset = pos;
            jumped = true;
            pos = target;
            continue;
        }
        if (n > 63 || pos + n > p.size())
            return {};
        labels << QString::fromUtf8(p.mid(pos, n));
        pos += n;
    }
    return {};
}
void add16(QByteArray &p, quint16 n) {
    p.append(char(n >> 8));
    p.append(char(n & 255));
}
void add32(QByteArray &p, quint32 n) {
    add16(p, quint16(n >> 16));
    add16(p, quint16(n));
}
QByteArray encodedName(const QString &name) {
    QByteArray p;
    for (const auto &part : name.split('.')) {
        auto b = part.toUtf8();
        if (b.size() > 63)
            return {};
        p.append(char(b.size()));
        p += b;
    }
    p.append('\0');
    return p;
}
void record(QByteArray &p, QString name, quint16 type, const QByteArray &data, quint32 ttl,
            bool unique = true) {
    p += encodedName(name);
    add16(p, type);
    add16(p, unique ? 0x8001 : 1);
    add32(p, ttl);
    add16(p, quint16(data.size()));
    p += data;
}
} // namespace
Discovery::Discovery(QObject *parent) : QObject(parent) {
    udp.bind(QHostAddress::AnyIPv4, 5353, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    for (const auto &i : QNetworkInterface::allInterfaces())
        if (i.flags().testFlag(QNetworkInterface::IsUp) &&
            i.flags().testFlag(QNetworkInterface::CanMulticast))
            udp.joinMulticastGroup(QHostAddress("224.0.0.251"), i);
    connect(&udp, &QUdpSocket::readyRead, this, [this] {
        while (udp.hasPendingDatagrams()) {
            QByteArray p(int(udp.pendingDatagramSize()), 0);
            QHostAddress source;
            udp.readDatagram(p.data(), p.size(), &source);
            if (ConnectClient::localAddress(source))
                ingest(p);
        }
    });
    timer.setInterval(5000);
    connect(&timer, &QTimer::timeout, this, &Discovery::refresh);
    timer.start();
    QTimer::singleShot(0, this, &Discovery::refresh);
}
void Discovery::query(const QString &name, quint16 type) {
    QByteArray p(12, 0);
    p[5] = 1;
    for (const auto &part : name.split('.')) {
        auto b = part.toUtf8();
        if (b.size() > 63)
            return;
        p.append(char(b.size()));
        p += b;
    }
    p.append('\0');
    add16(p, type);
    add16(p, 1);
    udp.writeDatagram(p, QHostAddress("224.0.0.251"), 5353);
}
Discovery::~Discovery() {
    announce(0);
}
void Discovery::refresh() {
    announce();
    query("_velune-player._tcp.local", 12);
    auto now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = records.begin(); it != records.end();) {
        if (now - it->seen > 20000)
            it = records.erase(it);
        else
            ++it;
    }
    emit changed();
}
QList<Device> Discovery::devices() const {
    QList<Device> result;
    for (const auto &r : records) {
        QString h = r.host.isEmpty() ? addresses.value(r.target) : r.host;
        if (r.protocol == 4 && r.id.size() == 64 && r.id != localId && r.port > 0 &&
            ConnectClient::localAddress(QHostAddress(h)))
            result.append({r.name, h, r.id, r.port});
    }
    return result;
}
void Discovery::ingest(const QByteArray &p) {
    if (p.size() < 12)
        return;
    int offset = 12;
    int questions = number(p, 4), count = number(p, 6) + number(p, 8) + number(p, 10);
    if (questions > 128 || count > 512)
        return;
    for (int i = 0; i < questions; ++i) {
        auto question = dnsName(p, offset);
        if (localPort &&
            (question == "_velune-player._tcp.local" || question == "velune-" + localId.left(16) + ".local" ||
             question == localInstance.toLower()))
            announce();
        offset += 4;
        if (offset > p.size())
            return;
    }
    for (int i = 0; i < count; ++i) {
        auto name = dnsName(p, offset);
        if (name.isEmpty() || offset + 10 > p.size())
            return;
        auto type = number(p, offset), len = number(p, offset + 8);
        offset += 10;
        int end = offset + len;
        if (end > p.size())
            return;
        int at = offset;
        const auto ownInstance = localInstance.toLower();
        if (type == 12 && name == "_velune-player._tcp.local") {
            auto instance = dnsName(p, at, false);
            if (!instance.isEmpty() && instance.toLower() != ownInstance) {
                auto &r = records[instance.toLower()];
                if (r.name.isEmpty())
                    r.name = instance.section("._velune-player", 0, 0);
                r.seen = QDateTime::currentMSecsSinceEpoch();
                if (!r.port || r.target.isEmpty())
                    query(instance, 33);
                if (!r.protocol || r.id.isEmpty())
                    query(instance, 16);
            }
        } else if ((type == 33 || type == 16) && name != ownInstance &&
                   name.endsWith("._velune-player._tcp.local")) {
            auto &r = records[name];
            r.seen = QDateTime::currentMSecsSinceEpoch();
            if (type == 33 && len >= 6) {
                r.port = number(p, offset + 4);
                at = offset + 6;
                r.target = dnsName(p, at);
                if (!addresses.contains(r.target))
                    query(r.target, 1);
            }
            if (type == 16)
                while (at < end) {
                    int l = quint8(p[at++]);
                    if (at + l > end)
                        return;
                    auto txt = p.mid(at, l);
                    at += l;
                    auto eq = txt.indexOf('=');
                    if (eq < 0)
                        continue;
                    auto k = txt.left(eq), v = txt.mid(eq + 1);
                    if (k == "id")
                        r.id = QString::fromUtf8(v);
                    if (k == "protocol")
                        r.protocol = v.toInt();
                    if (k == "name")
                        r.name = QString::fromUtf8(v);
                }
        } else if (type == 1 && len == 4)
            addresses[name] = QString("%1.%2.%3.%4")
                                  .arg(quint8(p[offset]))
                                  .arg(quint8(p[offset + 1]))
                                  .arg(quint8(p[offset + 2]))
                                  .arg(quint8(p[offset + 3]));
        offset = end;
    }
    emit changed();
}
void Discovery::advertise(QString identity, quint16 port, QString name) {
    if (localPort && (identity != localId || port != localPort))
        announce(0);
    localId = identity;
    localPort = port;
    localName = name.replace('.', '-').left(40);
    const auto label = localName.isEmpty()
                           ? QString("Desktop")
                           : QString(localName).replace(QRegularExpression("[^A-Za-z0-9_-]"), "-").left(32);
    localInstance = "Velune-" + label + '-' + localId.left(8) + "._velune-player._tcp.local";
    announce();
}
void Discovery::announce(quint32 ttl) {
    if (localId.size() != 64 || !localPort)
        return;
    const auto target = "velune-" + localId.left(16) + ".local";
    // The fingerprint suffix avoids service-name collisions on multi-device LANs.
    const auto instance = localInstance;
    const auto service = QString("_velune-player._tcp.local");
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!iface.flags().testFlag(QNetworkInterface::IsUp) ||
            iface.flags().testFlag(QNetworkInterface::IsLoopBack) ||
            !iface.flags().testFlag(QNetworkInterface::CanMulticast))
            continue;
        for (const auto &entry : iface.addressEntries()) {
            const auto address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol || !ConnectClient::localAddress(address))
                continue;
            QByteArray packet(12, 0);
            packet[2] = char(0x84);
            packet[7] = 4;
            record(packet, service, 12, encodedName(instance), ttl, false);
            QByteArray srv(4, 0);
            add16(srv, localPort);
            srv += encodedName(target);
            record(packet, instance, 33, srv, ttl);
            QByteArray txt;
            for (const auto &value : {QByteArray("id=") + localId.toUtf8(), QByteArray("protocol=4"),
                                      QByteArray("name=") + localName.toUtf8(), QByteArray("renderer=1")}) {
                txt.append(char(value.size()));
                txt += value;
            }
            record(packet, instance, 16, txt, ttl);
            QByteArray ip;
            add32(ip, address.toIPv4Address());
            record(packet, target, 1, ip, ttl);
            udp.setMulticastInterface(iface);
            udp.writeDatagram(packet, QHostAddress("224.0.0.251"), 5353);
        }
    }
}
} // namespace velune
