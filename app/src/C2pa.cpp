// Content Credentials (C2PA), read and removed. See C2pa.h.
//
// The record is a JUMBF "manifest store": boxes of boxes, labelled. Each
// manifest in it holds assertions (what was done, by which tool, the hash of
// the file), a claim listing them, and a signature (COSE) over the claim with
// the signer's certificate. The last manifest is the file's current one; any
// before it are earlier steps (an edit of an AI picture, say).
//
// What is checked here is only whether the file's bytes still match the hash
// in the record (c2pa.hash.data). The signature isn't verified; the app says
// so rather than call anything "trusted".

#include "C2pa.h"
#include "Id3.h"
#include "MetaIO.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QtEndian>
#include <QFloat16>
#include <QJsonDocument>

#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>

namespace meta::c2pa {

namespace {

// --- CBOR (RFC 8949): enough to read a manifest ---------------------------------
//
// Maps become QVariantMaps (number keys as text), byte strings QByteArrays,
// tags are dropped and their content kept. Damaged input gives up quietly.

class Cbor {
public:
    explicit Cbor(const QByteArray& b) : b_(b) {}

    QVariant value(int depth = 0)
    {
        if (!ok_ || depth > 48 || i_ >= b_.size()) return fail();
        const quint8 ib = quint8(b_[i_++]);
        const int major = ib >> 5, info = ib & 31;
        if (major == 7) return simple(info);
        if (info == 31) return indefinite(major, depth);
        const quint64 n = arg(info);
        if (!ok_) return {};
        switch (major) {
        case 0: return QVariant::fromValue(n);
        case 1: return QVariant::fromValue(-1 - qint64(n));
        case 2: return bytes(n);
        case 3: return QString::fromUtf8(bytes(n));
        case 4: {
            if (n > quint64(b_.size() - i_)) return fail();
            QVariantList out;
            for (quint64 k = 0; k < n && ok_; ++k) out.append(value(depth + 1));
            return out;
        }
        case 5: {
            if (n > quint64(b_.size() - i_)) return fail();
            QVariantMap out;
            for (quint64 k = 0; k < n && ok_; ++k) {
                const QString key = value(depth + 1).toString();
                out.insert(key, value(depth + 1));
            }
            return out;
        }
        case 6: return value(depth + 1);   // a tag: what it wraps
        }
        return fail();
    }
    bool ok() const { return ok_; }

private:
    QVariant fail()
    {
        ok_ = false;
        return {};
    }

    quint64 arg(int info)
    {
        if (info < 24) return quint64(info);
        const int n = info == 24 ? 1 : info == 25 ? 2 : info == 26 ? 4 : info == 27 ? 8 : 0;
        if (!n || i_ + n > b_.size()) {
            ok_ = false;
            return 0;
        }
        quint64 v = 0;
        for (int k = 0; k < n; ++k) v = (v << 8) | quint8(b_[i_ + k]);
        i_ += n;
        return v;
    }

    QByteArray bytes(quint64 n)
    {
        if (n > quint64(b_.size() - i_)) {
            ok_ = false;
            return {};
        }
        const QByteArray out = b_.mid(i_, qsizetype(n));
        i_ += qsizetype(n);
        return out;
    }

    QVariant simple(int info)
    {
        switch (info) {
        case 20: return false;
        case 21: return true;
        case 22:
        case 23: return {};
        case 25: {
            if (i_ + 2 > b_.size()) return fail();
            const quint16 h = qFromBigEndian<quint16>(b_.constData() + i_);
            i_ += 2;
            qfloat16 f;
            memcpy(&f, &h, 2);
            return double(float(f));
        }
        case 26: {
            if (i_ + 4 > b_.size()) return fail();
            const float f = qFromBigEndian<float>(b_.constData() + i_);
            i_ += 4;
            return double(f);
        }
        case 27: {
            if (i_ + 8 > b_.size()) return fail();
            const double d = qFromBigEndian<double>(b_.constData() + i_);
            i_ += 8;
            return d;
        }
        }
        if (info < 24) return {};
        if (info == 24) {
            ++i_;
            return {};
        }
        return fail();
    }

    QVariant indefinite(int major, int depth)
    {
        auto atBreak = [&] { return i_ < b_.size() && quint8(b_[i_]) == 0xFF; };
        if (major == 2 || major == 3) {
            QByteArray all;
            while (ok_ && !atBreak()) {
                const QVariant part = value(depth + 1);
                all += major == 2 ? part.toByteArray() : part.toString().toUtf8();
            }
            ++i_;
            return major == 2 ? QVariant(all) : QVariant(QString::fromUtf8(all));
        }
        if (major == 4) {
            QVariantList out;
            while (ok_ && !atBreak() && i_ < b_.size()) out.append(value(depth + 1));
            ++i_;
            return out;
        }
        if (major == 5) {
            QVariantMap out;
            while (ok_ && !atBreak() && i_ < b_.size()) {
                const QString key = value(depth + 1).toString();
                out.insert(key, value(depth + 1));
            }
            ++i_;
            return out;
        }
        return fail();
    }

    const QByteArray& b_;
    qsizetype i_ = 0;
    bool ok_ = true;
};

QVariant cbor(const QByteArray& b)
{
    Cbor c(b);
    const QVariant v = c.value();
    return c.ok() ? v : QVariant();
}

// --- JUMBF: boxes of boxes -------------------------------------------------------

struct Node {
    QString label;
    QList<Node> children;                          // nested superboxes
    QList<QPair<QByteArray, QByteArray>> content;  // other boxes: type, body
};

quint32 be32(const QByteArray& b, qsizetype at) { return qFromBigEndian<quint32>(b.constData() + at); }

// The label in a description box ("jumd"): a 16-byte type, a toggles byte,
// then the label when toggle 0x02 is set.
QString labelOf(const QByteArray& jumd)
{
    if (jumd.size() < 17 || !(quint8(jumd[16]) & 0x02)) return {};
    const QByteArray rest = jumd.mid(17);
    const qsizetype nul = rest.indexOf('\0');
    return QString::fromUtf8(nul < 0 ? rest : rest.left(nul));
}

// The boxes between from and to, as children of @p into.
bool walk(const QByteArray& b, qsizetype from, qsizetype to, Node& into, int depth)
{
    if (depth > 16) return false;
    for (qsizetype pos = from; pos + 8 <= to;) {
        qint64 size = be32(b, pos);
        int head = 8;
        if (size == 1) {
            if (pos + 16 > to) return false;
            size = qint64(qFromBigEndian<quint64>(b.constData() + pos + 8));
            head = 16;
        } else if (size == 0) {
            size = to - pos;
        }
        if (size < head || pos + size > to) return false;
        const QByteArray type = b.mid(pos + 4, 4);
        if (type == "jumb") {
            Node n;
            if (!walk(b, pos + head, pos + qsizetype(size), n, depth + 1)) return false;
            into.children.append(n);
        } else if (type == "jumd") {
            into.label = labelOf(b.mid(pos + head, qsizetype(size) - head));
        } else {
            into.content.append({type, b.mid(pos + head, qsizetype(size) - head)});
        }
        pos += qsizetype(size);
    }
    return true;
}

const Node* childLabelled(const Node& n, const QString& label)
{
    for (const Node& c : n.children)
        if (c.label == label) return &c;
    return nullptr;
}

// A box's data as CBOR (or JSON, which some writers use for assertions).
QVariant dataOf(const Node& n)
{
    for (const auto& [type, body] : n.content) {
        if (type == "cbor") return cbor(body);
        if (type == "json") return QJsonDocument::fromJson(body).toVariant();
    }
    return {};
}

// "c2pa.actions.v2__1" -> "c2pa.actions": the instance and version don't matter here.
QString baseLabel(const QString& label)
{
    static const QRegularExpression tail(R"((__\d+)?$)");
    static const QRegularExpression version(R"(\.v\d+$)");
    QString s = label;
    s.remove(tail);
    s.remove(version);
    return s;
}

// --- Where the record sits in each format ------------------------------------------

struct Found {
    QByteArray store;         // the manifest store's bytes (one jumb box)
    QList<qint64> mp4Boxes;   // MP4: the boxes holding it, by offset
};

enum class Format { None, Png, Jpeg, Riff, Mp3, Mp4 };

Format formatOf(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "png") return Format::Png;
    if (ext == "jpg" || ext == "jpeg") return Format::Jpeg;
    if (ext == "wav" || ext == "webp") return Format::Riff;
    if (ext == "mp3") return Format::Mp3;
    if (ext == "mp4" || ext == "m4a" || ext == "m4v" || ext == "mov") return Format::Mp4;
    return Format::None;
}

const QByteArray kMp4Uuid = QByteArray::fromHex("d8fec3d61b0e483c92975828877ec481");

bool isStore(const QByteArray& jumbf)
{
    Node root;
    return walk(jumbf, 0, jumbf.size(), root, 0) && !root.children.isEmpty() && root.children[0].label == "c2pa";
}

// PNG: a caBX chunk.
std::optional<QPair<qint64, qint64>> pngChunk(QFile& f)   // where, and its whole length
{
    if (!f.seek(8)) return std::nullopt;
    for (qint64 pos = 8; pos + 12 <= f.size();) {
        f.seek(pos);
        const QByteArray h = f.read(8);
        const qint64 n = be32(h, 0);
        if (pos + 12 + n > f.size()) return std::nullopt;
        if (h.mid(4, 4) == "caBX") return QPair<qint64, qint64>{pos, 12 + n};
        if (h.mid(4, 4) == "IEND") break;
        pos += 12 + n;
    }
    return std::nullopt;
}

// JPEG: APP11 segments, "JP", then a box instance number and a sequence
// number. A box split over several segments repeats its header in each.
struct App11 {
    qint64 at = 0, length = 0;   // the whole segment, marker included
    quint16 instance = 0;
    QByteArray payload;          // the box bytes it carries
};

QList<App11> jpegSegments(QFile& f)
{
    QList<App11> out;
    f.seek(0);
    if (f.read(2) != QByteArray::fromHex("ffd8")) return out;
    for (qint64 pos = 2; pos + 4 <= f.size();) {
        f.seek(pos);
        const QByteArray h = f.read(4);
        if (quint8(h[0]) != 0xFF) break;
        const quint8 marker = quint8(h[1]);
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            pos += 2;
            continue;
        }
        if (marker == 0xFF) {   // fill byte
            ++pos;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) break;   // the picture starts: no more metadata
        const qint64 len = qFromBigEndian<quint16>(h.constData() + 2);
        if (len < 2 || pos + 2 + len > f.size()) break;
        if (marker == 0xEB && len >= 2 + 8) {
            const QByteArray body = f.read(len - 2);
            if (body.startsWith("JP")) {
                App11 s{pos, 2 + len, qFromBigEndian<quint16>(body.constData() + 2), {}};
                const quint32 seq = be32(body, 4);
                QByteArray box = body.mid(8);
                if (seq > 1 && box.size() >= 8) box = box.mid(be32(box, 0) == 1 ? 16 : 8);   // the repeated header
                s.payload = box;
                out.append(s);
            }
        }
        pos += 2 + len;
    }
    return out;
}

// RIFF (WAV, WebP): a top-level "C2PA" chunk.
std::optional<QPair<qint64, qint64>> riffChunk(QFile& f)
{
    f.seek(0);
    const QByteArray head = f.read(12);
    if (head.size() < 12 || !head.startsWith("RIFF")) return std::nullopt;
    for (qint64 pos = 12; pos + 8 <= f.size();) {
        f.seek(pos);
        const QByteArray h = f.read(8);
        const qint64 n = qFromLittleEndian<quint32>(h.constData() + 4);
        if (pos + 8 + n > f.size()) return std::nullopt;
        if (h.left(4) == "C2PA") return QPair<qint64, qint64>{pos, 8 + n + (n & 1)};
        pos += 8 + n + (n & 1);
    }
    return std::nullopt;
}

// MP3: a GEOB frame holding the store.
bool isC2paFrame(const id3::Tag& t, const id3::Frame& fr, QByteArray* object)
{
    if (fr.id != "GEOB") return false;
    const auto c = id3::content(t, fr);
    if (!c || c->size() < 2) return false;
    const quint8 enc = quint8(c->at(0));
    const qsizetype mimeEnd = c->indexOf('\0', 1);
    if (mimeEnd < 0) return false;
    const QByteArray mime = c->mid(1, mimeEnd - 1).toLower();
    if (mime != "application/x-c2pa-manifest-store" && mime != "application/c2pa") return false;
    // Then a file name and a description, each ended in the frame's encoding.
    const bool wide = enc == 1 || enc == 2;
    qsizetype i = mimeEnd + 1;
    for (int field = 0; field < 2; ++field) {
        for (;; i += wide ? 2 : 1) {
            if (i + (wide ? 1 : 0) >= c->size()) return false;
            if (wide ? (c->at(i) == 0 && c->at(i + 1) == 0) : c->at(i) == 0) break;
        }
        i += wide ? 2 : 1;
    }
    if (object) *object = c->mid(i);
    return true;
}

std::optional<id3::Tag> mp3Tag(QFile& f)
{
    f.seek(0);
    const qint64 size = id3::tagSize(f.read(10));
    if (!size) return std::nullopt;
    f.seek(0);
    return id3::parse(f.read(size));
}

// MP4: top-level uuid boxes of C2PA's type. The one whose purpose is
// "manifest" holds the store; "merkle" ones (fragmented files) go with it.
std::optional<Found> mp4Store(QFile& f)
{
    Found found;
    for (qint64 pos = 0; pos + 8 <= f.size();) {
        f.seek(pos);
        const QByteArray h = f.read(16);
        qint64 size = be32(h, 0);
        int head = 8;
        if (size == 1) {
            size = qint64(qFromBigEndian<quint64>(h.constData() + 8));
            head = 16;
        } else if (size == 0) {
            size = f.size() - pos;
        }
        if (size < head || pos + size > f.size()) break;
        if (h.mid(4, 4) == "uuid" && size >= head + 16 + 4) {
            f.seek(pos + head);
            if (f.read(16) == kMp4Uuid) {
                found.mp4Boxes.append(pos);
                const QByteArray body = f.read(qMin<qint64>(size - head - 16, 64 << 20));
                const qsizetype nul = body.indexOf('\0', 4);   // after version and flags, the purpose
                if (nul > 0 && body.mid(4, nul - 4) == "manifest" && found.store.isEmpty())
                    found.store = body.mid(nul + 1 + 8);       // then the merkle offset, then the store
            }
        }
        pos += size;
    }
    if (found.mp4Boxes.isEmpty()) return std::nullopt;
    return found;
}

std::optional<Found> locate(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    Found found;
    switch (formatOf(path)) {
    case Format::Png:
        if (const auto c = pngChunk(f)) {
            f.seek(c->first + 8);
            found.store = f.read(c->second - 12);
            return found;
        }
        return std::nullopt;
    case Format::Jpeg: {
        QMap<quint16, QByteArray> byInstance;
        QList<quint16> order;
        for (const App11& s : jpegSegments(f)) {
            if (!byInstance.contains(s.instance)) order.append(s.instance);
            byInstance[s.instance] += s.payload;
        }
        for (quint16 i : order)
            if (isStore(byInstance[i])) {
                found.store = byInstance[i];
                return found;
            }
        return std::nullopt;
    }
    case Format::Riff:
        if (const auto c = riffChunk(f)) {
            f.seek(c->first + 4);
            const quint32 n = qFromLittleEndian<quint32>(f.read(4).constData());
            found.store = f.read(n);
            return found;
        }
        return std::nullopt;
    case Format::Mp3: {
        const auto tag = mp3Tag(f);
        if (!tag) return std::nullopt;
        for (const id3::Frame& fr : tag->frames)
            if (QByteArray object; isC2paFrame(*tag, fr, &object)) {
                found.store = object;
                return found;
            }
        return std::nullopt;
    }
    case Format::Mp4:
        return mp4Store(f);
    case Format::None:
        break;
    }
    return std::nullopt;
}

// --- Turning the record into plain words ---------------------------------------------

// IPTC's digital source types, said plainly.
QString sourceName(const QString& url, bool* ai)
{
    const QString t = url.section('/', -1);
    *ai = t == "trainedAlgorithmicMedia" || t == "compositeWithTrainedAlgorithmicMedia";
    static const QHash<QString, QString> names{
        {"trainedAlgorithmicMedia", "Made by AI"},
        {"compositeWithTrainedAlgorithmicMedia", "Partly made by AI"},
        {"algorithmicMedia", "Made by software (not AI)"},
        {"digitalCapture", "Taken with a camera or recorder"},
        {"computationalCapture", "Taken with a camera, with processing"},
        {"negativeFilm", "Scanned from film"},
        {"positiveFilm", "Scanned from film"},
        {"print", "Scanned from a print"},
        {"humanEdits", "Edited by a person"},
        {"compositeCapture", "Combined from several captures"},
        {"compositeSynthetic", "Combined with made-up material"},
        {"composite", "Combined from several sources"},
        {"algorithmicallyEnhanced", "Enhanced by software"},
        {"dataDrivenMedia", "Made by software from data"},
        {"digitalArt", "Digital art made by a person"},
        {"digitalCreation", "Made on a computer by a person"},
        {"virtualRecording", "Recorded inside software"},
        {"screenCapture", "A screen capture"},
    };
    return names.value(t, t);
}

QString actionName(const QString& action)
{
    static const QHash<QString, QString> names{
        {"c2pa.created", "Created"},          {"c2pa.opened", "Opened"},           {"c2pa.edited", "Edited"},
        {"c2pa.converted", "Converted"},      {"c2pa.transcoded", "Re-encoded"},   {"c2pa.repackaged", "Repackaged"},
        {"c2pa.watermarked", "Watermarked"},  {"c2pa.watermarked.bound", "Watermarked"},
        {"c2pa.watermarked.unbound", "Watermarked"},                               {"c2pa.placed", "Something placed in it"},
        {"c2pa.cropped", "Cropped"},          {"c2pa.resized", "Resized"},         {"c2pa.color_adjustments", "Colours adjusted"},
        {"c2pa.filtered", "Filtered"},        {"c2pa.drawing", "Drawn on"},        {"c2pa.orientation", "Turned"},
        {"c2pa.published", "Published"},      {"c2pa.removed", "Something removed"}, {"c2pa.redacted", "Redacted"},
        {"c2pa.translated", "Translated"},    {"c2pa.edited.metadata", "Metadata edited"}, {"c2pa.unknown", "Changed"},
    };
    if (names.contains(action)) return names.value(action);
    QString s = action;
    s.remove(QRegularExpression("^c2pa\\."));
    s.replace('_', ' ').replace('.', ' ');
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

// A software agent is text in older records, {name, version} in newer ones.
QString agentName(const QVariant& v)
{
    if (v.typeId() == QMetaType::QVariantMap) {
        const QVariantMap m = v.toMap();
        const QString name = m.value("name").toString(), version = m.value("version").toString();
        return version.isEmpty() || name.contains(version) ? name : name + " " + version;
    }
    return v.toString();
}

QString whenText(const QString& iso)
{
    QDateTime t = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!t.isValid()) t = QDateTime::fromString(iso, Qt::ISODate);
    if (!t.isValid()) return iso;
    // Midnight UTC exactly is a date with no time (OpenAI writes them so):
    // shown as that date, not as the evening before.
    const QDateTime utc = t.toUTC();
    if (utc.time() == QTime(0, 0)) return QLocale().toString(utc.date(), QLocale::ShortFormat);
    return QLocale().toString(t.toLocalTime(), QLocale::ShortFormat);
}

// The signer's name, from the first certificate in the signature.
QString signerOf(const QVariant& signature)
{
    const QVariantList cose = signature.toList();   // protected, unprotected, payload, signature
    if (cose.size() < 2) return {};
    const QVariantMap prot = cbor(cose[0].toByteArray()).toMap();
    const QVariantMap unprot = cose[1].toMap();
    QVariant chain = prot.value("33");
    if (!chain.isValid()) chain = unprot.value("33");
    if (!chain.isValid()) chain = unprot.value("x5chain");
    const QByteArray der = chain.typeId() == QMetaType::QVariantList ? chain.toList().value(0).toByteArray() : chain.toByteArray();
    if (der.isEmpty()) return {};
    PCCERT_CONTEXT cert = CertCreateCertificateContext(X509_ASN_ENCODING, reinterpret_cast<const BYTE*>(der.constData()),
                                                       DWORD(der.size()));
    if (!cert) return {};
    wchar_t buf[512] = {};
    QString name;
    if (CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(szOID_ORGANIZATION_NAME), buf, 512) > 1)
        name = QString::fromWCharArray(buf);
    if (name.isEmpty() && CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, buf, 512) > 1)
        name = QString::fromWCharArray(buf);
    CertFreeCertificateContext(cert);
    return name;
}

// Whether the file's bytes, minus the excluded ranges, still hash to what
// the record says.
QString hashMatch(const QString& path, const QVariantMap& binding, const QString& claimAlg)
{
    const QString alg = binding.value("alg", claimAlg).toString().toLower();
    QCryptographicHash::Algorithm a;
    if (alg == "sha256" || alg.isEmpty()) a = QCryptographicHash::Sha256;
    else if (alg == "sha384") a = QCryptographicHash::Sha384;
    else if (alg == "sha512") a = QCryptographicHash::Sha512;
    else return {};
    const QByteArray expected = binding.value("hash").toByteArray();
    if (expected.isEmpty()) return {};

    QList<QPair<qint64, qint64>> skip;
    for (const QVariant& e : binding.value("exclusions").toList()) {
        const QVariantMap m = e.toMap();
        skip.append({m.value("start").toLongLong(), m.value("length").toLongLong()});
    }
    std::sort(skip.begin(), skip.end());

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(a);
    auto add = [&](qint64 from, qint64 to) {
        f.seek(from);
        while (from < to) {
            const QByteArray buf = f.read(std::min<qint64>(to - from, 4 << 20));
            if (buf.isEmpty()) return false;
            h.addData(buf);
            from += buf.size();
        }
        return true;
    };
    qint64 pos = 0;
    for (const auto& [start, length] : skip) {
        if (start > pos && !add(pos, std::min(start, f.size()))) return {};
        pos = std::max(pos, start + length);
    }
    if (pos < f.size() && !add(pos, f.size())) return {};
    return h.result() == expected ? "unchanged" : "changed";
}

QVariantMap describe(const QString& path, const QByteArray& store, bool checkMatch)
{
    QVariantMap out{{"present", true}};
    Node root;
    walk(store, 0, store.size(), root, 0);
    const Node* top = root.children.isEmpty() ? nullptr : &root.children[0];
    if (!top || top->label != "c2pa" || top->children.isEmpty()) {
        out["notes"] = QStringList{"The record is there but couldn't be read."};
        return out;
    }
    const Node& active = top->children.last();
    out["history"] = int(top->children.size()) - 1;

    QStringList actions, notes;
    QString source, tool, when, provider, author;
    bool ai = false, watermark = false, fingerprint = false;
    QVariantMap dataHash;   // only this kind of hash is checked here

    // AI anywhere in the history counts: an edited AI picture is still one.
    for (const Node& manifest : top->children) {
        const Node* assertions = childLabelled(manifest, "c2pa.assertions");
        if (!assertions) continue;
        const bool isActive = &manifest == &active;
        for (const Node& a : assertions->children) {
            const QString label = baseLabel(a.label);
            if (label == "c2pa.actions") {
                for (const QVariant& v : dataOf(a).toMap().value("actions").toList()) {
                    const QVariantMap act = v.toMap();
                    const QString name = act.value("action").toString();
                    const QString agent = agentName(act.value("softwareAgent"));
                    bool thisAi = false;
                    const QString dst = act.value("digitalSourceType").toString();
                    if (!dst.isEmpty()) {
                        const QString plain = sourceName(dst, &thisAi);
                        ai = ai || thisAi;
                        if (isActive || source.isEmpty() || thisAi) source = plain;
                    }
                    if (name.startsWith("c2pa.watermarked")) watermark = true;
                    if (!isActive) continue;
                    if (name == "c2pa.created" || name == "c2pa.opened") {
                        if (!agent.isEmpty() && tool.isEmpty()) tool = agent;
                        if (when.isEmpty()) when = act.value("when").toString();
                    }
                    QString line = actionName(name);
                    if (!agent.isEmpty()) line += " with " + agent;
                    if (!actions.contains(line)) actions.append(line);
                }
            } else if (label == "c2pa.soft-binding") {
                fingerprint = true;
            } else if (isActive && label == "c2pa.hash.data") {
                dataHash = dataOf(a).toMap();
            } else if (isActive && label == "stds.schema-org.CreativeWork") {
                QStringList names;
                for (const QVariant& p : dataOf(a).toMap().value("author").toList())
                    if (const QString n = p.toMap().value("name").toString(); !n.isEmpty()) names.append(n);
                author = names.join(", ");
            } else if (isActive) {   // a maker's own record (Suno's provenance, say)
                const QVariantMap m = dataOf(a).toMap();
                const QString who = m.value("providerName").toString();
                QString what = m.value("systemName").toString();
                if (!m.value("systemVersion").toString().isEmpty()) what += " " + m.value("systemVersion").toString();
                if (!who.isEmpty() || !what.trimmed().isEmpty())
                    provider = who.isEmpty() ? what.trimmed() : what.trimmed().isEmpty() ? who : who + " (" + what.trimmed() + ")";
                if (when.isEmpty()) when = m.value("createdAt").toString();
            }
        }
    }

    // The claim: who recorded it.
    QString claimAlg;
    for (const Node& c : active.children) {
        if (!c.label.startsWith("c2pa.claim")) continue;
        const QVariantMap claim = dataOf(c).toMap();
        claimAlg = claim.value("alg").toString();
        QVariant info = claim.value("claim_generator_info");
        if (info.typeId() == QMetaType::QVariantList) info = info.toList().value(0);
        QString by = agentName(info);
        if (by.isEmpty()) by = claim.value("claim_generator").toString().section(' ', 0, 0).replace('/', ' ');
        out["recordedBy"] = by;
        if (tool.isEmpty()) tool = by;
    }
    if (const Node* sig = childLabelled(active, "c2pa.signature")) out["signer"] = signerOf(dataOf(*sig));

    out["hashable"] = !dataHash.isEmpty();
    out["match"] = checkMatch && !dataHash.isEmpty() ? hashMatch(path, dataHash, claimAlg) : QString();

    if (watermark)
        notes.append("The maker also hid a watermark in the content itself. Removing the credentials doesn't remove that.");
    if (fingerprint)
        notes.append("The maker keeps a fingerprint of the content, so it can be matched to this record even without it.");
    out["source"] = source;
    out["ai"] = ai;
    out["tool"] = tool;
    out["provider"] = provider;
    out["author"] = author;
    out["when"] = when.isEmpty() ? QString() : whenText(when);
    out["actions"] = actions;
    out["notes"] = notes;
    return out;
}

// --- Writing a file without it --------------------------------------------------------

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

// The file with the byte ranges in @p cut left out, then @p fix applied to
// the new file (a RIFF size, say).
QString writeWithout(const QString& path, QList<QPair<qint64, qint64>> cut, const std::function<bool(QFile&)>& fix = {})
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return "Can't open the file: " + in.errorString();
    std::sort(cut.begin(), cut.end());
    const QString temp = tempPathFor(path);
    QFile out(temp);
    if (!out.open(QIODevice::ReadWrite | QIODevice::Truncate)) return "Can't write in this folder: " + out.errorString();
    auto abandon = [&](const QString& why) {
        out.close();
        QFile::remove(temp);
        return why;
    };
    qint64 pos = 0;
    for (const auto& [at, length] : cut) {
        if (at > pos && !copyRange(in, out, pos, at - pos)) return abandon("Copying the file failed (is the disk full?).");
        pos = std::max(pos, at + length);
    }
    if (pos < in.size() && !copyRange(in, out, pos, in.size() - pos)) return abandon("Copying the file failed (is the disk full?).");
    if (fix && !fix(out)) return abandon("The file would be too big for its header.");
    if (out.error() != QFile::NoError) return abandon("Writing failed: " + out.errorString());
    out.close();
    in.close();
    QString error;
    if (!replaceFile(path, temp, &error)) return error;
    return {};
}

} // namespace

bool supports(const QString& path) { return formatOf(path) != Format::None; }

QVariantMap read(const QString& path, bool checkMatch)
{
    if (!supports(path)) return {};
    const auto found = locate(path);
    if (!found) return {};
    if (found->store.isEmpty()) return {{"present", true}, {"notes", QStringList{"The record is there but couldn't be read."}}};
    return describe(path, found->store, checkMatch);
}

QString remove(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return "Can't open the file: " + f.errorString();
    switch (formatOf(path)) {
    case Format::Png: {
        QList<QPair<qint64, qint64>> cut;
        if (const auto c = pngChunk(f)) cut.append(*c);
        f.close();
        return cut.isEmpty() ? QString() : writeWithout(path, cut);
    }
    case Format::Jpeg: {
        QMap<quint16, QByteArray> byInstance;
        QMultiMap<quint16, App11> segments;
        for (const App11& s : jpegSegments(f)) {
            byInstance[s.instance] += s.payload;
            segments.insert(s.instance, s);
        }
        f.close();
        QList<QPair<qint64, qint64>> cut;
        for (auto it = byInstance.begin(); it != byInstance.end(); ++it)
            if (isStore(it.value()))
                for (const App11& s : segments.values(it.key())) cut.append({s.at, s.length});
        return cut.isEmpty() ? QString() : writeWithout(path, cut);
    }
    case Format::Riff: {
        QList<QPair<qint64, qint64>> cut;
        if (const auto c = riffChunk(f)) cut.append(*c);
        f.close();
        if (cut.isEmpty()) return {};
        return writeWithout(path, cut, [](QFile& out) {
            const qint64 total = out.size();
            if (total - 8 > 0xFFFFFFFFLL) return false;
            char n[4];
            qToLittleEndian<quint32>(quint32(total - 8), n);
            out.seek(4);
            return out.write(n, 4) == 4;
        });
    }
    case Format::Mp3: {
        f.close();
        const auto r = rewriteMp3Tag(path, [](id3::Tag& tag) {
            tag.frames.removeIf([&](const id3::Frame& fr) { return isC2paFrame(tag, fr, nullptr); });
        });
        return r ? *r : QString("This MP3's tag is an old kind that can't be rewritten here.");
    }
    case Format::Mp4: {
        const auto found = mp4Store(f);
        f.close();
        if (!found) return {};
        const auto r = removeMp4Boxes(path, found->mp4Boxes);
        return r ? *r : QString("This file's layout can't be rewritten safely, so nothing was removed.");
    }
    case Format::None:
        break;
    }
    return "This kind of file can't carry Content Credentials here.";
}

} // namespace meta::c2pa
