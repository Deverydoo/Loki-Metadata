#include "Exif.h"

#include <QtEndian>

#include <algorithm>

namespace meta::exif {

namespace {

enum class Enc { Ascii, Ucs2, Short, Percent };   // Percent: Windows' RatingPercent, written only

struct ExifTag {
    quint16 tag;
    QString field;
    Enc enc;
};

// IFD0 tags and the fields they hold. Artist is kept in two tags: Artist
// (read first) and Windows' XPAuthor; both are written.
const QList<ExifTag>& exifTags()
{
    static const QList<ExifTag> tags{
        {0x9C9B, "title", Enc::Ucs2},         {0x010E, "description", Enc::Ascii}, {0x9C9F, "subject", Enc::Ucs2},
        {0x9C9C, "comment", Enc::Ucs2},       {0x9C9E, "keywords", Enc::Ucs2},     {0x013B, "artist", Enc::Ascii},
        {0x9C9D, "artist", Enc::Ucs2},        {0x8298, "copyright", Enc::Ascii},   {0x0132, "date", Enc::Ascii},
        {0x0131, "encoder", Enc::Ascii},      {0x010F, "make", Enc::Ascii},        {0x0110, "model", Enc::Ascii},
        {0x4746, "rating", Enc::Short},       {0x4749, "rating", Enc::Percent},
    };
    return tags;
}

int typeSize(quint16 type)
{
    switch (type) {
    case 1: case 2: case 6: case 7: return 1;
    case 3: case 8: return 2;
    case 4: case 9: case 11: return 4;
    case 5: case 10: case 12: return 8;
    default: return 0;
    }
}

struct Tiff {
    QByteArray b;
    bool le = true;

    quint16 u16(qsizetype at) const
    {
        if (at < 0 || at + 2 > b.size()) return 0;
        return le ? qFromLittleEndian<quint16>(b.constData() + at) : qFromBigEndian<quint16>(b.constData() + at);
    }
    quint32 u32(qsizetype at) const
    {
        if (at < 0 || at + 4 > b.size()) return 0;
        return le ? qFromLittleEndian<quint32>(b.constData() + at) : qFromBigEndian<quint32>(b.constData() + at);
    }
    quint32 u32of(const QByteArray& raw) const
    {
        return le ? qFromLittleEndian<quint32>(raw.constData()) : qFromBigEndian<quint32>(raw.constData());
    }
    void put16(QByteArray& out, quint16 v) const
    {
        char x[2];
        if (le) qToLittleEndian(v, x);
        else qToBigEndian(v, x);
        out.append(x, 2);
    }
    void put32(QByteArray& out, quint32 v) const
    {
        char x[4];
        if (le) qToLittleEndian(v, x);
        else qToBigEndian(v, x);
        out.append(x, 4);
    }
};

struct Entry {
    quint16 tag = 0, type = 0;
    quint32 count = 0;
    QByteArray raw;   // the 4 value-or-offset bytes, as stored
};

QList<Entry> readIfd(const Tiff& t, quint32 at, quint32* next)
{
    QList<Entry> out;
    const quint16 n = t.u16(at);
    if (at == 0 || at + 2 + qsizetype(n) * 12 + 4 > t.b.size()) {
        if (next) *next = 0;
        return out;
    }
    for (int i = 0; i < n; ++i) {
        const qsizetype e = at + 2 + i * 12;
        out.append({t.u16(e), t.u16(e + 2), t.u32(e + 4), t.b.mid(e + 8, 4)});
    }
    if (next) *next = t.u32(at + 2 + n * 12);
    return out;
}

QByteArray entryBytes(const Tiff& t, const Entry& e)
{
    const qint64 size = qint64(typeSize(e.type)) * e.count;
    if (size <= 0 || size > t.b.size()) return {};
    if (size <= 4) return e.raw.left(size);
    const quint32 off = t.u32of(e.raw);
    if (qint64(off) + size > qint64(t.b.size())) return {};
    return t.b.mid(off, size);
}

QString decode(const QByteArray& bytes, Enc enc)
{
    if (enc == Enc::Ucs2) {
        QString s;
        for (qsizetype i = 0; i + 1 < bytes.size(); i += 2) {
            const char16_t c = char16_t(quint8(bytes[i]) | (quint8(bytes[i + 1]) << 8));
            if (!c) break;
            s.append(QChar(c));
        }
        return s;
    }
    const qsizetype nul = bytes.indexOf('\0');
    return QString::fromUtf8(nul >= 0 ? bytes.left(nul) : bytes).trimmed();
}

QByteArray encode(const QString& value, Enc enc)
{
    if (enc == Enc::Ucs2) {
        QByteArray b;
        for (QChar c : value) {
            b.append(char(c.unicode() & 0xFF));
            b.append(char(c.unicode() >> 8));
        }
        b.append(2, '\0');
        return b;
    }
    return value.toUtf8() + '\0';
}

QString tagValue(const Tiff& t, const QList<Entry>& ifd, quint16 tag, Enc enc)
{
    for (const Entry& e : ifd) {
        if (e.tag != tag) continue;
        if (enc == Enc::Percent) return {};
        if (enc == Enc::Short) {
            const quint16 v = t.le ? qFromLittleEndian<quint16>(e.raw.constData()) : qFromBigEndian<quint16>(e.raw.constData());
            return v ? QString::number(v) : QString();
        }
        return decode(entryBytes(t, e), enc);
    }
    return {};
}

bool validRating(const QString& v)
{
    bool ok = false;
    const int n = v.trimmed().toInt(&ok);
    return ok && n >= 0 && n <= 5;
}

// Windows' star rating as RatingPercent.
quint16 ratingPercent(int stars)
{
    static const quint16 pct[6]{0, 1, 25, 50, 75, 99};
    return pct[std::clamp(stars, 0, 5)];
}

Tiff tiffOf(const QByteArray& b) { return Tiff{b, b.size() > 0 && b[0] == 'I'}; }

} // namespace

bool holds(const QString& field)
{
    for (const ExifTag& x : exifTags())
        if (x.field == field) return true;
    return false;
}

void read(const QByteArray& tiff, Pairs* tags, Pairs* info)
{
    const Tiff t = tiffOf(tiff);
    quint32 next = 0;
    const QList<Entry> ifd0 = readIfd(t, t.u32(4), &next);
    for (const ExifTag& x : exifTags()) {
        const QString v = tagValue(t, ifd0, x.tag, x.enc);
        if (v.isEmpty()) continue;
        bool have = false;
        for (const auto& [k, old] : *tags) have = have || k == x.field;
        if (!have) tags->append({x.field, v});
    }
    for (const Entry& e : ifd0)
        if (e.tag == 0x8769) {   // the Exif sub-directory: when it was taken
            const QString taken = tagValue(t, readIfd(t, t.u32of(e.raw), nullptr), 0x9003, Enc::Ascii);
            if (!taken.isEmpty()) info->append({QString("Taken"), taken});
        }
}

QByteArray update(const QByteArray& tiff, const QHash<QString, QString>& changes, QStringList* notStored, bool* changed)
{
    *changed = false;
    // The fields EXIF has a place for.
    QHash<QString, QString> exif;
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        if (!holds(it.key())) notStored->append(it.key());
        else if (it.key() == "rating" && !it.value().isEmpty() && !validRating(it.value()))
            notStored->append(it.key());   // not 0 to 5 stars
        else exif.insert(it.key(), it.value());
    }
    if (exif.isEmpty()) return tiff;
    *changed = true;

    Tiff t;
    QList<Entry> ifd0;
    quint32 next = 0;
    if (!tiff.isEmpty()) {
        t = tiffOf(tiff);
        ifd0 = readIfd(t, t.u32(4), &next);
    } else {
        t.b = QByteArray("II*\0\x08\0\0\0", 8);
        t.le = true;
    }

    // Which tags change, to what (SHORTs in the block's byte order).
    QHash<quint16, QByteArray> set;   // empty: remove
    QHash<quint16, quint16> types;
    for (auto it = exif.begin(); it != exif.end(); ++it)
        for (const ExifTag& x : exifTags()) {
            if (x.field != it.key()) continue;
            QByteArray bytes;
            if (!it.value().isEmpty()) {
                if (x.enc == Enc::Short || x.enc == Enc::Percent) {
                    const int stars = it.value().toInt();
                    t.put16(bytes, x.enc == Enc::Short ? quint16(stars) : ratingPercent(stars));
                } else {
                    bytes = encode(it.value(), x.enc);
                }
            }
            set.insert(x.tag, bytes);
            types.insert(x.tag, x.enc == Enc::Ucs2 ? 1 : x.enc == Enc::Ascii ? 2 : 3);
        }

    // The new IFD0: the old entries not being changed, as they are (their
    // offsets still point at data that stays put), plus the changed ones.
    QList<Entry> entries;
    for (const Entry& e : ifd0)
        if (!set.contains(e.tag)) entries.append(e);
    QHash<quint16, QByteArray> outOfLine;
    for (auto it = set.begin(); it != set.end(); ++it) {
        if (it.value().isEmpty()) continue;
        const quint16 type = types.value(it.key());
        Entry e{it.key(), type, quint32(it.value().size() / typeSize(type)), {}};
        if (it.value().size() <= 4) e.raw = it.value().leftJustified(4, '\0');
        else outOfLine.insert(e.tag, it.value());
        entries.append(e);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.tag < b.tag; });

    QByteArray b = t.b;
    if (b.size() & 1) b.append('\0');
    const quint32 ifdAt = quint32(b.size());
    const quint32 dataAt = ifdAt + 2 + quint32(entries.size()) * 12 + 4;
    QByteArray data;
    t.put16(b, quint16(entries.size()));
    for (const Entry& e : entries) {
        t.put16(b, e.tag);
        t.put16(b, e.type);
        t.put32(b, e.count);
        if (outOfLine.contains(e.tag)) {
            t.put32(b, dataAt + quint32(data.size()));
            data += outOfLine.value(e.tag);
            if (data.size() & 1) data.append('\0');
        } else {
            b += e.raw;
        }
    }
    t.put32(b, next);
    b += data;
    QByteArray head;   // point the header at the new IFD0
    t.put32(head, ifdAt);
    b.replace(4, 4, head);
    return b;
}

} // namespace meta::exif
