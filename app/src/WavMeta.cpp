// WAV tags, written without FFmpeg: the LIST/INFO chunk is rebuilt and every
// other chunk (the audio, broadcast-WAV details, cue points, C2PA content
// credentials, anything else) is copied byte for byte. Rewriting through
// FFmpeg would drop the chunks it doesn't know.
//
// Reading still goes through runa (FFmpeg reads INFO the same way).

#include "MetaIO.h"

#include <QFile>
#include <QRegularExpression>
#include <QtEndian>

namespace meta {

namespace {

// The INFO IDs for the named fields (FFmpeg reads the same ones back).
const QList<QPair<QString, QByteArray>>& infoIds()
{
    static const QList<QPair<QString, QByteArray>> ids{
        {"title", "INAM"},   {"artist", "IART"},    {"album", "IPRD"},      {"genre", "IGNR"},
        {"date", "ICRD"},    {"track", "ITRK"},     {"comment", "ICMT"},    {"copyright", "ICOP"},
        {"language", "ILNG"}, {"encoder", "ISFT"},  {"encoded_by", "ITCH"}, {"keywords", "IKEY"},
        {"subject", "ISBJ"},
    };
    return ids;
}

// The IDs a field lives in: the one it is written to first, then any other
// a reader would also take it from (FFmpeg reads IPRT as the track too).
QList<QByteArray> idsFor(const QString& field)
{
    for (const auto& [f, id] : infoIds())
        if (f == field) return field == "track" ? QList<QByteArray>{id, "IPRT"} : QList<QByteArray>{id};
    static const QRegularExpression raw("^I[A-Z0-9]{3}$");
    if (raw.match(field).hasMatch()) return {field.toLatin1()};
    return {};
}

struct Chunk {
    QByteArray id;
    qint64 at = 0;       // where its header starts
    quint32 size = 0;    // of the body
    bool info = false;   // the LIST/INFO chunk
};

void le32(QByteArray& b, quint32 v)
{
    char x[4];
    qToLittleEndian(v, x);
    b.append(x, 4);
}

bool copyRange(QFile& from, QFile& to, qint64 at, qint64 length)
{
    if (!from.seek(at)) return false;
    QByteArray buf;
    while (length > 0) {
        buf = from.read(std::min<qint64>(length, 4 << 20));
        if (buf.isEmpty()) return false;
        if (to.write(buf) != buf.size()) return false;
        length -= buf.size();
    }
    return true;
}

} // namespace

std::optional<QString> writeWav(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return QString("Can't open the file: " + in.errorString());
    const QByteArray head = in.read(12);
    if (head.size() < 12 || !head.startsWith("RIFF") || head.mid(8, 4) != "WAVE") return std::nullopt;   // RF64 and such

    // The chunks, as they are.
    QList<Chunk> chunks;
    int infoAt = -1;
    const qint64 fileSize = in.size();
    qint64 pos = 12;
    while (pos + 8 <= fileSize) {
        in.seek(pos);
        const QByteArray h = in.read(8);
        Chunk c{h.left(4), pos, qFromLittleEndian<quint32>(h.constData() + 4)};
        if (pos + 8 + qint64(c.size) > fileSize) return std::nullopt;   // sizes don't add up: let FFmpeg cope
        if (c.id == "LIST" && infoAt < 0 && c.size >= 4 && in.read(4) == "INFO") {
            c.info = true;
            infoAt = int(chunks.size());
        }
        chunks.append(c);
        pos += 8 + c.size + (c.size & 1);
    }

    // The INFO items, changed.
    QList<QPair<QByteArray, QByteArray>> items;
    if (infoAt >= 0) {
        in.seek(chunks[infoAt].at + 12);
        const QByteArray body = in.read(chunks[infoAt].size - 4);
        for (qsizetype i = 0; i + 8 <= body.size();) {
            const QByteArray id = body.mid(i, 4);
            const quint32 n = qFromLittleEndian<quint32>(body.constData() + i + 4);
            QByteArray value = body.mid(i + 8, n);
            while (value.endsWith('\0')) value.chop(1);
            items.append({id, value});
            i += 8 + n + (n & 1);
        }
    }
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        const QList<QByteArray> ids = idsFor(it.key());
        if (ids.isEmpty()) {
            notStored->append(it.key());
            continue;
        }
        qsizetype place = -1;   // where the old one was, to keep the order
        for (qsizetype i = items.size() - 1; i >= 0; --i)
            if (ids.contains(items[i].first)) {
                place = i;
                items.removeAt(i);
            }
        if (it.value().isEmpty()) continue;
        const QPair<QByteArray, QByteArray> item{ids.first(), it.value().toUtf8()};
        if (place >= 0) items.insert(place, item);
        else items.append(item);
    }
    QByteArray info;
    if (!items.isEmpty()) {
        QByteArray body("INFO");
        for (const auto& [id, value] : items) {
            body += id;
            le32(body, quint32(value.size() + 1));
            body += value;
            body += '\0';
            if ((value.size() + 1) & 1) body += '\0';
        }
        info = "LIST";
        le32(info, quint32(body.size()));
        info += body;
    }

    // The new file: every chunk copied, the INFO list in its old place (or
    // just before the audio when there wasn't one).
    const QString temp = tempPathFor(path);
    QFile out(temp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString("Can't write in this folder: " + out.errorString());
    auto abandon = [&](const QString& why) {
        out.close();
        QFile::remove(temp);
        return std::optional<QString>(why);
    };
    out.write(head);
    bool placed = false;
    for (int i = 0; i < chunks.size(); ++i) {
        const Chunk& c = chunks[i];
        if (c.info || (infoAt < 0 && !placed && c.id == "data")) {
            out.write(info);
            placed = true;
            if (c.info) continue;
        }
        const qint64 length = 8 + qint64(c.size);
        if (!copyRange(in, out, c.at, length)) return abandon("Copying the file failed (is the disk full?).");
        if (c.size & 1) out.write(QByteArray(1, '\0'));   // the pad byte, even if the old file lacked it
    }
    if (!placed) out.write(info);
    const qint64 total = out.size();
    if (total - 8 > 0xFFFFFFFFLL) return abandon("The file would be too big for a WAV header.");
    out.seek(4);
    QByteArray size;
    le32(size, quint32(total - 8));
    out.write(size);
    if (out.error() != QFile::NoError) return abandon("Writing failed: " + out.errorString());
    out.close();
    in.close();

    QString error;
    if (!replaceFile(path, temp, &error)) return error;
    return QString();
}

} // namespace meta
