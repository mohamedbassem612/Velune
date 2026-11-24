#include "Mp3Tags.h"
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
namespace velune {
namespace {
constexpr int MaxTag = 16 * 1024 * 1024;
quint32 number(const QByteArray &s, bool sync = false) {
    quint32 n = 0;
    for (unsigned char c : s) {
        if (sync && (c & 128))
            return MaxTag + 1;
        n = (n << (sync ? 7 : 8)) | c;
    }
    return n;
}
QByteArray encodedSize(quint32 n, bool sync) {
    QByteArray b(4, '\0');
    for (int i = 3; i >= 0; --i) {
        b[i] = char(n & (sync ? 127 : 255));
        n >>= sync ? 7 : 8;
    }
    return b;
}
QByteArray unsync(QByteArray b) {
    b.replace(QByteArray("\xff\0", 2), QByteArray("\xff", 1));
    return b;
}
QString text(QByteArray b, int encoding) {
    if (encoding == 0)
        return QString::fromLatin1(b);
    if (encoding == 3)
        return QString::fromUtf8(b);
    QStringDecoder decoder(encoding == 2 ? QStringDecoder::Utf16BE : QStringDecoder::Utf16);
    return decoder(b);
}
int terminator(const QByteArray &b, int encoding, int start = 0) {
    const int width = (encoding == 1 || encoding == 2) ? 2 : 1;
    for (int i = start; i + width <= b.size(); i += width)
        if (b[i] == 0 && (width == 1 || b[i + 1] == 0))
            return i;
    return -1;
}
int frameLength(const QByteArray &b, int i) {
    if (i < 0 || i + 4 > b.size())
        return 0;
    const auto h = number(b.mid(i, 4));
    const int version = (h >> 19) & 3, layer = (h >> 17) & 3;
    const int rate = (h >> 12) & 15, sample = (h >> 10) & 3;
    if ((h & 0xffe00000) != 0xffe00000 || version == 1 || layer != 1 || rate == 0 || rate == 15 ||
        sample == 3)
        return 0;
    static const int rates[2][16] = {{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
                                     {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}};
    static const int samples[] = {44100, 48000, 32000};
    int hz = samples[sample] / (version == 3 ? 1 : version == 2 ? 2 : 4);
    return (version == 3 ? 144000 : 72000) * rates[version == 3][rate] / hz + ((h >> 9) & 1);
}
} // namespace
Mp3Info readVeluneMp3(const QString &path) {
    Mp3Info info;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return info;
    auto header = f.read(10);
    if (header.size() != 10 || !header.startsWith("ID3") || (header[3] != 3 && header[3] != 4))
        return info;
    const bool v4 = header[3] == 4;
    const int size = int(number(header.mid(6, 4), true));
    if (size < 0 || size > MaxTag || size + 18 > f.size())
        return info;
    auto body = f.read(size);
    if (body.size() != size)
        return info;
    if (header[5] & 0x80)
        body = unsync(body);
    int pos = 0;
    if (header[5] & 0x40) {
        if (body.size() < 4)
            return info;
        pos = int(number(body.left(4), v4)) + (v4 ? 0 : 4);
        if (pos < 4 || pos > body.size())
            return info;
    }
    while (pos + 10 <= body.size() && body[pos] != 0) {
        const auto key = body.mid(pos, 4);
        const int length = int(number(body.mid(pos + 4, 4), v4));
        if (length <= 0 || length > body.size() - pos - 10)
            return {};
        const unsigned char flags = body[pos + 9];
        auto data = body.mid(pos + 10, length);
        pos += 10 + length;
        // Skip compressed/encrypted/grouped frames; our writer never emits them.
        if (flags & (v4 ? 0x4d : 0xe0))
            continue;
        if (v4 && (flags & 2) && !(header[5] & 0x80))
            data = unsync(data);
        const int enc = static_cast<unsigned char>(data[0]);
        const int width = (enc == 1 || enc == 2) ? 2 : 1;
        if (key == "COMM" && data.size() >= 5) {
            auto comment = data.mid(4);
            const int end = terminator(comment, enc);
            if (end >= 0 && text(comment.left(end), enc) == "Songs-DB_Custom1") {
                auto value = text(comment.mid(end + width), enc).remove(QChar(0)).trimmed();
                if (QRegularExpression("^velune:[A-Za-z0-9_-]{11}$").match(value).hasMatch())
                    info.track.id = value.mid(7);
            }
        } else if (key == "APIC" && data.size() < 8 * 1024 * 1024) {
            const int mimeEnd = data.indexOf('\0', 1);
            if (mimeEnd > 0) {
                const int end = terminator(data, enc, mimeEnd + 2);
                if (end >= 0)
                    info.cover = data.mid(end + width);
            }
        } else if (key.startsWith('T')) {
            auto value = text(data.mid(1), enc).remove(QChar(0));
            if (key == "TIT2")
                info.track.title = value;
            else if (key == "TPE1")
                info.track.artist = value;
            else if (key == "TALB")
                info.track.album = value;
            else if (key == "TLEN")
                info.track.duration = int(value.toLongLong() / 1000);
        }
    }
    // Require two complete Layer III frames, not just a tag or an interrupted .part file.
    auto audio = f.read(16384);
    for (int i = 0; i < 4096 && i + 4 < audio.size(); ++i) {
        const int first = frameLength(audio, i);
        const int second = first ? frameLength(audio, i + first) : 0;
        if (second && i + first + second <= audio.size()) {
            info.valid = !info.track.id.isEmpty();
            break;
        }
    }
    if (info.track.title.isEmpty())
        info.track.title = info.track.id;
    return info;
}
bool writeVeluneId(const QString &path, const QString &id) {
    if (!QRegularExpression("^[A-Za-z0-9_-]{11}$").match(id).hasMatch())
        return false;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        return false;
    auto header = input.read(10);
    if (header.size() != 10 || !header.startsWith("ID3") || header[3] != 3 || header[5] != 0)
        return false;
    const int size = int(number(header.mid(6, 4), true));
    if (size > MaxTag || size > input.size() - 10)
        return false;
    // Insert first, before ffmpeg's frames and padding. Android reads this exact COMM sub-ID.
    QByteArray payload("\0engSongs-DB_Custom1\0", 21);
    payload += "velune:" + id.toLatin1();
    const auto frame =
        QByteArray("COMM") + encodedSize(payload.size(), false) + QByteArray(2, '\0') + payload;
    header.replace(6, 4, encodedSize(size + frame.size(), true));
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(header) != header.size() ||
        output.write(frame) != frame.size())
        return false;
    while (!input.atEnd()) {
        auto bytes = input.read(256 * 1024);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size())
            return false;
    }
    input.close();
    return output.commit();
}
QString downloadFileName(const Track &t) {
    auto name = t.artist.isEmpty() ? t.title : t.artist + " - " + t.title;
    name.replace(QRegularExpression("[\\\\/:*?\"<>|\\[\\]\\x00-\\x1f]"), "_");
    name = name.trimmed().left(80);
    while (name.endsWith('.') || name.endsWith(' '))
        name.chop(1);
    if (name.isEmpty())
        name = "song";
    // Prefix reserved Windows device names; include the same ID suffix as Android.
    if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",
                           QRegularExpression::CaseInsensitiveOption)
            .match(name)
            .hasMatch())
        name.prepend('_');
    return name + '-' + t.id + ".mp3";
}
} // namespace velune
