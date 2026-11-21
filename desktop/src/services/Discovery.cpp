#include "Discovery.h"
#include "ConnectClient.h"
#include <QDateTime>
#include <QNetworkInterface>
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
void Discovery::refresh() {
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
        if (r.protocol == 4 && r.id.size() == 64 && r.port > 0 &&
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
        dnsName(p, offset);
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
        if (type == 12 && name == "_velune-player._tcp.local") {
            auto instance = dnsName(p, at, false);
            if (!instance.isEmpty()) {
                auto &r = records[instance.toLower()];
                r.name = instance.section("._velune-player", 0, 0);
                r.seen = QDateTime::currentMSecsSinceEpoch();
                query(instance, 33);
                query(instance, 16);
            }
        } else if ((type == 33 || type == 16) && name.endsWith("._velune-player._tcp.local")) {
            auto &r = records[name];
            r.seen = QDateTime::currentMSecsSinceEpoch();
            if (type == 33 && len >= 6) {
                r.port = number(p, offset + 4);
                at = offset + 6;
                r.target = dnsName(p, at);
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
} // namespace velune
