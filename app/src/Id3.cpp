#include "Id3.h"

#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>
#include <functional>

namespace meta::id3 {

namespace {

// Text frames that hold a named field.
const QList<QPair<QString, QByteArray>>& textFrames()
{
    static const QList<QPair<QString, QByteArray>> frames{
        {"title", "TIT2"},       {"subtitle", "TIT3"},     {"artist", "TPE1"},      {"album_artist", "TPE2"},
        {"album", "TALB"},       {"composer", "TCOM"},     {"lyricist", "TEXT"},    {"conductor", "TPE3"},
        {"remixer", "TPE4"},     {"original_artist", "TOPE"}, {"genre", "TCON"},    {"track", "TRCK"},
        {"disc", "TPOS"},        {"publisher", "TPUB"},    {"copyright", "TCOP"},   {"encoded_by", "TENC"},
        {"encoder", "TSSE"},     {"language", "TLAN"},     {"grouping", "TIT1"},    {"bpm", "TBPM"},
        {"key", "TKEY"},         {"isrc", "TSRC"},
    };
    return frames;
}

QString fieldOfFrame(const QByteArray& id)
{
    for (const auto& [field, f] : textFrames())
        if (f == id) return field;
    return {};
}

QByteArray frameOfField(const QString& field)
{
    for (const auto& [f, id] : textFrames())
        if (f == field) return id;
    return {};
}

// "(17)", "17" and "(17)Rock" all mean Rock.
QString genreName(const QString& raw)
{
    static const QRegularExpression numbered(R"(^\((\d+)\)(.*)$|^(\d+)$)");
    const QRegularExpressionMatch m = numbered.match(raw.trimmed());
    if (!m.hasMatch()) return raw;
    if (!m.captured(2).isEmpty()) return m.captured(2);
    const int n = (m.captured(1).isEmpty() ? m.captured(3) : m.captured(1)).toInt();
    return n < genres().size() ? genres()[n] : raw;
}

quint32 synchsafe(const char* p)
{
    return (quint32(quint8(p[0]) & 0x7f) << 21) | (quint32(quint8(p[1]) & 0x7f) << 14) | (quint32(quint8(p[2]) & 0x7f) << 7)
         | quint32(quint8(p[3]) & 0x7f);
}

QByteArray toSynchsafe(quint32 v)
{
    QByteArray b(4, '\0');
    b[0] = char((v >> 21) & 0x7f);
    b[1] = char((v >> 14) & 0x7f);
    b[2] = char((v >> 7) & 0x7f);
    b[3] = char(v & 0x7f);
    return b;
}

QByteArray be32(quint32 v)
{
    QByteArray b(4, '\0');
    qToBigEndian(v, b.data());
    return b;
}

// Undoes ID3 "unsynchronisation": every FF 00 was an FF.
QByteArray resync(const QByteArray& b)
{
    QByteArray out;
    out.reserve(b.size());
    for (qsizetype i = 0; i < b.size(); ++i) {
        out.append(b[i]);
        if (quint8(b[i]) == 0xFF && i + 1 < b.size() && b[i + 1] == 0) ++i;
    }
    return out;
}

// A frame's content, with its storage tricks undone. Nothing for compressed
// or encrypted frames (they are kept, just not shown).
std::optional<QByteArray> content(const Tag& t, const Frame& f)
{
    QByteArray d = f.data;
    if (t.version == 3) {
        if (f.flags & 0x00C0) return std::nullopt;
        if (f.flags & 0x0020) d = d.mid(1);   // group id
        return d;
    }
    if (f.flags & 0x000C) return std::nullopt;
    if (f.flags & 0x0040) d = d.mid(1);       // group id
    if (f.flags & 0x0001) d = d.mid(4);       // data length
    if (f.flags & 0x0002) d = resync(d);
    return d;
}

QString decodeOne(quint8 enc, const QByteArray& b)
{
    switch (enc) {
    case 0: return QString::fromLatin1(b);
    case 3: return QString::fromUtf8(b);
    default: {
        bool little = enc == 1;   // UTF-16 with a byte-order mark; without one, assume little-endian
        qsizetype i = 0;
        if (b.size() >= 2 && quint8(b[0]) == 0xFF && quint8(b[1]) == 0xFE) { little = true; i = 2; }
        else if (b.size() >= 2 && quint8(b[0]) == 0xFE && quint8(b[1]) == 0xFF) { little = false; i = 2; }
        QString s;
        for (; i + 1 < b.size(); i += 2)
            s.append(QChar(char16_t(little ? quint8(b[i]) | (quint8(b[i + 1]) << 8) : (quint8(b[i]) << 8) | quint8(b[i + 1]))));
        return s;
    }
    }
}

// The strings in @p b, split at the terminators of encoding @p enc.
QStringList strings(quint8 enc, const QByteArray& b)
{
    QStringList out;
    const bool wide = enc == 1 || enc == 2;
    qsizetype start = 0;
    for (qsizetype i = 0; i < b.size(); i += wide ? 2 : 1) {
        const bool end = wide ? (i + 1 < b.size() && b[i] == 0 && b[i + 1] == 0) : b[i] == 0;
        if (!end) continue;
        out.append(decodeOne(enc, b.mid(start, i - start)));
        start = i + (wide ? 2 : 1);
    }
    if (start < b.size()) out.append(decodeOne(enc, b.mid(start)));
    return out;
}

bool isLatin1(const QString& s)
{
    for (QChar c : s)
        if (c.unicode() > 255) return false;
    return true;
}

// Text in the plainest encoding that holds it.
quint8 encodingFor(int version, const QString& s) { return isLatin1(s) ? 0 : version == 4 ? 3 : 1; }

QByteArray encode(quint8 enc, const QString& s)
{
    if (enc == 0) return s.toLatin1();
    if (enc == 3) return s.toUtf8();
    QByteArray b = QByteArray::fromHex("FFFE");
    for (QChar c : s) {
        b.append(char(c.unicode() & 0xFF));
        b.append(char(c.unicode() >> 8));
    }
    return b;
}

QByteArray terminator(quint8 enc) { return enc == 1 || enc == 2 ? QByteArray(2, '\0') : QByteArray(1, '\0'); }

const QByteArray kWindowsRater = "Windows Media Player 9 Series";

int starsOf(quint8 popm)
{
    if (popm == 0) return 0;
    if (popm < 32) return 1;
    if (popm < 96) return 2;
    if (popm < 160) return 3;
    if (popm < 224) return 4;
    return 5;
}

quint8 popmOf(int stars)
{
    static const quint8 v[6]{0, 1, 64, 128, 196, 255};
    return v[std::clamp(stars, 0, 5)];
}

// What a frame says, if it holds a field.
struct Said {
    QString field, value;
};

std::optional<Said> said(const Tag& t, const Frame& f)
{
    const auto c = content(t, f);
    if (!c || c->isEmpty()) return std::nullopt;
    const quint8 enc = quint8(c->at(0));
    if (f.id == "TXXX") {
        const QStringList s = strings(enc, c->mid(1));
        if (s.size() < 2) return std::nullopt;
        const QString desc = s[0];
        if (desc.compare("MOOD", Qt::CaseInsensitive) == 0) return Said{"mood", s[1]};
        // A description that is also a field's name is told apart.
        return Said{fieldOrder(desc.toLower()) < knownFields().size() ? "TXXX:" + desc : desc, s.mid(1).join("; ")};
    }
    if (f.id.startsWith('T')) {
        const QString value = strings(enc, c->mid(1)).join("; ").trimmed();
        if (f.id == "TMOO") return Said{"mood", value};
        if (f.id == "TCON") return Said{"genre", genreName(value)};
        const QString field = fieldOfFrame(f.id);
        return Said{field.isEmpty() ? QString::fromLatin1(f.id) : field, value};
    }
    if (f.id == "COMM" || f.id == "USLT") {
        if (c->size() < 4) return std::nullopt;
        const QStringList s = strings(enc, c->mid(4));
        if (s.isEmpty() || !s[0].isEmpty()) return std::nullopt;   // ones with a description (iTunNORM...) aren't shown
        return Said{f.id == "COMM" ? "comment" : "lyrics", s.mid(1).join("\n")};
    }
    if (f.id == "POPM") {
        const qsizetype nul = c->indexOf('\0');
        if (nul < 0 || nul + 1 >= c->size()) return std::nullopt;
        const int stars = starsOf(quint8(c->at(nul + 1)));
        return Said{"rating", stars ? QString::number(stars) : QString()};
    }
    return std::nullopt;
}

bool isDateFrame(const QByteArray& id) { return id == "TYER" || id == "TDAT" || id == "TIME" || id == "TDRC" || id == "TRDA"; }

QString textOf(const Tag& t, const QByteArray& id)
{
    for (const Frame& f : t.frames)
        if (f.id == id)
            if (const auto c = content(t, f); c && !c->isEmpty()) return strings(quint8(c->at(0)), c->mid(1)).join(" ").trimmed();
    return {};
}

} // namespace

const QStringList& genres()
{
    static const QStringList g{
        "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal",
        "New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno", "Industrial",
        "Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal",
        "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental", "Acid", "House", "Game", "Sound Clip",
        "Gospel", "Noise", "AlternRock", "Bass", "Soul", "Punk", "Space", "Meditative", "Instrumental Pop",
        "Instrumental Rock", "Ethnic", "Gothic", "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk",
        "Eurodance", "Dream", "Southern Rock", "Comedy", "Cult", "Gangsta", "Top 40", "Christian Rap", "Pop/Funk",
        "Jungle", "Native American", "Cabaret", "New Wave", "Psychadelic", "Rave", "Showtunes", "Trailer", "Lo-Fi",
        "Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical", "Rock & Roll", "Hard Rock"};
    return g;
}

qint64 tagSize(const QByteArray& header)
{
    if (header.size() < 10 || !header.startsWith("ID3")) return 0;
    const bool footer = quint8(header[3]) == 4 && (quint8(header[5]) & 0x10);
    return 10 + qint64(synchsafe(header.constData() + 6)) + (footer ? 10 : 0);
}

std::optional<Tag> parse(const QByteArray& bytes)
{
    Tag t;
    if (bytes.isEmpty()) return t;   // no tag yet
    if (bytes.size() < 10 || !bytes.startsWith("ID3")) return std::nullopt;
    t.present = true;
    t.version = quint8(bytes[3]);
    if (t.version < 3 || t.version > 4) return std::nullopt;
    const quint8 flags = quint8(bytes[5]);
    const quint32 size = synchsafe(bytes.constData() + 6);
    QByteArray body = bytes.mid(10, size);
    if (body.size() < qsizetype(size)) return std::nullopt;
    if (t.version == 3 && (flags & 0x80)) body = resync(body);

    qsizetype pos = 0;
    if (flags & 0x40 && body.size() >= 4)   // extended header: skipped (and not written back)
        pos = t.version == 3 ? qsizetype(qFromBigEndian<quint32>(body.constData())) + 4 : qsizetype(synchsafe(body.constData()));
    while (pos + 10 <= body.size()) {
        if (body[pos] == 0) break;   // padding
        Frame fr;
        fr.id = body.mid(pos, 4);
        const quint32 n = t.version == 4 ? synchsafe(body.constData() + pos + 4) : qFromBigEndian<quint32>(body.constData() + pos + 4);
        fr.flags = qFromBigEndian<quint16>(body.constData() + pos + 8);
        if (pos + 10 + qsizetype(n) > body.size()) return std::nullopt;
        fr.data = body.mid(pos + 10, n);
        t.frames.append(fr);
        pos += 10 + n;
    }
    return t;
}

Pairs fields(const Tag& t)
{
    Pairs out;
    auto has = [&](const QString& k) {
        for (const auto& [key, v] : out)
            if (key == k) return true;
        return false;
    };
    // The rating: Windows' own first, then any player's.
    for (int pass = 0; pass < 2 && !has("rating"); ++pass)
        for (const Frame& f : t.frames)
            if (f.id == "POPM" && (pass == 1 || f.data.startsWith(kWindowsRater + '\0')))
                if (const auto s = said(t, f); s && !s->value.isEmpty()) {
                    out.append({s->field, s->value});
                    break;
                }
    for (const Frame& f : t.frames) {
        if (f.id == "POPM" || isDateFrame(f.id)) continue;
        const auto s = said(t, f);
        if (s && !s->value.isEmpty() && !has(s->field)) out.append({s->field, s->value});
    }
    // The date: 2.4's TDRC, or 2.3's year (and day, DDMM).
    QString date = textOf(t, "TDRC");
    if (date.isEmpty()) {
        date = textOf(t, "TYER");
        const QString ddmm = textOf(t, "TDAT");
        if (date.size() == 4 && ddmm.size() == 4) date += "-" + ddmm.mid(2, 2) + "-" + ddmm.left(2);
    }
    if (!date.isEmpty()) out.append({QString("date"), date});
    return out;
}

void apply(Tag& t, const QHash<QString, QString>& changes, QStringList* notStored)
{
    // Replaces the frames @p matches picks with @p frame (none: just removes),
    // keeping the first one's place.
    auto replace = [&](const std::function<bool(const Frame&)>& matches, const std::optional<Frame>& frame) {
        qsizetype place = -1;
        for (qsizetype i = t.frames.size() - 1; i >= 0; --i)
            if (matches(t.frames[i])) {
                place = i;
                t.frames.removeAt(i);
            }
        if (!frame) return;
        if (place >= 0) t.frames.insert(place, *frame);
        else t.frames.append(*frame);
    };
    auto textFrame = [&](const QByteArray& id, const QString& value) {
        const quint8 enc = encodingFor(t.version, value);
        return Frame{id, 0, char(enc) + encode(enc, value)};
    };
    auto withDesc = [&](const QByteArray& id, const QString& desc, const QString& value, bool lang) {
        const quint8 enc = encodingFor(t.version, desc + value);
        QByteArray d(1, char(enc));
        if (lang) d += "eng";
        d += encode(enc, desc) + terminator(enc) + encode(enc, value);
        return Frame{id, 0, d};
    };
    auto descIs = [&](const Frame& f, const QString& desc, bool lang) {
        const auto c = content(t, f);
        if (!c || c->size() < (lang ? 4 : 1)) return false;
        const QStringList s = strings(quint8(c->at(0)), c->mid(lang ? 4 : 1));
        return !s.isEmpty() && s[0].compare(desc, Qt::CaseInsensitive) == 0;
    };

    static const QRegularExpression rawText("^T[A-Z0-9]{3}$");
    static const QRegularExpression ymd(R"(^(\d{4})(?:[-:/.](\d{2})[-:/.](\d{2}))?)");
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        const QString& field = it.key();
        const QString& value = it.value();
        const bool set = !value.isEmpty();
        if (const QByteArray id = frameOfField(field); !id.isEmpty()) {
            replace([&](const Frame& f) { return f.id == id; }, set ? std::optional(textFrame(id, value)) : std::nullopt);
        } else if (field == "date") {
            replace([](const Frame& f) { return isDateFrame(f.id); }, std::nullopt);
            if (!set) continue;
            if (t.version == 4) {
                t.frames.append(textFrame("TDRC", value));
            } else {   // 2.3 keeps the year, and the day and month as DDMM
                const QRegularExpressionMatch m = ymd.match(value.trimmed());
                if (!m.hasMatch()) {
                    notStored->append(field);
                    continue;
                }
                t.frames.append(textFrame("TYER", m.captured(1)));
                if (!m.captured(2).isEmpty()) t.frames.append(textFrame("TDAT", m.captured(3) + m.captured(2)));
            }
        } else if (field == "mood") {
            replace([&](const Frame& f) { return f.id == "TMOO" || (f.id == "TXXX" && descIs(f, "MOOD", false)); },
                    !set ? std::nullopt : std::optional(t.version == 4 ? textFrame("TMOO", value) : withDesc("TXXX", "MOOD", value, false)));
        } else if (field == "comment" || field == "lyrics") {
            const QByteArray id = field == "comment" ? "COMM" : "USLT";
            replace([&](const Frame& f) { return f.id == id && descIs(f, "", true); },
                    set ? std::optional(withDesc(id, "", value, true)) : std::nullopt);
        } else if (field == "rating") {
            bool ok = false;
            const int stars = value.trimmed().toInt(&ok);
            if (set && (!ok || stars < 0 || stars > 5)) {
                notStored->append(field);
                continue;
            }
            replace([&](const Frame& f) { return f.id == "POPM" && f.data.startsWith(kWindowsRater + '\0'); },
                    set && stars > 0 ? std::optional(Frame{"POPM", 0, kWindowsRater + '\0' + char(popmOf(stars))}) : std::nullopt);
            if (!set) replace([](const Frame& f) { return f.id == "POPM"; }, std::nullopt);   // cleared: every player's
        } else if (rawText.match(field).hasMatch() && field != "TXXX") {
            const QByteArray id = field.toLatin1();
            replace([&](const Frame& f) { return f.id == id; }, set ? std::optional(textFrame(id, value)) : std::nullopt);
        } else {   // anything else: a user-defined text frame
            const QString desc = field.startsWith("TXXX:") ? field.mid(5) : field;
            replace([&](const Frame& f) { return f.id == "TXXX" && descIs(f, desc, false); },
                    set ? std::optional(withDesc("TXXX", desc, value, false)) : std::nullopt);
        }
    }
}

QByteArray build(const Tag& t, int padding)
{
    if (t.frames.isEmpty()) return {};
    QByteArray frames;
    for (const Frame& f : t.frames) {
        frames += f.id;
        frames += t.version == 4 ? toSynchsafe(quint32(f.data.size())) : be32(quint32(f.data.size()));
        frames += char(f.flags >> 8);
        frames += char(f.flags & 0xFF);
        frames += f.data;
    }
    frames += QByteArray(padding, '\0');
    QByteArray tag("ID3");
    tag += char(t.version);
    tag += '\0';
    tag += '\0';   // no unsynchronisation, no extended header
    tag += toSynchsafe(quint32(frames.size()));
    return tag + frames;
}

} // namespace meta::id3
