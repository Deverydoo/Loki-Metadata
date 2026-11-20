// MP3 tags, read and written without FFmpeg (the ID3v2 work is in Id3.cpp).
//
// The new tag is written ahead of the audio, which is copied as it is. An
// ID3v1 tag at the end, if there is one, is brought in line. Files with an
// ID3v2.2 tag (rare, very old) are left to FFmpeg.

#include "Id3.h"
#include "MetaIO.h"

#include <QFile>

namespace meta {

namespace {

// --- ID3v1 (the old fixed-size tag at the very end) --------------------------

struct V1 {
    bool present = false;
    QByteArray bytes;   // the 128 bytes
};

V1 readV1(QFile& f)
{
    V1 v;
    if (f.size() < 128) return v;
    f.seek(f.size() - 128);
    v.bytes = f.read(128);
    v.present = v.bytes.startsWith("TAG");
    return v;
}

QString v1Text(const QByteArray& b, int at, int len)
{
    QByteArray s = b.mid(at, len);
    const qsizetype nul = s.indexOf('\0');
    if (nul >= 0) s.truncate(nul);
    return QString::fromLatin1(s).trimmed();
}

Pairs fieldsOfV1(const V1& v)
{
    if (!v.present) return {};
    Pairs out;
    auto add = [&](const char* k, const QString& s) {
        if (!s.isEmpty()) out.append({QString(k), s});
    };
    add("title", v1Text(v.bytes, 3, 30));
    add("artist", v1Text(v.bytes, 33, 30));
    add("album", v1Text(v.bytes, 63, 30));
    add("date", v1Text(v.bytes, 93, 4));
    const bool hasTrack = v.bytes[125] == 0 && v.bytes[126] != 0;
    add("comment", v1Text(v.bytes, 97, hasTrack ? 28 : 30));
    if (hasTrack) add("track", QString::number(quint8(v.bytes[126])));
    const int g = quint8(v.bytes[127]);
    if (g < id3::genres().size()) add("genre", id3::genres()[g]);
    return out;
}

QByteArray v1Field(const QString& s, int len) { return s.toLatin1().left(len).leftJustified(len, '\0'); }

// The ID3v1 tag, matching @p fields.
QByteArray buildV1(const V1& old, const Pairs& fields)
{
    auto value = [&](const char* k) {
        for (const auto& [key, v] : fields)
            if (key == k) return v;
        return QString();
    };
    QByteArray b("TAG");
    b += v1Field(value("title"), 30);
    b += v1Field(value("artist"), 30);
    b += v1Field(value("album"), 30);
    b += v1Field(value("date").left(4), 4);
    bool ok = false;
    const int track = value("track").section('/', 0, 0).toInt(&ok);
    if (ok && track > 0 && track < 256) {
        b += v1Field(value("comment"), 28);
        b += '\0';
        b += char(track);
    } else {
        b += v1Field(value("comment"), 30);
    }
    const qsizetype g = id3::genres().indexOf(value("genre"));
    b += g >= 0 ? char(g) : old.bytes.size() == 128 ? old.bytes[127] : char(0xFF);
    return b;
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

// The tag at the start of the file, and where the audio starts.
std::optional<id3::Tag> leadingTag(QFile& f, qint64* audioAt)
{
    f.seek(0);
    const qint64 size = id3::tagSize(f.read(10));
    *audioAt = size;
    f.seek(0);
    return id3::parse(size ? f.read(size) : QByteArray());
}

} // namespace

std::optional<Pairs> readMp3Tags(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    qint64 audioAt = 0;
    const auto tag = leadingTag(f, &audioAt);
    if (!tag) return std::nullopt;
    Pairs out = id3::fields(*tag);
    for (const auto& [k, v] : fieldsOfV1(readV1(f))) {   // the old tag fills gaps only
        bool have = false;
        for (const auto& [key, old] : out) have = have || key == k;
        if (!have) out.append({k, v});
    }
    return out;
}

std::optional<QString> rewriteMp3Tag(const QString& path, const std::function<void(id3::Tag&)>& edit)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return QString("Can't open the file: " + in.errorString());
    qint64 audioAt = 0;
    auto tag = leadingTag(in, &audioAt);
    if (!tag) return std::nullopt;   // ID3v2.2 or damaged
    const V1 v1 = readV1(in);
    edit(*tag);
    const QByteArray bytes = id3::build(*tag, 1024);   // padding, so other taggers can edit in place

    const QString temp = tempPathFor(path);
    QFile out(temp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString("Can't write in this folder: " + out.errorString());
    out.write(bytes);
    const qint64 audioEnd = in.size() - (v1.present ? 128 : 0);
    if (!copyRange(in, out, audioAt, audioEnd - audioAt)) {
        out.close();
        QFile::remove(temp);
        return QString("Copying the audio failed (is the disk full?).");
    }
    if (v1.present) out.write(buildV1(v1, id3::fields(*tag)));
    if (out.error() != QFile::NoError) {
        const QString why = out.errorString();
        out.close();
        QFile::remove(temp);
        return "Writing failed: " + why;
    }
    out.close();
    in.close();
    QString error;
    if (!replaceFile(path, temp, &error)) return error;
    return QString();
}

std::optional<QString> writeMp3(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    return rewriteMp3Tag(path, [&](id3::Tag& tag) { id3::apply(tag, changes, notStored); });
}

} // namespace meta
