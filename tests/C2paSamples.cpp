#include "C2paSamples.h"

#include <QCryptographicHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QtEndian>

#include <functional>

namespace {

// --- CBOR, written ------------------------------------------------------------------

QByteArray head(int major, quint64 n)
{
    QByteArray b;
    const char m = char(major << 5);
    if (n < 24) return QByteArray(1, char(m | char(n)));
    int bytes = n < 256 ? 1 : n < 65536 ? 2 : n < (1ull << 32) ? 4 : 8;
    b.append(char(m | char(bytes == 1 ? 24 : bytes == 2 ? 25 : bytes == 4 ? 26 : 27)));
    for (int k = bytes - 1; k >= 0; --k) b.append(char((n >> (8 * k)) & 0xFF));
    return b;
}

QByteArray fixed32(quint32 n)   // always 5 bytes, so a store's size doesn't depend on it
{
    QByteArray b(1, char(0x1a));
    char x[4];
    qToBigEndian(n, x);
    return b + QByteArray(x, 4);
}

QByteArray text(const QString& s)
{
    const QByteArray u = s.toUtf8();
    return head(3, quint64(u.size())) + u;
}

QByteArray bytes(const QByteArray& b) { return head(2, quint64(b.size())) + b; }

QByteArray map(const QList<QPair<QString, QByteArray>>& entries)
{
    QByteArray b = head(5, quint64(entries.size()));
    for (const auto& [k, v] : entries) b += text(k) + v;
    return b;
}

QByteArray array(const QList<QByteArray>& items)
{
    QByteArray b = head(4, quint64(items.size()));
    for (const QByteArray& i : items) b += i;
    return b;
}

// --- JUMBF ------------------------------------------------------------------------------

QByteArray be32(quint32 v)
{
    char x[4];
    qToBigEndian(v, x);
    return QByteArray(x, 4);
}

QByteArray le32(quint32 v)
{
    char x[4];
    qToLittleEndian(v, x);
    return QByteArray(x, 4);
}

QByteArray synchsafe(quint32 n)
{
    QByteArray b(4, '\0');
    for (int k = 0; k < 4; ++k) b[k] = char((n >> (7 * (3 - k))) & 0x7f);
    return b;
}

QByteArray box(const QByteArray& type, const QByteArray& body) { return be32(quint32(8 + body.size())) + type + body; }

QByteArray superbox(const char* kind, const QString& label, const QByteArray& contents)
{
    const QByteArray uuid = QByteArray(kind, 4) + QByteArray::fromHex("00110010800000aa00389b71");
    return box("jumb", box("jumd", uuid + char(0x03) + label.toUtf8() + '\0') + contents);
}

QByteArray store(const QByteArray& hash, quint32 start, quint32 length, bool withHash)
{
    const QByteArray actions = map({{"actions",
        array({map({{"action", text("c2pa.created")},
                    {"softwareAgent", map({{"name", text("Test Generator")}, {"version", text("2.0")}})},
                    {"digitalSourceType", text("http://cv.iptc.org/newscodes/digitalsourcetype/trainedAlgorithmicMedia")},
                    {"when", text("2026-11-20T10:30:00Z")}}),
               map({{"action", text("c2pa.watermarked.unbound")}})})}});
    QByteArray assertions = superbox("cbor", "c2pa.actions.v2", box("cbor", actions));
    if (withHash) {
        const QByteArray binding = map({{"exclusions", array({map({{"start", fixed32(start)}, {"length", fixed32(length)}})})},
                                        {"alg", text("sha256")},
                                        {"hash", bytes(hash)},
                                        {"pad", bytes(QByteArray(2, '\0'))}});
        assertions += superbox("cbor", "c2pa.hash.data", box("cbor", binding));
    }
    const QByteArray claim = map({{"claim_generator_info", map({{"name", text("Test Maker")}, {"version", text("1.0")}})},
                                  {"alg", text("sha256")}});
    const QByteArray manifest = superbox("c2ma", "urn:c2pa:0b6c6f6b-6900-4d65-7461-646174610001",
                                         superbox("c2as", "c2pa.assertions", assertions)
                                             + superbox("c2cl", "c2pa.claim.v2", box("cbor", claim)));
    return superbox("c2pa", "c2pa", manifest);
}

quint32 crc32(const QByteArray& b)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (char ch : b) c = table[(c ^ quint8(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct Placed {
    QByteArray file;
    qint64 start = 0, length = 0;   // the store's container in it
};

// The MP4 chunk offsets at or past @p from moved by @p delta.
void shiftOffsets(QByteArray& b, qint64 from, qint64 to, qint64 at, qint64 delta)
{
    for (qint64 pos = from; pos + 8 <= to;) {
        const qint64 size = qFromBigEndian<quint32>(b.constData() + pos);
        if (size < 8 || pos + size > to) return;
        const QByteArray type = b.mid(pos + 4, 4);
        if (type == "moov" || type == "trak" || type == "mdia" || type == "minf" || type == "stbl") {
            shiftOffsets(b, pos + 8, pos + size, at, delta);
        } else if (type == "stco" || type == "co64") {
            const bool wide = type == "co64";
            const quint32 count = qFromBigEndian<quint32>(b.constData() + pos + 12);
            for (quint32 i = 0; i < count; ++i) {
                const qint64 p = pos + 16 + qint64(i) * (wide ? 8 : 4);
                if (wide) {
                    const quint64 v = qFromBigEndian<quint64>(b.constData() + p);
                    if (qint64(v) >= at) qToBigEndian<quint64>(v + quint64(delta), b.data() + p);
                } else {
                    const quint32 v = qFromBigEndian<quint32>(b.constData() + p);
                    if (qint64(v) >= at) qToBigEndian<quint32>(quint32(v + delta), b.data() + p);
                }
            }
        }
        pos += size;
    }
}

Placed place(const QByteArray& file, C2paHome home, const QByteArray& s, bool split)
{
    Placed p;
    switch (home) {
    case C2paHome::Png: {
        if (!file.startsWith("\x89PNG")) return {};
        const QByteArray chunk = be32(quint32(s.size())) + "caBX" + s + be32(crc32("caBX" + s));
        p = {file.left(33) + chunk + file.mid(33), 33, chunk.size()};
        break;
    }
    case C2paHome::Jpeg: {
        if (!file.startsWith("\xFF\xD8")) return {};
        QList<QByteArray> parts;
        if (split) {
            const qsizetype n = s.size() / 3 + 1;
            for (qsizetype i = 0; i < s.size(); i += n) parts.append(s.mid(i, n));
        } else {
            parts.append(s);
        }
        QByteArray segs;
        for (int i = 0; i < parts.size(); ++i) {
            const QByteArray payload = QByteArray("JP") + char(0) + char(1) + be32(quint32(i + 1)) + (i ? s.left(8) : QByteArray())
                                     + parts[i];
            segs += QByteArray("\xFF\xEB", 2) + char(((payload.size() + 2) >> 8) & 0xFF) + char((payload.size() + 2) & 0xFF) + payload;
        }
        p = {file.left(2) + segs + file.mid(2), 2, segs.size()};
        break;
    }
    case C2paHome::Webp:
    case C2paHome::Wav: {
        if (!file.startsWith("RIFF")) return {};
        QByteArray chunk = "C2PA" + le32(quint32(s.size())) + s;
        if (s.size() & 1) chunk += '\0';
        QByteArray out = file + chunk;
        const QByteArray size = le32(quint32(out.size() - 8));
        out.replace(4, 4, size);
        p = {out, file.size(), chunk.size()};
        break;
    }
    case C2paHome::Mp3: {
        const QByteArray frameBody = QByteArray(1, '\0') + "application/x-c2pa-manifest-store" + '\0' + "c2pa" + '\0'
                                   + "c2pa manifest store" + '\0' + s;
        if (file.startsWith("ID3")) {
            const int version = quint8(file[3]);
            const quint32 old = (quint32(quint8(file[6])) << 21) | (quint32(quint8(file[7])) << 14) | (quint32(quint8(file[8])) << 7)
                              | quint32(quint8(file[9]));
            const quint32 n = quint32(frameBody.size());
            const QByteArray size = version == 4 ? synchsafe(n) : be32(n);
            const QByteArray frame = "GEOB" + size + QByteArray(2, '\0') + frameBody;
            const quint32 total = old + quint32(frame.size());
            p = {file.left(6) + synchsafe(total) + frame + file.mid(10), 10, frame.size()};
        } else {
            const QByteArray frame = "GEOB" + be32(quint32(frameBody.size())) + QByteArray(2, '\0') + frameBody;
            const quint32 total = quint32(frame.size());
            const QByteArray tagHead = QByteArray("ID3\x03\x00\x00", 6) + synchsafe(total);
            p = {tagHead + frame + file, 10, frame.size()};
        }
        break;
    }
    case C2paHome::Mp4: {
        if (file.mid(4, 4) != "ftyp") return {};
        const qint64 at = qFromBigEndian<quint32>(file.constData());
        const QByteArray body = QByteArray::fromHex("d8fec3d61b0e483c92975828877ec481") + QByteArray(4, '\0') + "manifest" + '\0'
                              + QByteArray(8, '\0') + s;
        const QByteArray uuid = box("uuid", body);
        QByteArray out = file.left(at) + uuid + file.mid(at);
        shiftOffsets(out, 0, out.size(), at, uuid.size());   // the media after it moved (old offsets, at or past it)
        p = {out, at, uuid.size()};
        break;
    }
    }
    return p;
}

} // namespace

QByteArray withCredentials(const QByteArray& file, C2paHome home, bool split)
{
    const bool hashed = home != C2paHome::Mp4;
    const QByteArray zero(32, '\0');
    const Placed first = place(file, home, store(zero, 0, 0, hashed), split);
    if (first.file.isEmpty()) return {};
    if (!hashed) return first.file;
    // Same sizes every time, so the store lands in the same place.
    const Placed again = place(file, home, store(zero, quint32(first.start), quint32(first.length), hashed), split);
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(again.file.left(again.start));
    h.addData(again.file.mid(again.start + again.length));
    return place(file, home, store(h.result(), quint32(first.start), quint32(first.length), hashed), split).file;
}

QByteArray withStore(const QByteArray& file, C2paHome home, const QByteArray& s) { return place(file, home, s, false).file; }

QByteArray damagedStore()
{
    QByteArray s = store(QByteArray(32, '\0'), 0, 0, true);
    s.truncate(s.size() * 2 / 3);   // the outer box now claims more than there is
    return s;
}
