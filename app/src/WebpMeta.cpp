// WebP metadata: an EXIF chunk (the same IFD0 fields as a JPG, see Exif.h)
// and an XMP chunk if there is one. The image chunks are copied as they are.
// Adding EXIF to a simple WebP gives it the VP8X header the format asks for.

#include "Exif.h"
#include "MetaIO.h"
#include "Xmp.h"

#include <QFile>
#include <QtEndian>

namespace meta {

namespace {

struct Chunk {
    QByteArray id;
    QByteArray data;
};

std::optional<QList<Chunk>> parseWebp(const QByteArray& file)
{
    if (file.size() < 12 || !file.startsWith("RIFF") || file.mid(8, 4) != "WEBP") return std::nullopt;
    QList<Chunk> out;
    for (qsizetype pos = 12; pos + 8 <= file.size();) {
        const quint32 n = qFromLittleEndian<quint32>(file.constData() + pos + 4);
        if (pos + 8 + qsizetype(n) > file.size()) return std::nullopt;
        out.append({file.mid(pos, 4), file.mid(pos + 8, n)});
        pos += 8 + n + (n & 1);
    }
    return out;
}

const QByteArray kExifPrefix("Exif\0\0", 6);

// The TIFF block in an EXIF chunk (some writers start it with "Exif\0\0").
QByteArray tiffOf(const QByteArray& data) { return data.startsWith(kExifPrefix) ? data.mid(6) : data; }

// The picture's size, from whichever header it has.
bool canvas(const QList<Chunk>& chunks, quint32* w, quint32* h, bool* alpha)
{
    *alpha = false;
    for (const Chunk& c : chunks) {
        if (c.id == "ALPH") *alpha = true;
        if (c.id == "VP8X" && c.data.size() >= 10) {
            *w = 1 + (quint32(quint8(c.data[4])) | (quint32(quint8(c.data[5])) << 8) | (quint32(quint8(c.data[6])) << 16));
            *h = 1 + (quint32(quint8(c.data[7])) | (quint32(quint8(c.data[8])) << 8) | (quint32(quint8(c.data[9])) << 16));
            *alpha = quint8(c.data[0]) & 0x10;
            return true;
        }
        if (c.id == "VP8L" && c.data.size() >= 5 && quint8(c.data[0]) == 0x2f) {
            const quint32 bits = qFromLittleEndian<quint32>(c.data.constData() + 1);
            *w = 1 + (bits & 0x3fff);
            *h = 1 + ((bits >> 14) & 0x3fff);
            *alpha = *alpha || ((bits >> 28) & 1);
            return true;
        }
        if (c.id == "VP8 " && c.data.size() >= 10) {
            *w = qFromLittleEndian<quint16>(c.data.constData() + 6) & 0x3fff;
            *h = qFromLittleEndian<quint16>(c.data.constData() + 8) & 0x3fff;
            return true;
        }
    }
    return false;
}

QByteArray chunkBytes(const QByteArray& id, const QByteArray& data)
{
    QByteArray b = id;
    char n[4];
    qToLittleEndian<quint32>(quint32(data.size()), n);
    b.append(n, 4);
    b += data;
    if (data.size() & 1) b += '\0';
    return b;
}

} // namespace

ReadResult readWebp(const QString& path)
{
    ReadResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = "Can't open the file: " + f.errorString();
        return r;
    }
    const auto chunks = parseWebp(f.readAll());
    if (!chunks) {
        r.error = "This isn't a WebP file, or it's damaged.";
        return r;
    }
    r.info.append({QString("Format"), QString("WebP image")});
    quint32 w = 0, h = 0;
    bool alpha = false;
    if (canvas(*chunks, &w, &h, &alpha)) r.info.append({QString("Dimensions"), QString("%1 × %2").arg(w).arg(h)});
    bool lossless = false, animated = false;
    for (const Chunk& c : *chunks) {
        lossless = lossless || c.id == "VP8L";
        animated = animated || c.id == "ANIM";
    }
    r.info.append({QString("Kind"), QString(animated ? "Animated" : lossless ? "Lossless" : "Lossy") + (alpha ? ", with transparency" : "")});
    QByteArray packet;
    for (const Chunk& c : *chunks) {
        if (c.id == "EXIF") exif::read(tiffOf(c.data), &r.tags, &r.info);
        if (c.id == "XMP ") packet = c.data;
    }
    if (!packet.isEmpty()) xmp::read(packet, &r.tags, &r.info);
    r.ok = true;
    return r;
}

QString writeWebp(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return "Can't open the file: " + f.errorString();
    const auto parsed = parseWebp(f.readAll());
    f.close();
    if (!parsed) return "This isn't a WebP file, or it's damaged.";
    QList<Chunk> chunks = *parsed;

    QByteArray oldExif, packet;
    for (const Chunk& c : chunks) {
        if (c.id == "EXIF") oldExif = tiffOf(c.data);
        if (c.id == "XMP ") packet = c.data;
    }
    bool changed = false;
    const QByteArray tiff = exif::update(oldExif, changes, notStored, &changed);
    if (!changed) return {};
    if (!packet.isEmpty()) {
        const QByteArray updated = xmp::update(packet, changes);
        if (!updated.isEmpty()) packet = updated;
    }

    // The chunks again: VP8X first (made when missing), the image as it was,
    // then EXIF and XMP at the end, where the format puts them.
    QList<Chunk> out;
    int vp8x = -1;
    for (const Chunk& c : chunks) {
        if (c.id == "EXIF" || c.id == "XMP ") continue;
        if (c.id == "VP8X") vp8x = int(out.size());
        out.append(c);
    }
    if (vp8x < 0) {
        quint32 w = 0, h = 0;
        bool alpha = false;
        if (!canvas(chunks, &w, &h, &alpha)) return "Can't find the picture's size in this WebP.";
        QByteArray x(10, '\0');
        x[0] = char(alpha ? 0x10 : 0);
        for (int i = 0; i < 3; ++i) {
            x[4 + i] = char(((w - 1) >> (8 * i)) & 0xff);
            x[7 + i] = char(((h - 1) >> (8 * i)) & 0xff);
        }
        out.insert(0, {"VP8X", x});
        vp8x = 0;
    }
    QByteArray& flags = out[vp8x].data;
    flags[0] = char(quint8(flags[0]) | 0x08 | (packet.isEmpty() ? 0 : 0x04));   // has EXIF (and XMP, if there is some)
    out.append({"EXIF", tiff});
    if (!packet.isEmpty()) out.append({"XMP ", packet});

    QByteArray body("WEBP");
    for (const Chunk& c : out) body += chunkBytes(c.id, c.data);
    QByteArray file("RIFF");
    char n[4];
    qToLittleEndian<quint32>(quint32(body.size()), n);
    file.append(n, 4);
    file += body;

    const QString temp = tempPathFor(path);
    QFile t(temp);
    if (!t.open(QIODevice::WriteOnly | QIODevice::Truncate)) return "Can't write in this folder: " + t.errorString();
    if (t.write(file) != file.size()) {
        t.close();
        QFile::remove(temp);
        return "Writing failed (is the disk full?).";
    }
    t.close();
    QString error;
    if (!replaceFile(path, temp, &error)) return error;
    return {};
}

} // namespace meta
