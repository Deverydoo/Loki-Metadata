#include "Vorbis.h"

#include <QtEndian>

namespace meta::vorbis {

namespace {

// The usual names for each field; the first is the one written.
const QList<QPair<QString, QStringList>>& names()
{
    static const QList<QPair<QString, QStringList>> n{
        {"title", {"TITLE"}},
        {"subtitle", {"SUBTITLE"}},
        {"artist", {"ARTIST"}},
        {"album_artist", {"ALBUMARTIST", "ALBUM ARTIST", "ALBUM_ARTIST"}},
        {"album", {"ALBUM"}},
        {"composer", {"COMPOSER"}},
        {"lyricist", {"LYRICIST"}},
        {"conductor", {"CONDUCTOR"}},
        {"remixer", {"REMIXER"}},
        {"genre", {"GENRE"}},
        {"date", {"DATE"}},
        {"track", {"TRACKNUMBER"}},
        {"disc", {"DISCNUMBER"}},
        {"grouping", {"GROUPING"}},
        {"bpm", {"BPM"}},
        {"key", {"INITIALKEY", "KEY"}},
        {"mood", {"MOOD"}},
        {"isrc", {"ISRC"}},
        {"description", {"DESCRIPTION"}},
        {"comment", {"COMMENT"}},
        {"lyrics", {"LYRICS", "UNSYNCEDLYRICS"}},
        {"copyright", {"COPYRIGHT"}},
        {"publisher", {"ORGANIZATION", "LABEL", "PUBLISHER"}},
        {"language", {"LANGUAGE"}},
        {"encoded_by", {"ENCODED-BY", "ENCODEDBY"}},
        {"encoder", {"ENCODER"}},
    };
    return n;
}

QString fieldOf(const QString& name)
{
    const QString upper = name.toUpper();
    for (const auto& [field, list] : names())
        if (list.contains(upper)) return field;
    return name;   // free-form, shown as written
}

QStringList namesOf(const QString& field)
{
    for (const auto& [f, list] : names())
        if (f == field) return list;
    return {field.toUpper()};
}

bool isPicture(const QString& name)
{
    const QString upper = name.toUpper();
    return upper == "METADATA_BLOCK_PICTURE" || upper == "COVERART" || upper == "COVERARTMIME";
}

void le32(QByteArray& b, quint32 v)
{
    char x[4];
    qToLittleEndian(v, x);
    b.append(x, 4);
}

} // namespace

std::optional<Comments> parse(const QByteArray& b, qsizetype* used)
{
    Comments c;
    qsizetype pos = 0;
    auto u32 = [&](quint32* v) {
        if (pos + 4 > b.size()) return false;
        *v = qFromLittleEndian<quint32>(b.constData() + pos);
        pos += 4;
        return true;
    };
    quint32 n = 0;
    if (!u32(&n) || pos + qsizetype(n) > b.size()) return std::nullopt;
    c.vendor = b.mid(pos, n);
    pos += n;
    quint32 count = 0;
    if (!u32(&count)) return std::nullopt;
    for (quint32 i = 0; i < count; ++i) {
        if (!u32(&n) || pos + qsizetype(n) > b.size()) return std::nullopt;
        c.lines.append(b.mid(pos, n));
        pos += n;
    }
    if (used) *used = pos;
    return c;
}

QByteArray build(const Comments& c)
{
    QByteArray b;
    le32(b, quint32(c.vendor.size()));
    b += c.vendor;
    le32(b, quint32(c.lines.size()));
    for (const QByteArray& line : c.lines) {
        le32(b, quint32(line.size()));
        b += line;
    }
    return b;
}

Pairs fields(const Comments& c)
{
    Pairs out;
    for (const QByteArray& line : c.lines) {
        const qsizetype eq = line.indexOf('=');
        if (eq <= 0) continue;
        const QString name = QString::fromUtf8(line.left(eq));
        if (isPicture(name)) continue;
        const QString field = fieldOf(name), value = QString::fromUtf8(line.mid(eq + 1));
        bool merged = false;
        for (auto& [k, v] : out)
            if (k == field) {   // a repeated name: the values together
                v += "; " + value;
                merged = true;
            }
        if (!merged) out.append({field, value});
    }
    return out;
}

void apply(Comments& c, const QHash<QString, QString>& changes)
{
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        const QStringList all = namesOf(it.key());
        qsizetype place = -1;
        for (qsizetype i = c.lines.size() - 1; i >= 0; --i) {
            const qsizetype eq = c.lines[i].indexOf('=');
            if (eq > 0 && all.contains(QString::fromUtf8(c.lines[i].left(eq)).toUpper())) {
                place = i;
                c.lines.removeAt(i);
            }
        }
        if (it.value().isEmpty()) continue;
        const QByteArray line = all.first().toUtf8() + '=' + it.value().toUtf8();
        if (place >= 0) c.lines.insert(place, line);
        else c.lines.append(line);
    }
}

} // namespace meta::vorbis
