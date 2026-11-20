// PNG and JPG metadata, read and written without touching the pixels.
//
// PNG: the text chunks (tEXt, zTXt, iTXt). Writing replaces all of them with
//      fresh tEXt (or iTXt, for text outside Latin-1) right after the header.
// JPG: the EXIF block's first directory (IFD0), where Windows and most photo
//      tools keep title, author, comments and so on. Writing adds a new IFD0
//      at the end of the EXIF block and points the header at it; everything
//      else in the block (camera settings, GPS, maker notes, the thumbnail)
//      stays byte for byte where it was, so no offset inside it breaks.
// Both: an XMP packet (Photoshop, Lightroom...) is read for the same fields
//      and updated along with them; see Xmp.h.

#include "MetaIO.h"
#include "Exif.h"
#include "Xmp.h"

#include <QFile>
#include <QHash>
#include <QSet>
#include <QtEndian>

namespace meta {

namespace {

QString fail(const QString& why) { return why.isEmpty() ? QString("Unknown error.") : why; }

bool readAll(const QString& path, QByteArray* out, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = "Can't open the file: " + f.errorString();
        return false;
    }
    *out = f.readAll();
    return true;
}

// Writes @p data beside @p path and swaps it in.
QString writeAndReplace(const QString& path, const QByteArray& data)
{
    const QString temp = tempPathFor(path);
    QFile f(temp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return "Can't write in this folder: " + f.errorString();
    if (f.write(data) != data.size()) {
        f.close();
        QFile::remove(temp);
        return "Writing failed (is the disk full?).";
    }
    f.close();
    QString error;
    if (!replaceFile(path, temp, &error)) return error;
    return {};
}

QString valueOf(const Pairs& tags, const QString& key)
{
    for (const auto& [k, v] : tags)
        if (k == key) return v;
    return {};
}

// The tags after the changes: the old ones in order, changed or removed,
// then new ones.
Pairs merged(const Pairs& tags, const QHash<QString, QString>& changes)
{
    Pairs out;
    QSet<QString> seen;
    for (const auto& [k, v] : tags) {
        seen.insert(k);
        const QString value = changes.contains(k) ? changes.value(k) : v;
        if (!value.isEmpty()) out.append({k, value});
    }
    for (auto it = changes.begin(); it != changes.end(); ++it)
        if (!seen.contains(it.key()) && !it.value().isEmpty()) out.append({it.key(), it.value()});
    return out;
}

// --- PNG ---------------------------------------------------------------------

const QByteArray kPngSignature("\x89PNG\r\n\x1a\n", 8);

struct Chunk {
    QByteArray type;
    QByteArray data;
};

quint32 crc32(const QByteArray& type, const QByteArray& data)
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
    for (char b : type) c = table[(c ^ quint8(b)) & 0xFF] ^ (c >> 8);
    for (char b : data) c = table[(c ^ quint8(b)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool parsePng(const QByteArray& file, QList<Chunk>* chunks, QString* error)
{
    if (!file.startsWith(kPngSignature)) {
        *error = "This isn't a PNG file.";
        return false;
    }
    qsizetype pos = 8;
    while (pos + 12 <= file.size()) {
        const quint32 len = qFromBigEndian<quint32>(file.constData() + pos);
        if (pos + 12 + qsizetype(len) > file.size()) break;
        Chunk c{file.mid(pos + 4, 4), file.mid(pos + 8, len)};
        pos += 12 + len;
        const bool end = c.type == "IEND";
        chunks->append(std::move(c));
        if (end) return true;
    }
    *error = "The PNG file is damaged (it ends early).";
    return false;
}

QByteArray inflate(const QByteArray& z)
{
    QByteArray withSize(4, '\0');
    qToBigEndian<quint32>(quint32(qMin<qsizetype>(z.size() * 4, 64 * 1024 * 1024)), withSize.data());
    return qUncompress(withSize + z);
}

// PNG's registered keywords that match a named field.
const QList<QPair<QString, QString>>& pngNames()
{
    static const QList<QPair<QString, QString>> names{
        {"Title", "title"},       {"Author", "artist"},  {"Description", "description"}, {"Copyright", "copyright"},
        {"Creation Time", "date"}, {"Software", "encoder"}, {"Comment", "comment"},          {"Keywords", "keywords"},
    };
    return names;
}

QString fieldFromPng(const QString& keyword)
{
    for (const auto& [png, field] : pngNames())
        if (png.compare(keyword, Qt::CaseInsensitive) == 0) return field;
    return keyword;
}

QString pngFromField(const QString& field)
{
    for (const auto& [png, f] : pngNames())
        if (f == field) return png;
    return field;
}

bool validKeyword(const QString& k)
{
    if (k.isEmpty() || k.size() > 79 || k.front() == ' ' || k.back() == ' ') return false;
    for (QChar c : k)
        if (c.unicode() < 32 || c.unicode() > 255) return false;
    return true;
}

bool isLatin1(const QString& s)
{
    for (QChar c : s)
        if (c.unicode() > 255) return false;
    return true;
}

// The XMP packet's home in a PNG: an iTXt chunk with this keyword.
const QString kPngXmpKeyword = "XML:com.adobe.xmp";

// A text chunk's keyword and text, whichever of the three kinds it is.
bool decodeText(const Chunk& c, QString* keyword, QString* value)
{
    if (c.type != "tEXt" && c.type != "zTXt" && c.type != "iTXt") return false;
    const qsizetype nul = c.data.indexOf('\0');
    if (nul <= 0) return false;
    *keyword = QString::fromLatin1(c.data.left(nul));
    if (c.type == "tEXt") {
        *value = QString::fromLatin1(c.data.mid(nul + 1));
    } else if (c.type == "zTXt") {
        *value = QString::fromLatin1(inflate(c.data.mid(nul + 2)));
    } else {
        if (nul + 3 > c.data.size()) return false;
        const bool compressed = c.data[nul + 1] != 0;
        const qsizetype lang = c.data.indexOf('\0', nul + 3);
        if (lang < 0) return false;
        const qsizetype translated = c.data.indexOf('\0', lang + 1);
        if (translated < 0) return false;
        const QByteArray text = c.data.mid(translated + 1);
        *value = QString::fromUtf8(compressed ? inflate(text) : text);
    }
    return true;
}

QByteArray pngXmp(const QList<Chunk>& chunks)
{
    QString keyword, value;
    for (const Chunk& c : chunks)
        if (decodeText(c, &keyword, &value) && keyword == kPngXmpKeyword) return value.toUtf8();
    return {};
}

Pairs pngText(const QList<Chunk>& chunks)
{
    Pairs tags;
    auto add = [&](const QString& keyword, const QString& value) {
        const QString key = fieldFromPng(keyword);
        for (auto& [k, v] : tags)
            if (k == key) {
                v += "\n" + value;
                return;
            }
        tags.append({key, value});
    };
    QString keyword, value;
    for (const Chunk& c : chunks)
        if (decodeText(c, &keyword, &value) && keyword != kPngXmpKeyword) add(keyword, value);
    return tags;
}

QByteArray chunkBytes(const Chunk& c)
{
    QByteArray b(4, '\0');
    qToBigEndian<quint32>(quint32(c.data.size()), b.data());
    b += c.type;
    b += c.data;
    QByteArray crc(4, '\0');
    qToBigEndian<quint32>(crc32(c.type, c.data), crc.data());
    return b + crc;
}

// --- JPG / EXIF -------------------------------------------------------------

struct Segment {
    quint8 marker = 0;
    QByteArray data;   // without the marker and length
};

// The segments before the image data, and the image data itself (from SOS on).
bool parseJpeg(const QByteArray& file, QList<Segment>* segments, QByteArray* scan, QString* error)
{
    if (file.size() < 4 || quint8(file[0]) != 0xFF || quint8(file[1]) != 0xD8) {
        *error = "This isn't a JPG file.";
        return false;
    }
    qsizetype pos = 2;
    while (pos + 4 <= file.size()) {
        if (quint8(file[pos]) != 0xFF) break;
        while (pos < file.size() && quint8(file[pos]) == 0xFF) ++pos;   // fill bytes
        if (pos >= file.size()) break;
        const quint8 m = quint8(file[pos]);
        const qsizetype markerAt = pos - 1;
        ++pos;
        if (m == 0xDA || m == 0xD9) {
            *scan = file.mid(markerAt);
            return true;
        }
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;   // no length
        if (pos + 2 > file.size()) break;
        const quint16 len = qFromBigEndian<quint16>(file.constData() + pos);
        if (len < 2 || pos + len > file.size()) break;
        segments->append({m, file.mid(pos + 2, len - 2)});
        pos += len;
    }
    *error = "The JPG file is damaged.";
    return false;
}

const QByteArray kExifId("Exif\0\0", 6);
const QByteArray kXmpId("http://ns.adobe.com/xap/1.0/\0", 29);

} // namespace

// --- PNG ---------------------------------------------------------------------

ReadResult readPng(const QString& path)
{
    ReadResult r;
    QByteArray file;
    QList<Chunk> chunks;
    if (!readAll(path, &file, &r.error) || !parsePng(file, &chunks, &r.error)) return r;
    r.tags = pngText(chunks);
    r.info.append({QString("Format"), QString("PNG image")});
    if (const QByteArray packet = pngXmp(chunks); !packet.isEmpty()) xmp::read(packet, &r.tags, &r.info);
    if (!chunks.isEmpty() && chunks[0].type == "IHDR" && chunks[0].data.size() >= 13) {
        const QByteArray& h = chunks[0].data;
        r.info.append({"Dimensions", QString("%1 × %2").arg(qFromBigEndian<quint32>(h.constData())).arg(qFromBigEndian<quint32>(h.constData() + 4))});
        static const QHash<int, QString> colour{{0, "Greyscale"}, {2, "RGB"}, {3, "Palette"}, {4, "Greyscale + alpha"}, {6, "RGBA"}};
        r.info.append({"Colour", QString("%1, %2-bit").arg(colour.value(quint8(h[9]), "Unknown")).arg(quint8(h[8]))});
    }
    r.ok = true;
    return r;
}

QString writePng(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    QString error;
    QByteArray file;
    QList<Chunk> chunks;
    if (!readAll(path, &file, &error) || !parsePng(file, &chunks, &error)) return fail(error);

    QList<Chunk> text;
    // The XMP packet first, updated (it must stay uncompressed iTXt).
    if (QByteArray packet = pngXmp(chunks); !packet.isEmpty()) {
        const QByteArray updated = xmp::update(packet, changes);
        if (!updated.isEmpty()) packet = updated;
        text.append({"iTXt", kPngXmpKeyword.toLatin1() + QByteArray("\0\0\0\0\0", 5) + packet});
    }
    for (const auto& [field, value] : merged(pngText(chunks), changes)) {
        if (field == "rating") continue;
        const QString keyword = pngFromField(field);
        if (!validKeyword(keyword)) {
            if (changes.contains(field)) notStored->append(field);
            continue;
        }
        Chunk c;
        if (isLatin1(value)) {
            c.type = "tEXt";
            c.data = keyword.toLatin1() + '\0' + value.toLatin1();
        } else {
            c.type = "iTXt";
            c.data = keyword.toLatin1() + QByteArray("\0\0\0\0\0", 5) + value.toUtf8();
        }
        text.append(c);
    }

    QByteArray out = kPngSignature;
    for (int i = 0; i < chunks.size(); ++i) {
        const Chunk& c = chunks[i];
        if (c.type == "tEXt" || c.type == "zTXt" || c.type == "iTXt") continue;
        out += chunkBytes(c);
        if (i == 0)   // IHDR: the text goes straight after it
            for (const Chunk& t : text) out += chunkBytes(t);
    }
    return writeAndReplace(path, out);
}

// --- JPG ---------------------------------------------------------------------

ReadResult readJpeg(const QString& path)
{
    ReadResult r;
    QByteArray file, scan;
    QList<Segment> segments;
    if (!readAll(path, &file, &r.error) || !parseJpeg(file, &segments, &scan, &r.error)) return r;
    r.info.append({QString("Format"), QString("JPEG image")});

    QByteArray packet;   // XMP, read after EXIF so EXIF's values come first
    for (const Segment& s : segments) {
        const bool sof = (s.marker >= 0xC0 && s.marker <= 0xCF) && s.marker != 0xC4 && s.marker != 0xC8 && s.marker != 0xCC;
        if (sof && s.data.size() >= 5) {
            r.info.append({"Dimensions", QString("%1 × %2")
                                             .arg(qFromBigEndian<quint16>(s.data.constData() + 3))
                                             .arg(qFromBigEndian<quint16>(s.data.constData() + 1))});
        } else if (s.marker == 0xE1 && s.data.startsWith(kExifId)) {
            exif::read(s.data.mid(kExifId.size()), &r.tags, &r.info);
        } else if (s.marker == 0xE1 && s.data.startsWith(kXmpId)) {
            packet = s.data.mid(kXmpId.size());
        } else if (s.marker == 0xFE) {
            r.info.append({"JPEG comment", QString::fromUtf8(s.data).trimmed()});
        }
    }
    if (!packet.isEmpty()) xmp::read(packet, &r.tags, &r.info);
    r.ok = true;
    return r;
}

QString writeJpeg(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    QString error;
    QByteArray file, scan;
    QList<Segment> segments;
    if (!readAll(path, &file, &error) || !parseJpeg(file, &segments, &scan, &error)) return fail(error);

    int exifAt = -1;
    for (int i = 0; i < segments.size(); ++i)
        if (segments[i].marker == 0xE1 && segments[i].data.startsWith(kExifId)) {
            exifAt = i;
            break;
        }
    bool changed = false;
    const QByteArray tiff = exif::update(exifAt >= 0 ? segments[exifAt].data.mid(kExifId.size()) : QByteArray(), changes,
                                         notStored, &changed);
    if (!changed) return {};
    const QByteArray payload = kExifId + tiff;
    if (payload.size() > 65533) return "The photo's EXIF block is full; it can't take more text.";

    // The XMP packet, if there is one, gets the same changes.
    for (Segment& s : segments) {
        if (s.marker != 0xE1 || !s.data.startsWith(kXmpId)) continue;
        const QByteArray updated = xmp::update(s.data.mid(kXmpId.size()), changes);
        if (updated.isEmpty()) break;   // not readable XML: leave it alone
        if (kXmpId.size() + updated.size() > 65533) return "The photo's XMP block is full; it can't take more text.";
        s.data = kXmpId + updated;
        break;
    }

    if (exifAt >= 0) segments[exifAt].data = payload;
    else segments.insert(!segments.isEmpty() && segments[0].marker == 0xE0 ? 1 : 0, Segment{0xE1, payload});

    QByteArray out("\xFF\xD8", 2);
    for (const Segment& s : segments) {
        out.append(char(0xFF));
        out.append(char(s.marker));
        QByteArray len(2, '\0');
        qToBigEndian<quint16>(quint16(s.data.size() + 2), len.data());
        out += len;
        out += s.data;
    }
    out += scan;
    return writeAndReplace(path, out);
}

} // namespace meta
