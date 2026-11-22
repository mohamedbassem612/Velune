#pragma once
#include <QFile>
#include <QtEndian>
#include <cmath>
inline bool writePlaybackWav(const QString &path, int milliseconds = 4000) {
    QByteArray bytes;
    auto u16 = [&](quint16 v) {
        char b[2];
        qToLittleEndian(v, b);
        bytes.append(b, 2);
    };
    auto u32 = [&](quint32 v) {
        char b[4];
        qToLittleEndian(v, b);
        bytes.append(b, 4);
    };
    const int samples = 48000 * milliseconds / 1000;
    bytes.append("RIFF", 4);
    u32(36 + samples * 2);
    bytes.append("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(48000);
    u32(96000);
    u16(2);
    u16(16);
    bytes.append("data", 4);
    u32(samples * 2);
    for (int i = 0; i < samples; ++i)
        u16(quint16(qint16(1000 * std::sin(2 * 3.141592653589793 * 997 * i / 48000.))));
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
