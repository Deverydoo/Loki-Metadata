// MP4, M4A, M4V and MOV tags, written without FFmpeg.
//
// The iTunes item list (moov/udta/meta/ilst), which Windows, iTunes and most
// players read, in MP4, M4A, M4V and MOV alike (Windows writes it into MOV
// files too). MOV also keeps QuickTime's own text items in moov/udta, kept in
// step for the fields they cover. The star rating lives where Windows puts
// it: WM/SharedUserRating in Microsoft's Xtra box. Only the items for changed
// fields are rebuilt; every other box (the media data, cover art, GPS, camera
// data, XMP...) is copied as it is.
//
// When the new moov is a different size and media data sits after it (a
// "fast start" file, or a fragmented recording), every offset pointing past
// it is moved by the difference: the chunk offset tables (stco/co64), the
// fragment base offsets (tfhd) and the fragment index (tfra).

#include "MetaIO.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <functional>

namespace meta {

namespace {

QByteArray cc(const char* s) { return QByteArray(1, char(0xA9)) + s; }   // the copyright sign that starts Apple's names

struct Box {
    QByteArray type;
    qint64 at = 0;     // where its header starts
    qint64 size = 0;   // header included
    int head = 8;
    qint64 bodyAt() const { return at + head; }
    qint64 end() const { return at + size; }
};

quint32 u32(const QByteArray& b, qint64 at) { return qFromBigEndian<quint32>(b.constData() + at); }
quint64 u64(const QByteArray& b, qint64 at) { return qFromBigEndian<quint64>(b.constData() + at); }

QByteArray be16(quint16 v)
{
    QByteArray b(2, '\0');
    qToBigEndian(v, b.data());
    return b;
}

QByteArray be32(quint32 v)
{
    QByteArray b(4, '\0');
    qToBigEndian(v, b.data());
    return b;
}

QByteArray box(const QByteArray& type, const QByteArray& body) { return be32(quint32(8 + body.size())) + type + body; }

// The boxes in b between from and to. Nothing when they don't add up.
std::optional<QList<Box>> boxesIn(const QByteArray& b, qint64 from, qint64 to)
{
    QList<Box> out;
    for (qint64 pos = from; pos + 8 <= to;) {   // fewer than 8 left: QuickTime's terminator
        Box x{b.mid(pos + 4, 4), pos, u32(b, pos), 8};
        if (x.size == 1) {
            if (pos + 16 > to) return std::nullopt;
            x.size = qint64(u64(b, pos + 8));
            x.head = 16;
        } else if (x.size == 0) {
            x.size = to - pos;
        }
        if (x.size < x.head || pos + x.size > to) return std::nullopt;
        out.append(x);
        pos += x.size;
    }
    return out;
}

std::optional<Box> child(const QByteArray& b, const Box& parent, const QByteArray& type, qint64 skip = 0)
{
    const auto kids = boxesIn(b, parent.bodyAt() + skip, parent.end());
    if (!kids) return std::nullopt;
    for (const Box& k : *kids)
        if (k.type == type) return k;
    return std::nullopt;
}

std::optional<QList<Box>> topLevel(QFile& f)
{
    QList<Box> out;
    const qint64 size = f.size();
    for (qint64 pos = 0; pos + 8 <= size;) {
        f.seek(pos);
        const QByteArray h = f.read(16);
        Box x{h.mid(4, 4), pos, u32(h, 0), 8};
        if (x.size == 1) {
            if (h.size() < 16) return std::nullopt;
            x.size = qint64(u64(h, 8));
            x.head = 16;
        } else if (x.size == 0) {
            x.size = size - pos;
        }
        if (x.size < x.head || pos + x.size > size) return std::nullopt;
        out.append(x);
        pos += x.size;
    }
    return out;
}

// A meta box is a "full box" (4 bytes of version and flags before its
// children) in MP4, but not always in QuickTime files.
qint64 metaSkip(const QByteArray& b, const Box& meta) { return b.mid(meta.bodyAt() + 4, 4) == "hdlr" ? 0 : 4; }

// --- iTunes items (MP4, M4A, M4V) ---------------------------------------------

const QList<QPair<QString, QByteArray>>& itemNames()
{
    static const QList<QPair<QString, QByteArray>> n{
        {"title", cc("nam")},  {"artist", cc("ART")},  {"album_artist", "aART"}, {"album", cc("alb")},
        {"composer", cc("wrt")}, {"genre", cc("gen")}, {"date", cc("day")},      {"track", "trkn"},
        {"disc", "disk"},      {"grouping", cc("grp")}, {"comment", cc("cmt")},  {"description", "desc"},
        {"lyrics", cc("lyr")}, {"copyright", "cprt"},  {"encoder", cc("too")},   {"bpm", "tmpo"},
    };
    return n;
}

// Fields kept as iTunes "freeform" items (----), by name.
const QList<QPair<QString, QString>>& freeformNames()
{
    static const QList<QPair<QString, QString>> n{
        {"subtitle", "SUBTITLE"}, {"lyricist", "LYRICIST"}, {"conductor", "CONDUCTOR"}, {"remixer", "REMIXER"},
        {"isrc", "ISRC"},         {"mood", "MOOD"},         {"publisher", "LABEL"},     {"language", "LANGUAGE"},
        {"key", "initialkey"},
    };
    return n;
}

QByteArray itemNameOf(const QString& field)
{
    for (const auto& [f, n] : itemNames())
        if (f == field) return n;
    return {};
}

QString freeformNameOf(const QString& field)
{
    for (const auto& [f, n] : freeformNames())
        if (f == field) return n;
    return field;   // a custom field: its own name
}

QString fieldOfFreeform(const QString& name)
{
    for (const auto& [f, n] : freeformNames())
        if (n.compare(name, Qt::CaseInsensitive) == 0) return f;
    return name;
}

// The string in a mean/name box (after its version and flags).
QString boxText(const QByteArray& b, const Box& x) { return QString::fromUtf8(b.mid(x.bodyAt() + 4, x.end() - x.bodyAt() - 4)); }

// What an item holds: {field, value}, or nothing for items that aren't fields (cover art...).
std::optional<QPair<QString, QString>> itemValue(const QByteArray& b, const Box& item)
{
    QString field;
    if (item.type == "----") {
        const auto name = child(b, item, "name");
        if (!name) return std::nullopt;
        field = fieldOfFreeform(boxText(b, *name));
    } else if (item.type == "gnre") {
        field = "genre";
    } else {
        for (const auto& [f, n] : itemNames())
            if (n == item.type) field = f;
        if (field.isEmpty()) return std::nullopt;
    }
    const auto data = child(b, item, "data");
    if (!data || data->size < data->head + 8) return std::nullopt;
    const quint32 type = u32(b, data->bodyAt()) & 0xFFFFFF;
    const QByteArray v = b.mid(data->bodyAt() + 8, data->end() - data->bodyAt() - 8);
    QString value;
    if (item.type == "trkn" || item.type == "disk") {
        if (v.size() < 6) return std::nullopt;
        const int n = qFromBigEndian<quint16>(v.constData() + 2), total = qFromBigEndian<quint16>(v.constData() + 4);
        if (!n) return std::nullopt;
        value = total ? QString("%1/%2").arg(n).arg(total) : QString::number(n);
    } else if (item.type == "gnre") {
        static const QStringList g{"Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz",
                                   "Metal", "New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno"};
        const int n = v.size() >= 2 ? qFromBigEndian<quint16>(v.constData()) - 1 : -1;
        if (n < 0 || n >= g.size()) return std::nullopt;
        value = g[n];
    } else if (type == 21 || item.type == "tmpo") {
        qint64 n = 0;
        for (char c : v) n = (n << 8) | quint8(c);
        value = QString::number(n);
    } else if (type == 2) {
        for (qsizetype i = 0; i + 1 < v.size(); i += 2) value.append(QChar(char16_t((quint8(v[i]) << 8) | quint8(v[i + 1]))));
    } else {
        value = QString::fromUtf8(v);
    }
    return QPair<QString, QString>{field, value};
}

QByteArray dataBox(quint32 type, const QByteArray& value) { return box("data", be32(type) + be32(0) + value); }

// A new item for @p field, or nothing when @p value doesn't fit it.
std::optional<QByteArray> newItem(const QString& field, const QString& value)
{
    const QByteArray name = itemNameOf(field);
    if (name == "trkn" || name == "disk") {
        bool ok1 = false, ok2 = true;
        const int n = value.section('/', 0, 0).trimmed().toInt(&ok1);
        const int total = value.contains('/') ? value.section('/', 1, 1).trimmed().toInt(&ok2) : 0;
        if (!ok1 || !ok2 || n <= 0 || n > 65535 || total < 0 || total > 65535) return std::nullopt;
        QByteArray v = be16(0) + be16(quint16(n)) + be16(quint16(total));
        if (name == "trkn") v += be16(0);
        return box(name, dataBox(0, v));
    }
    if (name == "tmpo") {
        bool ok = false;
        const int bpm = value.trimmed().toInt(&ok);
        if (!ok || bpm < 0 || bpm > 65535) return std::nullopt;
        return box(name, dataBox(21, be16(quint16(bpm))));
    }
    if (!name.isEmpty()) return box(name, dataBox(1, value.toUtf8()));
    return box("----", box("mean", be32(0) + "com.apple.iTunes") + box("name", be32(0) + freeformNameOf(field).toUtf8())
                           + dataBox(1, value.toUtf8()));
}

bool itemIsField(const QByteArray& b, const Box& item, const QString& field)
{
    if (field == "genre" && item.type == "gnre") return true;
    if (item.type == "----") {
        const auto name = child(b, item, "name");
        return name && boxText(b, *name).compare(freeformNameOf(field), Qt::CaseInsensitive) == 0;
    }
    return !itemNameOf(field).isEmpty() && item.type == itemNameOf(field);
}

// --- QuickTime text items (MOV) -----------------------------------------------

const QList<QPair<QString, QByteArray>>& qtNames()
{
    static const QList<QPair<QString, QByteArray>> n{
        {"title", cc("nam")},  {"artist", cc("ART")},     {"album", cc("alb")},   {"comment", cc("cmt")},
        {"copyright", cc("cpy")}, {"date", cc("day")},    {"description", cc("des")}, {"genre", cc("gen")},
        {"encoder", cc("swr")},
    };
    return n;
}

QByteArray qtNameOf(const QString& field)
{
    for (const auto& [f, n] : qtNames())
        if (f == field) return n;
    return {};
}

std::optional<QString> qtValue(const QByteArray& b, const Box& item)
{
    if (item.size < item.head + 4) return std::nullopt;
    const quint16 len = qFromBigEndian<quint16>(b.constData() + item.bodyAt());
    const quint16 lang = qFromBigEndian<quint16>(b.constData() + item.bodyAt() + 2);
    const QByteArray text = b.mid(item.bodyAt() + 4, std::min<qint64>(len, item.end() - item.bodyAt() - 4));
    return lang < 0x400 ? QString::fromLatin1(text) : QString::fromUtf8(text);   // old Mac codes: near enough Latin-1
}

QByteArray qtItem(const QByteArray& name, const QString& value)
{
    const QByteArray text = value.toUtf8();
    return box(name, be16(quint16(text.size())) + be16(0x55C4) + text);   // 0x55C4: "und", UTF-8 text
}

// --- Windows' rating (udta/Xtra) ---------------------------------------------
//
// Xtra holds entries: size, name length, name, value count, then each value
// as size, type and data. The rating is one value of type 19 (a 64-bit
// little-endian number): 1, 25, 50, 75 or 99 for one to five stars.

const QByteArray kRatingName = "WM/SharedUserRating";

std::optional<QList<QPair<QByteArray, QByteArray>>> xtraEntries(const QByteArray& b, const Box& xtra)
{
    QList<QPair<QByteArray, QByteArray>> out;
    for (qint64 pos = xtra.bodyAt(); pos + 8 <= xtra.end();) {
        const quint32 size = u32(b, pos), nameLen = u32(b, pos + 4);
        if (size < 8 + nameLen || pos + size > xtra.end()) return std::nullopt;
        out.append({b.mid(pos + 8, nameLen), b.mid(pos, size)});
        pos += size;
    }
    return out;
}

int starsOfXtra(const QByteArray& entry)
{
    const quint32 nameLen = u32(entry, 4);
    const qint64 value = 8 + nameLen + 4;   // after the value count
    if (value + 6 + 8 > entry.size() || qFromBigEndian<quint16>(entry.constData() + value + 4) != 19) return 0;
    const qint64 pct = qint64(qFromLittleEndian<quint64>(entry.constData() + value + 6));
    if (pct <= 0) return 0;
    if (pct < 13) return 1;
    if (pct < 38) return 2;
    if (pct < 63) return 3;
    if (pct < 88) return 4;
    return 5;
}

QByteArray ratingEntry(int stars)
{
    static const quint64 pct[6]{0, 1, 25, 50, 75, 99};
    QByteArray data(8, '\0');
    qToLittleEndian<quint64>(pct[std::clamp(stars, 0, 5)], data.data());
    const QByteArray value = be32(14) + be16(19) + data;
    return be32(quint32(12 + kRatingName.size() + value.size())) + be32(quint32(kRatingName.size())) + kRatingName + be32(1) + value;
}

// --- Reading -------------------------------------------------------------------

Pairs fieldsOf(const QByteArray& moov)
{
    Pairs out;
    auto add = [&](const QString& k, const QString& v) {
        if (v.isEmpty()) return;
        for (const auto& [key, old] : out)
            if (key == k) return;
        out.append({k, v});
    };
    const Box m{"moov", 0, moov.size(), 8};
    const auto udta = child(moov, m, "udta");
    if (!udta) return out;
    if (const auto meta = child(moov, *udta, "meta"))
        if (const auto ilst = child(moov, *meta, "ilst", metaSkip(moov, *meta)))
            if (const auto items = boxesIn(moov, ilst->bodyAt(), ilst->end()))
                for (const Box& item : *items)
                    if (const auto v = itemValue(moov, item)) add(v->first, v->second);
    if (const auto kids = boxesIn(moov, udta->bodyAt(), udta->end()))
        for (const Box& k : *kids) {
            for (const auto& [f, n] : qtNames())
                if (k.type == n)
                    if (const auto v = qtValue(moov, k)) add(f, *v);
            if (k.type == "Xtra")
                if (const auto entries = xtraEntries(moov, k))
                    for (const auto& [name, bytes] : *entries)
                        if (name == kRatingName && starsOfXtra(bytes)) add("rating", QString::number(starsOfXtra(bytes)));
        }
    return out;
}

// --- Writing -------------------------------------------------------------------

QByteArray bytesOf(const QByteArray& b, const Box& x) { return b.mid(x.at, x.size); }

// The children of @p parent as raw bytes.
std::optional<QList<QPair<Box, QByteArray>>> rawChildren(const QByteArray& b, const Box& parent, qint64 skip = 0)
{
    const auto kids = boxesIn(b, parent.bodyAt() + skip, parent.end());
    if (!kids) return std::nullopt;
    QList<QPair<Box, QByteArray>> out;
    for (const Box& k : *kids) out.append({k, bytesOf(b, k)});
    return out;
}

// Replaces the children @p matches picks with @p fresh (if any), at the first one's place.
void replaceIn(QList<QPair<Box, QByteArray>>& kids, const std::function<bool(const Box&)>& matches, const std::optional<QByteArray>& fresh)
{
    qsizetype place = -1;
    for (qsizetype i = kids.size() - 1; i >= 0; --i)
        if (matches(kids[i].first)) {
            place = i;
            kids.removeAt(i);
        }
    if (!fresh) return;
    const QPair<Box, QByteArray> item{Box{}, *fresh};
    if (place >= 0) kids.insert(place, item);
    else kids.append(item);
}

QByteArray joined(const QList<QPair<Box, QByteArray>>& kids)
{
    QByteArray b;
    for (const auto& [x, bytes] : kids) b += bytes;
    return b;
}

std::optional<QByteArray> newMoov(const QByteArray& moov, bool mov, const QHash<QString, QString>& changes, QStringList* notStored)
{
    const Box m{"moov", 0, moov.size(), moov.size() >= 16 && u32(moov, 0) == 1 ? 16 : 8};
    auto moovKids = rawChildren(moov, m);
    if (!moovKids) return std::nullopt;
    std::optional<Box> udta = child(moov, m, "udta");
    QList<QPair<Box, QByteArray>> udtaKids;
    if (udta) {
        auto k = rawChildren(moov, *udta);
        if (!k) return std::nullopt;
        udtaKids = *k;
    }

    // The rating, in the Xtra box (other entries kept).
    if (changes.contains("rating")) {
        const QString value = changes.value("rating").trimmed();
        bool ok = value.isEmpty();
        const int stars = value.toInt(&ok);
        if (!value.isEmpty() && (!ok || stars < 0 || stars > 5)) {
            notStored->append("rating");
        } else {
            QList<QPair<QByteArray, QByteArray>> entries;
            for (const auto& [x, bytes] : udtaKids)
                if (x.type == "Xtra") {
                    const auto e = xtraEntries(moov, x);
                    if (!e) return std::nullopt;
                    entries = *e;
                }
            entries.erase(std::remove_if(entries.begin(), entries.end(), [](const auto& e) { return e.first == kRatingName; }), entries.end());
            if (!value.isEmpty() && stars > 0) entries.append({kRatingName, ratingEntry(stars)});
            QByteArray body;
            for (const auto& [name, bytes] : entries) body += bytes;
            replaceIn(udtaKids, [](const Box& x) { return x.type == "Xtra"; },
                      entries.isEmpty() ? std::nullopt : std::optional(box("Xtra", body)));
        }
    }
    {
        // The meta box and its item list, made if missing.
        QByteArray metaPrefix = be32(0);   // version and flags
        QList<QPair<Box, QByteArray>> metaKids, items;
        std::optional<Box> meta;
        for (const auto& [x, bytes] : udtaKids)
            if (x.type == "meta") meta = x;
        if (meta) {
            const qint64 skip = metaSkip(moov, *meta);
            metaPrefix = moov.mid(meta->bodyAt(), skip);
            auto k = rawChildren(moov, *meta, skip);
            if (!k) return std::nullopt;
            metaKids = *k;
            if (const auto ilst = child(moov, *meta, "ilst", skip)) {
                auto i = rawChildren(moov, *ilst);
                if (!i) return std::nullopt;
                items = *i;
            }
        }
        if (std::none_of(metaKids.begin(), metaKids.end(), [](const auto& k) { return k.first.type == "hdlr"; }))
            metaKids.insert(0, {Box{"hdlr"}, box("hdlr", be32(0) + be32(0) + "mdir" + "appl" + QByteArray(8, '\0') + QByteArray(1, '\0'))});

        for (auto it = changes.begin(); it != changes.end(); ++it) {
            if (it.key() == "rating") continue;   // done above
            std::optional<QByteArray> fresh;
            if (!it.value().isEmpty()) {
                fresh = newItem(it.key(), it.value());
                if (!fresh) {
                    notStored->append(it.key());
                    continue;
                }
            }
            replaceIn(items, [&](const Box& x) { return x.type.isEmpty() ? false : itemIsField(moov, x, it.key()); }, fresh);
            // QuickTime's own copy of the field: kept in step in a MOV, dropped
            // elsewhere (it would linger with the old value).
            if (const QByteArray qt = qtNameOf(it.key()); !qt.isEmpty())
                replaceIn(udtaKids, [&](const Box& x) { return x.type == qt; },
                          mov && !it.value().isEmpty() ? std::optional(qtItem(qt, it.value())) : std::nullopt);
        }
        replaceIn(metaKids, [](const Box& x) { return x.type == "ilst"; }, box("ilst", joined(items)));
        replaceIn(udtaKids, [](const Box& x) { return x.type == "meta"; }, box("meta", metaPrefix + joined(metaKids)));
    }
    replaceIn(*moovKids, [](const Box& x) { return x.type == "udta"; }, box("udta", joined(udtaKids)));
    return box("moov", joined(*moovKids));
}

// Moves every chunk offset at or past @p from by @p delta.
bool shiftChunkOffsets(QByteArray& b, const Box& parent, qint64 from, qint64 delta)
{
    const auto kids = boxesIn(b, parent.bodyAt(), parent.end());
    if (!kids) return false;
    for (const Box& k : *kids) {
        if (k.type == "trak" || k.type == "mdia" || k.type == "minf" || k.type == "stbl") {
            if (!shiftChunkOffsets(b, k, from, delta)) return false;
        } else if (k.type == "stco" || k.type == "co64") {
            const bool wide = k.type == "co64";
            const quint32 count = u32(b, k.bodyAt() + 4);
            const qint64 first = k.bodyAt() + 8;
            if (first + qint64(count) * (wide ? 8 : 4) > k.end()) return false;
            for (quint32 i = 0; i < count; ++i) {
                const qint64 at = first + qint64(i) * (wide ? 8 : 4);
                const qint64 v = wide ? qint64(u64(b, at)) : qint64(u32(b, at));
                if (v < from) continue;
                const qint64 moved = v + delta;
                if (wide) {
                    qToBigEndian<quint64>(quint64(moved), b.data() + at);
                } else {
                    if (moved > 0xFFFFFFFFLL) return false;   // would need co64
                    qToBigEndian<quint32>(quint32(moved), b.data() + at);
                }
            }
        }
    }
    return true;
}

// Fragments after the moov: absolute base offsets (tfhd) move too.
bool shiftFragment(QByteArray& b, qint64 from, qint64 delta)
{
    const Box moof{"moof", 0, b.size(), 8};
    const auto kids = boxesIn(b, 8, b.size());
    if (!kids) return false;
    for (const Box& traf : *kids) {
        if (traf.type != "traf") continue;
        const auto tfhd = child(b, traf, "tfhd");
        if (!tfhd) continue;
        const quint32 flags = u32(b, tfhd->bodyAt()) & 0xFFFFFF;
        if (!(flags & 1)) continue;   // relative to the moof: nothing to do
        const qint64 at = tfhd->bodyAt() + 8;
        const qint64 v = qint64(u64(b, at));
        if (v >= from) qToBigEndian<quint64>(quint64(v + delta), b.data() + at);
    }
    Q_UNUSED(moof);
    return true;
}

// The fragment index at the end (mfra/tfra) holds absolute moof offsets.
bool shiftIndex(QByteArray& b, qint64 from, qint64 delta)
{
    const auto kids = boxesIn(b, 8, b.size());
    if (!kids) return false;
    for (const Box& tfra : *kids) {
        if (tfra.type != "tfra") continue;
        const qint64 body = tfra.bodyAt();
        const bool v1 = quint8(b[body]) == 1;
        const quint32 sizes = u32(b, body + 8);
        const int extra = int(((sizes >> 4) & 3) + 1 + ((sizes >> 2) & 3) + 1 + (sizes & 3) + 1);
        const quint32 count = u32(b, body + 12);
        qint64 at = body + 16;
        for (quint32 i = 0; i < count; ++i) {
            const qint64 off = at + (v1 ? 8 : 4);
            if (off + (v1 ? 8 : 4) > tfra.end()) return false;
            const qint64 v = v1 ? qint64(u64(b, off)) : qint64(u32(b, off));
            if (v >= from) {
                if (v1) qToBigEndian<quint64>(quint64(v + delta), b.data() + off);
                else if (v + delta > 0xFFFFFFFFLL) return false;
                else qToBigEndian<quint32>(quint32(v + delta), b.data() + off);
            }
            at += (v1 ? 16 : 8) + extra;
        }
    }
    return true;
}

bool copyRange(QFile& from, QFile& to, qint64 at, qint64 length)
{
    if (!from.seek(at)) return false;
    while (length > 0) {
        const QByteArray buf = from.read(std::min<qint64>(length, 4 << 20));
        if (buf.isEmpty() || to.write(buf) != buf.size()) return false;
        length -= buf.size();
    }
    return true;
}

std::optional<QByteArray> readMoov(QFile& f, Box* where, QList<Box>* top)
{
    const auto boxes = topLevel(f);
    if (!boxes) return std::nullopt;
    *top = *boxes;
    std::optional<Box> moov;
    for (const Box& b : *boxes)
        if (b.type == "moov") {
            if (moov) return std::nullopt;   // two of them: not touching that
            moov = b;
        }
    if (!moov || moov->size > (256 << 20)) return std::nullopt;
    *where = *moov;
    f.seek(moov->at);
    QByteArray bytes = f.read(moov->size);
    if (bytes.size() < moov->size) return std::nullopt;
    return bytes;
}

bool isMov(const QString& path) { return path.endsWith(".mov", Qt::CaseInsensitive); }

} // namespace

std::optional<Pairs> readMp4Tags(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    Box where;
    QList<Box> top;
    const auto moov = readMoov(f, &where, &top);
    if (!moov) return std::nullopt;
    QByteArray m = *moov;
    if (where.head == 16) m = box("moov", m.mid(16));   // children read the same way either way
    return fieldsOf(m);
}

std::optional<QString> writeMp4(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return QString("Can't open the file: " + in.errorString());
    Box where;
    QList<Box> top;
    const auto moov = readMoov(in, &where, &top);
    if (!moov) return std::nullopt;
    auto fresh = newMoov(*moov, isMov(path), changes, notStored);
    if (!fresh) return std::nullopt;

    const qint64 delta = qint64(fresh->size()) - where.size;
    const qint64 from = where.end();   // offsets past the old moov move
    if (delta != 0 && !shiftChunkOffsets(*fresh, Box{"moov", 0, fresh->size(), 8}, from, delta)) return std::nullopt;

    const QString temp = tempPathFor(path);
    QFile out(temp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString("Can't write in this folder: " + out.errorString());
    auto abandon = [&](const QString& why) {
        out.close();
        QFile::remove(temp);
        return std::optional<QString>(why);
    };
    for (const Box& b : top) {
        if (b.at == where.at) {
            out.write(*fresh);
            continue;
        }
        if (delta != 0 && b.at > where.at && (b.type == "moof" || b.type == "mfra")) {
            in.seek(b.at);
            QByteArray bytes = in.read(b.size);
            if (b.head == 16) {
                out.close();
                QFile::remove(temp);
                return std::nullopt;
            }
            if (!(b.type == "moof" ? shiftFragment(bytes, from, delta) : shiftIndex(bytes, from, delta))) {
                out.close();
                QFile::remove(temp);
                return std::nullopt;
            }
            out.write(bytes);
            continue;
        }
        if (!copyRange(in, out, b.at, b.size)) return abandon("Copying the file failed (is the disk full?).");
    }
    if (out.error() != QFile::NoError) return abandon("Writing failed: " + out.errorString());
    out.close();
    in.close();
    QString error;
    if (!replaceFile(path, temp, &error)) return error;
    return QString();
}

std::optional<QString> removeMp4Boxes(const QString& path, const QList<qint64>& starts)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return QString("Can't open the file: " + in.errorString());
    const auto top = topLevel(in);
    if (!top) return std::nullopt;
    QList<Box> gone;   // highest first, so each shift is in the old file's terms
    for (const Box& b : *top)
        if (starts.contains(b.at)) gone.prepend(b);
    if (gone.isEmpty()) return QString();

    // Every absolute offset past a removed box moves back by its size: the
    // chunk offsets in the moov, fragments' base offsets, the fragment index.
    auto shifted = [&](const Box& b, QByteArray bytes) -> std::optional<QByteArray> {
        if (b.type != "moov" && b.type != "moof" && b.type != "mfra") return bytes;
        if (b.type != "moov" && b.head == 16) return std::nullopt;
        for (const Box& g : gone) {
            const bool ok = b.type == "moov" ? shiftChunkOffsets(bytes, Box{"moov", 0, bytes.size(), b.head}, g.end(), -g.size)
                          : b.type == "moof" ? shiftFragment(bytes, g.end(), -g.size)
                                             : shiftIndex(bytes, g.end(), -g.size);
            if (!ok) return std::nullopt;
        }
        return bytes;
    };

    const QString temp = tempPathFor(path);
    QFile out(temp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString("Can't write in this folder: " + out.errorString());
    auto abandon = [&](const std::optional<QString>& why) {
        out.close();
        QFile::remove(temp);
        return why;
    };
    for (const Box& b : *top) {
        if (starts.contains(b.at)) continue;
        if (b.type == "moov" || b.type == "moof" || b.type == "mfra") {
            if (b.size > (256 << 20)) return abandon(std::nullopt);
            in.seek(b.at);
            const auto bytes = shifted(b, in.read(b.size));
            if (!bytes) return abandon(QString("This file's layout can't be rewritten safely, so nothing was removed."));
            out.write(*bytes);
            continue;
        }
        if (!copyRange(in, out, b.at, b.size)) return abandon(QString("Copying the file failed (is the disk full?)."));
    }
    if (out.error() != QFile::NoError) return abandon("Writing failed: " + out.errorString());
    out.close();
    in.close();
    QString error;
    if (!replaceFile(path, temp, &error)) return error;
    return QString();
}

} // namespace meta
