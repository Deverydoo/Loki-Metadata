#include "Zip.h"

#include <QDateTime>
#include <QFile>
#include <QtEndian>

#include <zlib.h>

namespace meta::zip {

namespace {

quint16 u16(const QByteArray& b, qsizetype at) { return qFromLittleEndian<quint16>(b.constData() + at); }
quint32 u32(const QByteArray& b, qsizetype at) { return qFromLittleEndian<quint32>(b.constData() + at); }
void put16(QByteArray& b, quint16 v) { char x[2]; qToLittleEndian(v, x); b.append(x, 2); }
void put32(QByteArray& b, quint32 v) { char x[4]; qToLittleEndian(v, x); b.append(x, 4); }

QByteArray inflateRaw(const QByteArray& packed, quint32 size)
{
    QByteArray out(qsizetype(size), '\0');
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return {};
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(packed.constData()));
    z.avail_in = uInt(packed.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = uInt(out.size());
    const int r = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    if (r != Z_STREAM_END || z.total_out != size) return {};
    return out;
}

QByteArray deflateRaw(const QByteArray& data)
{
    z_stream z{};
    if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) return {};
    QByteArray out(qsizetype(deflateBound(&z, uLong(data.size()))), '\0');
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    z.avail_in = uInt(data.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = uInt(out.size());
    deflate(&z, Z_FINISH);
    out.truncate(qsizetype(z.total_out));
    deflateEnd(&z);
    return out;
}

quint32 crcOf(const QByteArray& data) { return quint32(crc32(0, reinterpret_cast<const Bytef*>(data.constData()), uInt(data.size()))); }

void dosTime(quint16* time, quint16* date)
{
    const QDateTime now = QDateTime::currentDateTime();
    *time = quint16((now.time().hour() << 11) | (now.time().minute() << 5) | (now.time().second() / 2));
    *date = quint16(((now.date().year() - 1980) << 9) | (now.date().month() << 5) | now.date().day());
}

QByteArray localHeader(const Entry& e)
{
    QByteArray h;
    put32(h, 0x04034b50);
    put16(h, e.needed);
    put16(h, quint16(e.flags & ~0x0008));   // sizes in the header, no data descriptor
    put16(h, e.method);
    put16(h, e.time);
    put16(h, e.date);
    put32(h, e.crc);
    put32(h, e.packedSize);
    put32(h, e.size);
    put16(h, quint16(e.name.size()));
    put16(h, 0);
    return h + e.name;
}

QByteArray centralEntry(const Entry& e)
{
    QByteArray h;
    put32(h, 0x02014b50);
    put16(h, e.madeBy);
    put16(h, e.needed);
    put16(h, e.flags);
    put16(h, e.method);
    put16(h, e.time);
    put16(h, e.date);
    put32(h, e.crc);
    put32(h, e.packedSize);
    put32(h, e.size);
    put16(h, quint16(e.name.size()));
    put16(h, quint16(e.extra.size()));
    put16(h, quint16(e.comment.size()));
    put16(h, 0);   // disk
    put16(h, e.internalAttr);
    put32(h, e.externalAttr);
    put32(h, e.localAt);
    return h + e.name + e.extra + e.comment;
}

QByteArray endRecord(int count, quint32 size, quint32 at, const QByteArray& comment)
{
    QByteArray h;
    put32(h, 0x06054b50);
    put16(h, 0);
    put16(h, 0);
    put16(h, quint16(count));
    put16(h, quint16(count));
    put32(h, size);
    put32(h, at);
    put16(h, quint16(comment.size()));
    return h + comment;
}

// A fresh entry for @p data, deflated unless @p stored.
QPair<Entry, QByteArray> freshEntry(const QByteArray& name, const QByteArray& data, bool stored, const Entry* like = nullptr)
{
    Entry e;
    if (like) {   // keep the old entry's date and attributes
        e = *like;
        e.extra.clear();
        e.comment = like->comment;
    } else {
        dosTime(&e.time, &e.date);
    }
    e.name = name;
    e.flags = e.flags & 0x0800;   // keep only the UTF-8 name flag
    const QByteArray packed = stored ? data : deflateRaw(data);
    e.method = stored ? 0 : 8;
    e.needed = 20;
    e.crc = crcOf(data);
    e.size = quint32(data.size());
    e.packedSize = quint32(packed.size());
    return {e, packed};
}

} // namespace

const Entry* Archive::find(const QByteArray& name) const
{
    for (const Entry& e : entries)
        if (e.name == name) return &e;
    return nullptr;
}

std::optional<Archive> read(QFile& f)
{
    const qint64 size = f.size();
    const qint64 tail = std::min<qint64>(size, 22 + 65535);
    f.seek(size - tail);
    const QByteArray end = f.read(tail);
    qsizetype eocd = -1;
    for (qsizetype i = end.size() - 22; i >= 0; --i)
        if (u32(end, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) return std::nullopt;
    Archive a;
    const quint16 count = u16(end, eocd + 10);
    const quint32 dirSize = u32(end, eocd + 12), dirAt = u32(end, eocd + 16);
    a.comment = end.mid(eocd + 22, u16(end, eocd + 20));
    if (count == 0xFFFF || dirAt == 0xFFFFFFFF || qint64(dirAt) + dirSize > size) return std::nullopt;   // ZIP64
    f.seek(dirAt);
    const QByteArray dir = f.read(dirSize);
    for (qsizetype pos = 0; a.entries.size() < count;) {
        if (pos + 46 > dir.size() || u32(dir, pos) != 0x02014b50) return std::nullopt;
        Entry e;
        e.madeBy = u16(dir, pos + 4);
        e.needed = u16(dir, pos + 6);
        e.flags = u16(dir, pos + 8);
        e.method = u16(dir, pos + 10);
        e.time = u16(dir, pos + 12);
        e.date = u16(dir, pos + 14);
        e.crc = u32(dir, pos + 16);
        e.packedSize = u32(dir, pos + 20);
        e.size = u32(dir, pos + 24);
        const quint16 n = u16(dir, pos + 28), x = u16(dir, pos + 30), c = u16(dir, pos + 32);
        e.internalAttr = u16(dir, pos + 36);
        e.externalAttr = u32(dir, pos + 38);
        e.localAt = u32(dir, pos + 42);
        e.name = dir.mid(pos + 46, n);
        e.extra = dir.mid(pos + 46 + n, x);
        e.comment = dir.mid(pos + 46 + n + x, c);
        if (e.flags & 0x0001) return std::nullopt;   // encrypted
        a.entries.append(e);
        pos += 46 + n + x + c;
    }
    return a;
}

namespace {

// Where an entry's data starts, and how many bytes its local record takes.
bool localSpan(QFile& f, const Entry& e, qint64* dataAt, qint64* span)
{
    f.seek(e.localAt);
    const QByteArray h = f.read(30);
    if (h.size() < 30 || u32(h, 0) != 0x04034b50) return false;
    *dataAt = qint64(e.localAt) + 30 + u16(h, 26) + u16(h, 28);
    qint64 end = *dataAt + e.packedSize;
    if (e.flags & 0x0008) {   // a data descriptor follows: with or without its signature
        f.seek(end);
        const QByteArray d = f.read(4);
        end += d.size() == 4 && u32(d, 0) == 0x08074b50 ? 16 : 12;
    }
    *span = end - e.localAt;
    return end <= f.size();
}

} // namespace

std::optional<QByteArray> extract(QFile& f, const Entry& e)
{
    qint64 dataAt = 0, span = 0;
    if (!localSpan(f, e, &dataAt, &span)) return std::nullopt;
    f.seek(dataAt);
    const QByteArray packed = f.read(e.packedSize);
    if (e.method == 0) return packed;
    if (e.method != 8) return std::nullopt;
    QByteArray data = inflateRaw(packed, e.size);
    if (data.size() != qsizetype(e.size)) return std::nullopt;
    return data;
}

QString rewrite(QFile& in, const Archive& a, const QHash<QByteArray, QByteArray>& replace,
                const QList<QPair<QByteArray, QByteArray>>& add, QFile& out)
{
    QList<Entry> written;
    for (const Entry& e : a.entries) {
        const quint32 at = quint32(out.pos());
        if (replace.contains(e.name)) {
            auto [fresh, packed] = freshEntry(e.name, replace.value(e.name), e.method == 0, &e);
            fresh.localAt = at;
            out.write(localHeader(fresh));
            out.write(packed);
            written.append(fresh);
            continue;
        }
        qint64 dataAt = 0, span = 0;
        if (!localSpan(in, e, &dataAt, &span)) return "The document's zip archive is damaged.";
        in.seek(e.localAt);
        const QByteArray raw = in.read(span);
        if (raw.size() != span) return "Reading the document failed.";
        out.write(raw);
        Entry moved = e;
        moved.localAt = at;
        written.append(moved);
    }
    for (const auto& [name, data] : add) {
        auto [fresh, packed] = freshEntry(name, data, false);
        fresh.localAt = quint32(out.pos());
        out.write(localHeader(fresh));
        out.write(packed);
        written.append(fresh);
    }
    const quint32 dirAt = quint32(out.pos());
    QByteArray dir;
    for (const Entry& e : written) dir += centralEntry(e);
    out.write(dir);
    out.write(endRecord(int(written.size()), quint32(dir.size()), dirAt, a.comment));
    if (out.pos() > 0xFFFFFFFFLL) return "The document would be too big for a plain zip archive.";
    return out.error() == QFile::NoError ? QString() : "Writing failed: " + out.errorString();
}

QByteArray create(const QList<QPair<QByteArray, QByteArray>>& files)
{
    QByteArray b, dir;
    int n = 0;
    for (const auto& [name, data] : files) {
        auto [e, packed] = freshEntry(name, data, n == 0);
        e.localAt = quint32(b.size());
        b += localHeader(e) + packed;
        dir += centralEntry(e);
        ++n;
    }
    const quint32 dirAt = quint32(b.size());
    return b + dir + endRecord(n, quint32(dir.size()), dirAt, {});
}

} // namespace meta::zip
