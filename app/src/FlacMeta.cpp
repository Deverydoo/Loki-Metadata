// FLAC tags: the VORBIS_COMMENT metadata block is rebuilt; every other block
// (stream info, seek table, cue sheet, pictures, application data) and the
// audio are copied as they are. When the new tags fit in the old block plus
// its padding, the audio stays at the same offset.

#include "MetaIO.h"
#include "Vorbis.h"

#include <QFile>

namespace meta {

namespace {

struct Block {
    int type = 0;
    QByteArray data;
};

struct Flac {
    qint64 prefix = 0;      // an ID3v2 tag some taggers put first (kept)
    QList<Block> blocks;
    qint64 audioAt = 0;
};

enum : int { kStreamInfo = 0, kPadding = 1, kVorbisComment = 4 };

std::optional<Flac> parseFlac(QFile& f)
{
    Flac x;
    f.seek(0);
    QByteArray head = f.read(10);
    if (head.startsWith("ID3") && head.size() == 10) {   // skip a leading ID3v2 tag
        const qint64 size = 10 + ((qint64(head[6] & 0x7f) << 21) | (qint64(head[7] & 0x7f) << 14) | (qint64(head[8] & 0x7f) << 7)
                                  | qint64(head[9] & 0x7f));
        x.prefix = size;
    }
    f.seek(x.prefix);
    if (f.read(4) != "fLaC") return std::nullopt;
    for (bool last = false; !last;) {
        const QByteArray h = f.read(4);
        if (h.size() < 4) return std::nullopt;
        last = quint8(h[0]) & 0x80;
        const int type = quint8(h[0]) & 0x7f;
        const qint64 n = (qint64(quint8(h[1])) << 16) | (qint64(quint8(h[2])) << 8) | quint8(h[3]);
        const QByteArray data = f.read(n);
        if (data.size() < n) return std::nullopt;
        x.blocks.append({type, data});
    }
    x.audioAt = f.pos();
    if (x.blocks.isEmpty() || x.blocks.first().type != kStreamInfo) return std::nullopt;
    return x;
}

std::optional<vorbis::Comments> commentsOf(const Flac& x)
{
    for (const Block& b : x.blocks)
        if (b.type == kVorbisComment) return vorbis::parse(b.data);
    return vorbis::Comments{"Loki Metadata", {}};
}

QByteArray header(int type, bool last, qint64 size)
{
    QByteArray h(4, '\0');
    h[0] = char((last ? 0x80 : 0) | type);
    h[1] = char((size >> 16) & 0xff);
    h[2] = char((size >> 8) & 0xff);
    h[3] = char(size & 0xff);
    return h;
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

std::optional<Pairs> readFlacTags(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const auto x = parseFlac(f);
    if (!x) return std::nullopt;
    const auto c = commentsOf(*x);
    if (!c) return std::nullopt;
    return vorbis::fields(*c);
}

std::optional<QString> writeFlac(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    Q_UNUSED(notStored);   // names are free-form: everything fits
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return QString("Can't open the file: " + in.errorString());
    const auto x = parseFlac(in);
    if (!x) return std::nullopt;
    auto c = commentsOf(*x);
    if (!c) return std::nullopt;
    // Windows reads a FLAC's "Comments" from DESCRIPTION, and writes the
    // comment to both; so a comment goes there too, unless the description
    // says something else.
    QHash<QString, QString> all = changes;
    if (all.contains("comment") && !all.contains("description")) {
        QString oldComment, oldDescription;
        for (const auto& [k, v] : vorbis::fields(*c)) {
            if (k == "comment") oldComment = v;
            if (k == "description") oldDescription = v;
        }
        if (oldDescription.isEmpty() || oldDescription == oldComment) all.insert("description", all.value("comment"));
    }
    vorbis::apply(*c, all);
    const QByteArray comment = vorbis::build(*c);

    // The blocks: the comment in its old place (or after the stream info),
    // padding at the end, sized so the audio doesn't move when it can.
    qint64 oldSize = 0;
    QList<Block> blocks;
    bool placed = false;
    for (const Block& b : x->blocks) {
        oldSize += 4 + b.data.size();
        if (b.type == kPadding) continue;
        if (b.type == kVorbisComment) {
            if (!placed) blocks.append({kVorbisComment, comment});
            placed = true;
            continue;
        }
        blocks.append(b);
    }
    if (!placed) blocks.insert(1, {kVorbisComment, comment});   // none before: right after the stream info
    qint64 newSize = 0;
    for (const Block& b : blocks) newSize += 4 + b.data.size();
    const qint64 padding = newSize + 4 <= oldSize ? oldSize - newSize - 4 : 4096;
    blocks.append({kPadding, QByteArray(padding, '\0')});
    if (comment.size() >= (1 << 24)) return QString("The tags are too large for a FLAC block.");

    const QString temp = tempPathFor(path);
    QFile out(temp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString("Can't write in this folder: " + out.errorString());
    auto abandon = [&](const QString& why) {
        out.close();
        QFile::remove(temp);
        return std::optional<QString>(why);
    };
    if (x->prefix && !copyRange(in, out, 0, x->prefix)) return abandon("Copying the file failed.");
    out.write("fLaC");
    for (int i = 0; i < blocks.size(); ++i) {
        out.write(header(blocks[i].type, i == blocks.size() - 1, blocks[i].data.size()));
        out.write(blocks[i].data);
    }
    if (!copyRange(in, out, x->audioAt, in.size() - x->audioAt)) return abandon("Copying the audio failed (is the disk full?).");
    if (out.error() != QFile::NoError) return abandon("Writing failed: " + out.errorString());
    out.close();
    in.close();
    QString error;
    if (!replaceFile(path, temp, &error)) return error;
    return QString();
}

} // namespace meta
