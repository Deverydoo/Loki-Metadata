// AIFF tags: an ID3v2 tag in an "ID3 " chunk (what FFmpeg, iTunes and most
// players read), with AIFF's own NAME, AUTH, "(c) " and ANNO text chunks read
// as a fallback. Writing rebuilds the ID3 chunk and drops the matching old
// text chunks for fields that change; the sound data and every other chunk
// are copied as they are.

#include "Id3.h"
#include "MetaIO.h"

#include <QFile>
#include <QtEndian>

namespace meta {

namespace {

struct Chunk {
    QByteArray id;
    qint64 at = 0;     // header start
    quint32 size = 0;  // body
};

std::optional<QList<Chunk>> chunksOf(QFile& f)
{
    f.seek(0);
    const QByteArray head = f.read(12);
    if (head.size() < 12 || !head.startsWith("FORM") || (head.mid(8, 4) != "AIFF" && head.mid(8, 4) != "AIFC")) return std::nullopt;
    QList<Chunk> out;
    for (qint64 pos = 12; pos + 8 <= f.size();) {
        f.seek(pos);
        const QByteArray h = f.read(8);
        Chunk c{h.left(4), pos, qFromBigEndian<quint32>(h.constData() + 4)};
        if (pos + 8 + qint64(c.size) > f.size()) return std::nullopt;
        out.append(c);
        pos += 8 + c.size + (c.size & 1);
    }
    return out;
}

bool isId3(const QByteArray& id) { return id == "ID3 " || id == "id3 "; }

// AIFF's own text chunks and the fields they hold.
const QList<QPair<QByteArray, QString>>& textChunks()
{
    static const QList<QPair<QByteArray, QString>> t{{"NAME", "title"}, {"AUTH", "artist"}, {"(c) ", "copyright"}, {"ANNO", "comment"}};
    return t;
}

QString textField(const QByteArray& id)
{
    for (const auto& [c, f] : textChunks())
        if (c == id) return f;
    return {};
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

} // namespace

std::optional<Pairs> readAiffTags(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const auto chunks = chunksOf(f);
    if (!chunks) return std::nullopt;
    Pairs out;
    for (const Chunk& c : *chunks)
        if (isId3(c.id)) {
            f.seek(c.at + 8);
            if (const auto tag = id3::parse(f.read(c.size))) out = id3::fields(*tag);
        }
    for (const Chunk& c : *chunks) {
        const QString field = textField(c.id);
        if (field.isEmpty()) continue;
        bool have = false;
        for (const auto& [k, v] : out) have = have || k == field;
        f.seek(c.at + 8);
        QByteArray text = f.read(c.size);
        while (text.endsWith('\0')) text.chop(1);
        if (!have && !text.isEmpty()) out.append({field, QString::fromLatin1(text)});
    }
    return out;
}

std::optional<QString> writeAiff(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return QString("Can't open the file: " + in.errorString());
    const auto chunks = chunksOf(in);
    if (!chunks) return std::nullopt;
    id3::Tag tag;
    for (const Chunk& c : *chunks)
        if (isId3(c.id)) {
            in.seek(c.at + 8);
            const auto parsed = id3::parse(in.read(c.size));
            if (!parsed) return std::nullopt;
            tag = *parsed;
        }
    id3::apply(tag, changes, notStored);
    const QByteArray id3 = id3::build(tag, 0);
    QByteArray id3Chunk;
    if (!id3.isEmpty()) {
        id3Chunk = "ID3 ";
        QByteArray n(4, '\0');
        qToBigEndian<quint32>(quint32(id3.size()), n.data());
        id3Chunk += n + id3;
        if (id3.size() & 1) id3Chunk += '\0';
    }

    const QString temp = tempPathFor(path);
    QFile out(temp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString("Can't write in this folder: " + out.errorString());
    auto abandon = [&](const QString& why) {
        out.close();
        QFile::remove(temp);
        return std::optional<QString>(why);
    };
    in.seek(0);
    out.write(in.read(12));
    bool placed = false;
    for (const Chunk& c : *chunks) {
        if (isId3(c.id)) {
            if (!placed) out.write(id3Chunk);
            placed = true;
            continue;
        }
        const QString field = textField(c.id);
        if (!field.isEmpty() && changes.contains(field)) continue;   // the ID3 tag holds it now
        if (!copyRange(in, out, c.at, 8 + qint64(c.size))) return abandon("Copying the file failed (is the disk full?).");
        if (c.size & 1) out.write(QByteArray(1, '\0'));
    }
    if (!placed) out.write(id3Chunk);
    const qint64 total = out.size();
    if (total - 8 > 0xFFFFFFFFLL) return abandon("The file would be too big for an AIFF header.");
    out.seek(4);
    QByteArray n(4, '\0');
    qToBigEndian<quint32>(quint32(total - 8), n.data());
    out.write(n);
    if (out.error() != QFile::NoError) return abandon("Writing failed: " + out.errorString());
    out.close();
    in.close();
    QString error;
    if (!replaceFile(path, temp, &error)) return error;
    return QString();
}

} // namespace meta
