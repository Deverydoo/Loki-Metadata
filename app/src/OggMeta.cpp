// Ogg Vorbis and Opus tags. The comment header packet is rebuilt and the
// header pages laid out again; the audio pages are copied as they are, only
// renumbered (with fresh checksums) when the header now takes a different
// number of pages. Files with more than one stream inside, or Ogg FLAC, are
// left to FFmpeg.

#include "MetaIO.h"
#include "Vorbis.h"

#include <QFile>
#include <QtEndian>

namespace meta {

namespace {

struct Page {
    quint8 type = 0;          // 1 continued, 2 first, 4 last
    quint64 granule = 0;
    quint32 serial = 0, seq = 0;
    QByteArray lacing;
    QByteArray body;
    qint64 size = 0;          // in the file, header included
};

quint32 oggCrc(const QByteArray& page)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 r = i << 24;
            for (int k = 0; k < 8; ++k) r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : r << 1;
            table[i] = r;
        }
        ready = true;
    }
    quint32 crc = 0;
    for (char c : page) crc = (crc << 8) ^ table[((crc >> 24) ^ quint8(c)) & 0xff];
    return crc;
}

bool readPage(QFile& f, qint64 at, Page* p)
{
    f.seek(at);
    const QByteArray h = f.read(27);
    if (h.size() < 27 || !h.startsWith("OggS") || h[4] != 0) return false;
    p->type = quint8(h[5]);
    p->granule = qFromLittleEndian<quint64>(h.constData() + 6);
    p->serial = qFromLittleEndian<quint32>(h.constData() + 14);
    p->seq = qFromLittleEndian<quint32>(h.constData() + 18);
    const int segments = quint8(h[26]);
    p->lacing = f.read(segments);
    if (p->lacing.size() < segments) return false;
    qint64 n = 0;
    for (char l : p->lacing) n += quint8(l);
    p->body = f.read(n);
    if (p->body.size() < n) return false;
    p->size = 27 + segments + n;
    return true;
}

QByteArray pageBytes(const Page& p)
{
    QByteArray b("OggS");
    b += '\0';
    b += char(p.type);
    char x[8];
    qToLittleEndian(p.granule, x);
    b.append(x, 8);
    qToLittleEndian(p.serial, x);
    b.append(x, 4);
    qToLittleEndian(p.seq, x);
    b.append(x, 4);
    b.append(4, '\0');   // checksum, filled in below
    b += char(p.lacing.size());
    b += p.lacing;
    b += p.body;
    qToLittleEndian(oggCrc(b), x);
    b.replace(22, 4, QByteArray(x, 4));
    return b;
}

enum class Codec { Vorbis, Opus };

struct Headers {
    Codec codec = Codec::Vorbis;
    quint32 serial = 0;
    QList<QByteArray> packets;   // the header packets (3 for Vorbis, 2 for Opus)
    int pages = 0;               // pages they took
    qint64 end = 0;              // where the audio pages start
};

std::optional<Headers> readHeaders(QFile& f)
{
    Headers h;
    QByteArray packet;
    qint64 at = 0;
    int wanted = 0;
    while (true) {
        Page p;
        if (!readPage(f, at, &p)) return std::nullopt;
        if (h.pages == 0) h.serial = p.serial;
        else if (p.serial != h.serial) return std::nullopt;   // more than one stream
        ++h.pages;
        at += p.size;
        qsizetype pos = 0;
        for (int i = 0; i < p.lacing.size(); ++i) {
            const int n = quint8(p.lacing[i]);
            packet += p.body.mid(pos, n);
            pos += n;
            if (n == 255) continue;
            if (h.packets.isEmpty()) {
                if (packet.startsWith("\x01vorbis")) { h.codec = Codec::Vorbis; wanted = 3; }
                else if (packet.startsWith("OpusHead")) { h.codec = Codec::Opus; wanted = 2; }
                else return std::nullopt;   // Ogg FLAC, Theora...
            }
            h.packets.append(packet);
            packet.clear();
            if (h.packets.size() == wanted) {
                if (i != p.lacing.size() - 1) return std::nullopt;   // audio sharing the header's page
                h.end = at;
                return h;
            }
        }
    }
}

// The comment packet's comments, and what follows them.
std::optional<vorbis::Comments> commentsOf(const Headers& h, QByteArray* tail)
{
    const QByteArray& p = h.packets[1];
    const int skip = h.codec == Codec::Vorbis ? 7 : 8;
    if (h.codec == Codec::Vorbis && !p.startsWith("\x03vorbis")) return std::nullopt;
    if (h.codec == Codec::Opus && !p.startsWith("OpusTags")) return std::nullopt;
    qsizetype used = 0;
    auto c = vorbis::parse(p.mid(skip), &used);
    if (!c) return std::nullopt;
    *tail = p.mid(skip + used);   // Vorbis: the framing bit; Opus: any extra data
    return c;
}

// Header packets laid out as pages: the first alone, the rest together.
QList<Page> paginate(const Headers& h, const QList<QByteArray>& packets)
{
    QList<Page> pages;
    Page first;
    first.type = 2;
    first.serial = h.serial;
    for (qsizetype n = packets[0].size(); ; n -= 255) {
        first.lacing += char(std::min<qsizetype>(n, 255));
        if (n < 255) break;
    }
    first.body = packets[0];
    pages.append(first);

    Page page;
    page.serial = h.serial;
    auto flush = [&](bool packetOpen) {
        page.seq = quint32(pages.size());
        page.granule = packetOpen ? ~quint64(0) : 0;   // no packet ends on it: -1
        pages.append(page);
        page = Page{};
        page.serial = h.serial;
        page.type = packetOpen ? 1 : 0;
    };
    for (int k = 1; k < packets.size(); ++k) {
        const QByteArray& p = packets[k];
        qsizetype pos = 0;
        while (true) {
            const qsizetype n = std::min<qsizetype>(p.size() - pos, 255);
            page.lacing += char(n);
            page.body += p.mid(pos, n);
            pos += n;
            const bool done = n < 255;
            if (page.lacing.size() == 255) flush(!done);
            if (done) break;
        }
    }
    if (!page.lacing.isEmpty()) flush(false);
    return pages;
}

} // namespace

std::optional<Pairs> readOggTags(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const auto h = readHeaders(f);
    if (!h) return std::nullopt;
    QByteArray tail;
    const auto c = commentsOf(*h, &tail);
    if (!c) return std::nullopt;
    return vorbis::fields(*c);
}

std::optional<QString> writeOgg(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    Q_UNUSED(notStored);   // names are free-form: everything fits
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return QString("Can't open the file: " + in.errorString());
    const auto h = readHeaders(in);
    if (!h) return std::nullopt;
    QByteArray tail;
    auto c = commentsOf(*h, &tail);
    if (!c) return std::nullopt;
    vorbis::apply(*c, changes);
    QList<QByteArray> packets = h->packets;
    packets[1] = (h->codec == Codec::Vorbis ? QByteArray("\x03vorbis") : QByteArray("OpusTags")) + vorbis::build(*c) + tail;
    const QList<Page> header = paginate(*h, packets);
    const qint64 shift = qint64(header.size()) - h->pages;   // audio pages renumbered by this

    const QString temp = tempPathFor(path);
    QFile out(temp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString("Can't write in this folder: " + out.errorString());
    auto abandon = [&](const QString& why) {
        out.close();
        QFile::remove(temp);
        return std::optional<QString>(why);
    };
    for (const Page& p : header) out.write(pageBytes(p));
    for (qint64 at = h->end; at < in.size();) {
        Page p;
        if (!readPage(in, at, &p)) {   // trailing bytes that aren't a page: keep them
            in.seek(at);
            out.write(in.readAll());
            break;
        }
        if (p.serial != h->serial) {
            out.close();
            QFile::remove(temp);
            return std::nullopt;   // a second stream: FFmpeg's turn
        }
        if (shift == 0) {
            in.seek(at);
            out.write(in.read(p.size));
        } else {
            p.seq = quint32(qint64(p.seq) + shift);
            out.write(pageBytes(p));
        }
        at += p.size;
    }
    if (out.error() != QFile::NoError) return abandon("Writing failed: " + out.errorString());
    out.close();
    in.close();
    QString error;
    if (!replaceFile(path, temp, &error)) return error;
    return QString();
}

} // namespace meta
