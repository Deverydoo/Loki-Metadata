// Loki Metadata's tests: every format read and written for real (on small
// generated files in a temporary folder), the editor's bulk editing, and the
// page itself, drawn offscreen.
//
// Set LOKI_UI_SHOTS=<folder> to save pictures of the page.

#include "MetaEditor.h"
#include "MetaIO.h"
#include "Zip.h"
#include "DocSamples.h"
#include "C2paSamples.h"

#include <runa.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QProcess>
#include <QPainter>
#include <QPdfWriter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <QtQml/qqmlextensionplugin.h>

#include <functional>

Q_IMPORT_QML_PLUGIN(LokiMetadataPlugin)

using namespace meta;

static QString tagOf(const Pairs& tags, const QString& key)
{
    for (const auto& [k, v] : tags)
        if (k == key) return v;
    return {};
}

static bool hasTag(const Pairs& tags, const QString& key)
{
    for (const auto& [k, v] : tags)
        if (k == key) return true;
    return false;
}

static QString infoOf(const Pairs& info, const QString& label) { return tagOf(info, label); }

// A JPEG with a big-endian EXIF block: Make "Canon" in IFD0, and an Exif
// sub-directory holding DateTimeOriginal.
static QByteArray jpegWithExif(const QByteArray& plainJpeg)
{
    QByteArray t("MM\0\x2A", 4);
    auto be16 = [&](quint16 v) { char b[2]; qToBigEndian(v, b); t.append(b, 2); };
    auto be32 = [&](quint32 v) { char b[4]; qToBigEndian(v, b); t.append(b, 4); };
    be32(8);
    // IFD0 at 8: 2 entries -> ends at 38
    be16(2);
    be16(0x010F); be16(2); be32(6); be32(38);      // Make -> "Canon\0" at 38
    be16(0x8769); be16(4); be32(1); be32(44);      // ExifIFD at 44
    be32(0);
    t.append("Canon\0", 6);
    // ExifIFD at 44: 1 entry -> ends at 62
    be16(1);
    be16(0x9003); be16(2); be32(20); be32(62);     // DateTimeOriginal at 62
    be32(0);
    t.append("2024:05:06 07:08:09\0", 20);

    const QByteArray payload = QByteArray("Exif\0\0", 6) + t;
    QByteArray seg("\xFF\xE1", 2);
    char len[2];
    qToBigEndian<quint16>(quint16(payload.size() + 2), len);
    seg.append(len, 2);
    seg += payload;
    return plainJpeg.left(2) + seg + plainJpeg.mid(2);
}

// XMP as Photoshop wrote it into a PNG (properties as attributes, an edit
// history). BOM stands for the byte-order mark Adobe puts in begin="".
static QByteArray photoshopXmp()
{
    QByteArray x(R"x(<?xpacket begin="BOM" id="W5M0MpCehiHzreSzNTczkc9d"?> <x:xmpmeta xmlns:x="adobe:ns:meta/" x:xmptk="Adobe XMP Core 10.0-c000 79.d20e46630, 2025/12/09-02:11:23        "> <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"> <rdf:Description rdf:about="" xmlns:xmp="http://ns.adobe.com/xap/1.0/" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:photoshop="http://ns.adobe.com/photoshop/1.0/" xmlns:xmpMM="http://ns.adobe.com/xap/1.0/mm/" xmlns:stEvt="http://ns.adobe.com/xap/1.0/sType/ResourceEvent#" xmp:CreatorTool="Adobe Photoshop 27.9 (Windows)" xmp:CreateDate="2026-10-16T21:20:34-04:00" xmp:ModifyDate="2026-11-03T07:04:48-05:00" xmp:MetadataDate="2026-11-03T07:04:48-05:00" dc:format="image/png" photoshop:ColorMode="3" xmpMM:InstanceID="xmp.iid:52e9425a-1446-1d48-8dee-d4c392f9d290" xmpMM:DocumentID="xmp.did:52e9425a-1446-1d48-8dee-d4c392f9d290" xmpMM:OriginalDocumentID="xmp.did:52e9425a-1446-1d48-8dee-d4c392f9d290"> <xmpMM:History> <rdf:Seq> <rdf:li stEvt:action="created" stEvt:instanceID="xmp.iid:52e9425a-1446-1d48-8dee-d4c392f9d290" stEvt:when="2026-10-16T21:20:34-04:00" stEvt:softwareAgent="Adobe Photoshop 27.9 (Windows)"/> </rdf:Seq> </xmpMM:History> </rdf:Description> </rdf:RDF> </x:xmpmeta> <?xpacket end="r"?>)x");
    x.replace("BOM", QByteArray::fromHex("EFBBBF"));
    return x;
}

// XMP the way Lightroom nests it: dc:title as a language alternative,
// dc:creator as an ordered list.
static const char* kNestedXmp =
    R"x(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><rdf:Description rdf:about="" xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title><rdf:Alt><rdf:li xml:lang="x-default">Old XMP title</rdf:li></rdf:Alt></dc:title><dc:creator><rdf:Seq><rdf:li>Ann</rdf:li><rdf:li>Bo</rdf:li></rdf:Seq></dc:creator></rdf:Description></rdf:RDF></x:xmpmeta>)x";

static QByteArray app1(const QByteArray& payload)
{
    QByteArray seg = QByteArray::fromHex("FFE1");
    char len[2];
    qToBigEndian<quint16>(quint16(payload.size() + 2), len);
    seg.append(len, 2);
    return seg + payload;
}

// --- ID3v2 by hand, for the MP3 tests ----------------------------------------

static QByteArray id3Frame(int version, const QByteArray& id, const QByteArray& data)
{
    QByteArray f = id;
    char n[4];
    if (version == 4) {
        const quint32 v = quint32(data.size());
        n[0] = char((v >> 21) & 0x7f); n[1] = char((v >> 14) & 0x7f); n[2] = char((v >> 7) & 0x7f); n[3] = char(v & 0x7f);
    } else {
        qToBigEndian<quint32>(quint32(data.size()), n);
    }
    f.append(n, 4);
    f.append(QByteArray(2, '\0'));
    return f + data;
}

static QByteArray id3Tag(int version, const QByteArray& frames)
{
    const quint32 v = quint32(frames.size());
    QByteArray t("ID3");
    t += char(version);
    t += QByteArray(2, '\0');
    t += char((v >> 21) & 0x7f); t += char((v >> 14) & 0x7f); t += char((v >> 7) & 0x7f); t += char(v & 0x7f);
    return t + frames;
}

// The frames of the ID3v2 tag at the start of @p file: id -> list of data.
static QMultiHash<QByteArray, QByteArray> id3Frames(const QByteArray& file, int* version = nullptr, qsizetype* end = nullptr)
{
    QMultiHash<QByteArray, QByteArray> out;
    if (!file.startsWith("ID3")) return out;
    const int ver = quint8(file[3]);
    if (version) *version = ver;
    auto ss = [&](qsizetype at) {
        return (quint32(quint8(file[at])) << 21) | (quint32(quint8(file[at + 1])) << 14) | (quint32(quint8(file[at + 2])) << 7)
             | quint32(quint8(file[at + 3]));
    };
    const qsizetype size = ss(6);
    if (end) *end = 10 + size;
    for (qsizetype pos = 10; pos + 10 <= 10 + size && file[pos] != 0;) {
        const quint32 n = ver == 4 ? ss(pos + 4) : qFromBigEndian<quint32>(file.constData() + pos + 4);
        out.insert(file.mid(pos, 4), file.mid(pos + 10, n));
        pos += 10 + n;
    }
    return out;
}

static QByteArray silentMp3Frames()
{
    QByteArray b;
    for (int i = 0; i < 40; ++i) b += QByteArray::fromHex("FFFB9000") + QByteArray(413, '\0');
    return b;
}

static void collectPair(void* ctx, const char* k, const char* v)
{
    static_cast<Pairs*>(ctx)->append({QString::fromUtf8(k), QString::fromUtf8(v)});
}

// What FFmpeg makes of the file's tags: a second opinion.
static Pairs ffmpegTags(const QString& path)
{
    Pairs tags;
    char error[256] = {};
    runa_read(QDir::toNativeSeparators(path).toUtf8().constData(), &collectPair, nullptr, &tags, error, sizeof error);
    return tags;
}

static QModelIndex rowOf(const FieldListModel* f, const QString& key)
{
    for (int i = 0; i < f->rowCount(); ++i)
        if (f->data(f->index(i), FieldListModel::KeyRole).toString() == key) return f->index(i);
    return {};
}

// A fingerprint of every packet in every stream (runa_samples --packets).
static QByteArray packetsOf(const QString& path)
{
    QProcess p;
    p.start(SAMPLES_EXE, {"--packets", QDir::toNativeSeparators(path)});
    p.waitForFinished(60000);
    return p.readAllStandardOutput();
}

static bool ffmpegOpens(const QString& path)
{
    char error[256] = {};
    return runa_read(QDir::toNativeSeparators(path).toUtf8().constData(), nullptr, nullptr, nullptr, error, sizeof error) == 0;
}

// Every Ogg page in the file has a good checksum and the next number.
static bool oggPagesValid(const QByteArray& file, int* pages)
{
    static quint32 table[256];
    for (quint32 i = 0; i < 256; ++i) {
        quint32 r = i << 24;
        for (int k = 0; k < 8; ++k) r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : r << 1;
        table[i] = r;
    }
    *pages = 0;
    for (qsizetype pos = 0; pos < file.size(); ++*pages) {
        if (file.mid(pos, 4) != "OggS") return false;
        const int segments = quint8(file[pos + 26]);
        qsizetype body = 0;
        for (int i = 0; i < segments; ++i) body += quint8(file[pos + 27 + i]);
        QByteArray page = file.mid(pos, 27 + segments + body);
        const quint32 stored = qFromLittleEndian<quint32>(page.constData() + 22);
        if (qFromLittleEndian<quint32>(page.constData() + 18) != quint32(*pages)) return false;
        page.replace(22, 4, QByteArray(4, '\0'));
        quint32 crc = 0;
        for (char c : page) crc = (crc << 8) ^ table[((crc >> 24) ^ quint8(c)) & 0xff];
        if (crc != stored) return false;
        pos += page.size();
    }
    return true;
}

// WebP chunks: id -> data.
static QList<QPair<QByteArray, QByteArray>> webpChunks(const QByteArray& file)
{
    QList<QPair<QByteArray, QByteArray>> out;
    for (qsizetype pos = 12; pos + 8 <= file.size();) {
        const quint32 n = qFromLittleEndian<quint32>(file.constData() + pos + 4);
        out.append({file.mid(pos, 4), file.mid(pos + 8, n)});
        pos += 8 + n + (n & 1);
    }
    return out;
}

static QByteArray fileBytes(const QString& path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

static QByteArray entryOf(const QString& path, const QByteArray& name)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const auto a = zip::read(f);
    if (!a || !a->find(name)) return {};
    return zip::extract(f, *a->find(name)).value_or(QByteArray());
}

class TestMetadata : public QObject {
    Q_OBJECT

    QTemporaryDir samples_;
    QTemporaryDir work_;

    QString copy(const QString& name, const QString& as = {})
    {
        const QString to = work_.filePath(as.isEmpty() ? name : as);
        QFile::remove(to);
        QFile::copy(samples_.filePath(name), to);
        QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner);
        return to;
    }

    QImage testImage()
    {
        QImage img(48, 32, QImage::Format_RGB32);
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) img.setPixel(x, y, qRgb(x * 5, y * 7, 120));
        return img;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(samples_.isValid() && work_.isValid());
        QProcess p;
        p.start(SAMPLES_EXE, {samples_.path()});
        QVERIFY(p.waitForFinished(60000));
        QVERIFY2(p.exitCode() == 0, p.readAll().constData());

        QImage img = testImage();
        img.setText("Title", "Old title");
        img.setText("Author", "Someone");
        QVERIFY(img.save(samples_.filePath("sample.png")));
        QVERIFY(testImage().save(samples_.filePath("sample.jpg"), nullptr, 90));
        QFile plain(samples_.filePath("sample.jpg"));
        QVERIFY(plain.open(QIODevice::ReadOnly));
        QFile exif(samples_.filePath("camera.jpg"));
        QVERIFY(exif.open(QIODevice::WriteOnly));
        exif.write(jpegWithExif(plain.readAll()));
        exif.close();

        // A 1x1 lossless WebP with nothing but its image chunk.
        QFile webp(samples_.filePath("sample.webp"));
        QVERIFY(webp.open(QIODevice::WriteOnly));
        webp.write(QByteArray::fromBase64("UklGRhoAAABXRUJQVlA4TA0AAAAvAAAAEAcQERGIiP4HAA=="));
        webp.close();

        // A small Word document and an OpenDocument text (DocSamples.cpp).
        QFile docx(samples_.filePath("sample.docx"));
        QVERIFY(docx.open(QIODevice::WriteOnly));
        docx.write(sampleDocx());
        docx.close();
        QFile odt(samples_.filePath("sample.odt"));
        QVERIFY(odt.open(QIODevice::WriteOnly));
        odt.write(sampleOdt());
        odt.close();

        // A one-page PDF, as Qt writes it.
        {
            QPdfWriter pdf(samples_.filePath("sample.pdf"));
            pdf.setTitle("Sample");
            pdf.setCreator("Someone");
            QPainter p(&pdf);
            p.drawText(200, 200, "Hello");
        }
    }

    // --- Each format, for real -----------------------------------------------

    void media_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<bool>("video");
        QTest::newRow("mp3") << "sample.mp3" << false;
        QTest::newRow("wav") << "sample.wav" << false;
        QTest::newRow("mp4") << "sample.mp4" << true;
        QTest::newRow("mov") << "sample.mov" << true;
        QTest::newRow("mkv") << "sample.mkv" << true;
        QTest::newRow("avi") << "sample.avi" << true;
        QTest::newRow("flac") << "sample.flac" << false;
        QTest::newRow("ogg") << "sample.ogg" << false;
        QTest::newRow("opus") << "sample.opus" << false;
        QTest::newRow("aiff") << "sample.aiff" << false;
        QTest::newRow("m4a") << "sample.m4a" << false;
        QTest::newRow("faststart mp4") << "faststart.mp4" << true;   // moov before the media: offsets move
        QTest::newRow("fragmented mp4") << "fragmented.mp4" << true; // like OBS: fragments move
    }

    void media()
    {
        QFETCH(QString, file);
        QFETCH(bool, video);
        const QString path = copy(file);
        const QByteArray media = packetsOf(path);
        QVERIFY(!media.isEmpty());
        const ReadResult before = meta::read(path);
        QVERIFY2(before.ok, qPrintable(before.error));
        QCOMPARE(tagOf(before.tags, "title"), QString("Sample"));
        QCOMPARE(tagOf(before.tags, "artist"), QString("Someone"));
        QVERIFY(!infoOf(before.info, "Audio").isEmpty());
        if (video) QVERIFY(!infoOf(before.info, "Video").isEmpty());

        const QString title = QString::fromUtf8("Ein Lied über Åse ✓");
        const WriteResult w = meta::write(path, {{"title", title}, {"genre", "Field recording"}, {"artist", ""}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY2(w.notStored.isEmpty(), qPrintable(w.notStored.join(", ")));

        const ReadResult after = meta::read(path);
        QVERIFY(after.ok);
        QCOMPARE(tagOf(after.tags, "title"), title);
        QCOMPARE(tagOf(after.tags, "genre"), QString("Field recording"));
        QVERIFY(!hasTag(after.tags, "artist"));
        // The streams came through untouched. (Codec and size: the sample's
        // frame rate reads differently once its last frame has a duration.)
        QCOMPARE(infoOf(after.info, "Audio"), infoOf(before.info, "Audio"));
        QCOMPARE(infoOf(after.info, "Video").section(',', 0, 1), infoOf(before.info, "Video").section(',', 0, 1));
        QCOMPARE(infoOf(after.info, "Duration"), infoOf(before.info, "Duration"));
        // No temporary file left behind.
        QVERIFY(!QFile::exists(tempPathFor(path)));
        // Every packet of sound and picture is exactly what it was.
        QCOMPARE(packetsOf(path), media);
    }

    // Every field "Add field" offers for a format must really stick in it.
    void everyOfferedFieldSticks_data()
    {
        QTest::addColumn<QString>("file");
        for (const char* f : {"sample.mp3", "sample.wav", "sample.mp4", "sample.mov", "sample.mkv", "sample.avi",
                              "sample.png", "sample.jpg", "sample.flac", "sample.ogg", "sample.opus", "sample.aiff",
                              "sample.m4a", "sample.webp", "sample.docx", "sample.odt", "sample.pdf"})
            QTest::newRow(f) << QString(f);
    }

    void everyOfferedFieldSticks()
    {
        QFETCH(QString, file);
        const QString path = copy(file, "all_" + file);
        QHash<QString, QString> changes;
        for (const QString& k : fieldsFor(path))
            changes.insert(k, k == "date" ? QString("2026-11-20") : k == "track" || k == "disc" ? QString("3") : k == "rating" ? QString("4") : k == "bpm" ? QString("120") : "Value of " + k);
        const WriteResult w = meta::write(path, changes);
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY2(w.notStored.isEmpty(), qPrintable("not kept: " + w.notStored.join(", ")));
    }

    // A WAV keeps every chunk it had: only the INFO list changes. (Suno puts
    // C2PA content credentials after the audio; FFmpeg would drop them.)
    void wavKeepsEveryOtherChunk()
    {
        QFile src(samples_.filePath("sample.wav"));
        QVERIFY(src.open(QIODevice::ReadOnly));
        QByteArray wav = src.readAll();
        const QByteArray extra = QByteArray("C2PA") + QByteArray::fromHex("0D000000") + QByteArray("thirteen byte") + '\0';
        wav += extra;
        char size[4];
        qToLittleEndian<quint32>(quint32(wav.size() - 8), size);
        wav.replace(4, 4, QByteArray(size, 4));
        const QString path = work_.filePath("credentials.wav");
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(wav);
        out.close();

        // The chunks of a WAV, by id.
        auto chunks = [](const QString& p) {
            QFile f(p);
            f.open(QIODevice::ReadOnly);
            const QByteArray b = f.readAll();
            QHash<QByteArray, QByteArray> out;
            for (qsizetype i = 12; i + 8 <= b.size();) {
                const quint32 n = qFromLittleEndian<quint32>(b.constData() + i + 4);
                out.insert(b.mid(i, 4), b.mid(i + 8, n));
                i += 8 + n + (n & 1);
            }
            return out;
        };
        const auto before = chunks(path);

        const WriteResult w = meta::write(path, {{"comment", "One comment for all"}, {"title", ""}, {"keywords", "rock; single"}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY2(w.notStored.isEmpty(), qPrintable(w.notStored.join(", ")));
        QCOMPARE(tagOf(w.after.tags, "comment"), QString("One comment for all"));
        QCOMPARE(tagOf(w.after.tags, "keywords"), QString("rock; single"));
        QCOMPARE(tagOf(w.after.tags, "artist"), QString("Someone"));
        QVERIFY(!hasTag(w.after.tags, "title"));

        const auto after = chunks(path);
        QCOMPARE(after.value("C2PA"), before.value("C2PA"));
        QCOMPARE(after.value("data"), before.value("data"));
        QCOMPARE(after.value("fmt "), before.value("fmt "));
        QFile check(path);
        QVERIFY(check.open(QIODevice::ReadOnly));
        const QByteArray head = check.read(8);
        QCOMPARE(qint64(qFromLittleEndian<quint32>(head.constData() + 4)), check.size() - 8);
    }

    // An MP3 with a busy tag: only the frames for changed fields change.
    void mp3KeepsEveryOtherFrame()
    {
        const QByteArray apic = QByteArray("\0image/jpeg\0\x03\0", 14) + QByteArray(300, 'J');
        const QByteArray priv = QByteArray("WM/MediaClassPrimaryID\0", 23) + QByteArray(16, 'P');
        const QByteArray otherRating = QByteArray("someone@player.example\0", 23) + char(128);
        const QByteArray itunes = QByteArray("\0eng", 4) + QByteArray("iTunNORM\0 000002A3 00000281", 27);
        const QByteArray gain = QByteArray("\0", 1) + QByteArray("REPLAYGAIN_TRACK_GAIN\0-6.50 dB", 30);
        const QByteArray frames = id3Frame(3, "TIT2", QByteArray("\0Old title", 10)) + id3Frame(3, "APIC", apic)
                                + id3Frame(3, "PRIV", priv) + id3Frame(3, "POPM", otherRating)
                                + id3Frame(3, "COMM", itunes) + id3Frame(3, "COMM", QByteArray("\0eng\0old comment", 16))
                                + id3Frame(3, "TXXX", gain) + id3Frame(3, "TCON", QByteArray("\0(17)", 5));
        QByteArray v1("TAG");
        v1 += QByteArray("Old title").leftJustified(30, '\0') + QByteArray(30 + 30 + 4 + 30, '\0') + char(17);
        const QByteArray audio = silentMp3Frames();
        const QString path = work_.filePath("busy.mp3");
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(id3Tag(3, frames) + audio + v1);
        out.close();

        ReadResult r = meta::read(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(tagOf(r.tags, "title"), QString("Old title"));
        QCOMPARE(tagOf(r.tags, "genre"), QString("Rock"));            // "(17)" is Rock
        QCOMPARE(tagOf(r.tags, "rating"), QString("3"));              // another player's 128 of 255
        QCOMPARE(tagOf(r.tags, "comment"), QString("old comment"));   // not the iTunNORM one

        const QString title = QString::fromUtf8("New title \u2713");
        const WriteResult w = meta::write(path, {{"title", title}, {"copyright", "HexKitten"}, {"album_artist", "Einar Blackstone"},
                                                 {"comment", "new comment"}, {"rating", "5"}, {"mood", "Calm"},
                                                 {"studio", "Home"}, {"date", "2026-11-11"}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY2(w.notStored.isEmpty(), qPrintable(w.notStored.join(", ")));
        QCOMPARE(tagOf(w.after.tags, "title"), title);
        QCOMPARE(tagOf(w.after.tags, "copyright"), QString("HexKitten"));
        QCOMPARE(tagOf(w.after.tags, "album_artist"), QString("Einar Blackstone"));
        QCOMPARE(tagOf(w.after.tags, "rating"), QString("5"));        // Windows' own rating now
        QCOMPARE(tagOf(w.after.tags, "mood"), QString("Calm"));
        QCOMPARE(tagOf(w.after.tags, "studio"), QString("Home"));
        QCOMPARE(tagOf(w.after.tags, "date"), QString("2026-11-11"));

        // FFmpeg reads the new tag the same way.
        const Pairs ff = ffmpegTags(path);
        QCOMPARE(tagOf(ff, "title"), title);
        QCOMPARE(tagOf(ff, "copyright"), QString("HexKitten"));
        QCOMPARE(tagOf(ff, "album_artist"), QString("Einar Blackstone"));

        // Everything not edited is byte for byte what it was.
        QFile check(path);
        QVERIFY(check.open(QIODevice::ReadOnly));
        const QByteArray file = check.readAll();
        int version = 0;
        qsizetype end = 0;
        const auto after = id3Frames(file, &version, &end);
        QCOMPARE(version, 3);
        QCOMPARE(after.value("APIC"), apic);
        QCOMPARE(after.value("PRIV"), priv);
        QVERIFY(after.values("POPM").contains(otherRating));
        QVERIFY(after.values("COMM").contains(itunes));
        QVERIFY(after.values("TXXX").contains(gain));
        QCOMPARE(file.mid(end, audio.size()), audio);
        // The old ID3v1 tag at the end follows along.
        QCOMPARE(file.size(), end + audio.size() + 128);
        QVERIFY(file.right(128).startsWith("TAGNew title"));
    }

    void mp3KeepsItsTagVersion()
    {
        const QString path = work_.filePath("v24.mp3");
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(id3Tag(4, id3Frame(4, "TIT2", QByteArray("\x03", 1) + QString::fromUtf8("Gr\u00FC\u00DFe").toUtf8()))
                  + silentMp3Frames());
        out.close();
        QCOMPARE(tagOf(meta::read(path).tags, "title"), QString::fromUtf8("Gr\u00FC\u00DFe"));
        const WriteResult w = meta::write(path, {{"artist", QString::fromUtf8("\u00C5se \u2713")}, {"date", "2026-11-11T20:00"}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY(w.notStored.isEmpty());
        QCOMPARE(tagOf(w.after.tags, "artist"), QString::fromUtf8("\u00C5se \u2713"));
        QCOMPARE(tagOf(w.after.tags, "date"), QString("2026-11-11T20:00"));
        QFile check(path);
        QVERIFY(check.open(QIODevice::ReadOnly));
        int version = 0;
        id3Frames(check.readAll(), &version);
        QCOMPARE(version, 4);
        QCOMPARE(tagOf(ffmpegTags(path), "artist"), QString::fromUtf8("\u00C5se \u2713"));
    }

    // A field nothing selected can hold isn't added; the editor says why.
    void unsupportedFieldsAreRefused()
    {
        MetaEditor e;
        e.addPaths({copy("sample.jpg", "refuse.jpg")});
        QVERIFY(e.waitForLoads(10000));
        const int rows = e.fields()->rowCount();
        e.addField("album");
        QCOMPARE(e.fields()->rowCount(), rows);
        QCOMPARE(e.messageKind(), QString("warning"));
        QVERIFY2(e.message().contains("can't hold"), qPrintable(e.message()));
        QVERIFY(rowOf(e.fields(), "copyright").isValid());   // already listed: it's one a JPG holds
    }

    // A comment so long the Opus header needs many more pages: the audio pages
    // are renumbered, every checksum is right, and the sound is untouched.
    void oggHeaderGrowsByManyPages()
    {
        const QString path = copy("sample.opus", "long.opus");
        const QByteArray media = packetsOf(path);
        int pagesBefore = 0;
        QVERIFY(oggPagesValid(fileBytes(path), &pagesBefore));
        const QString essay = QString("A long comment. ").repeated(6000);
        const WriteResult w = meta::write(path, {{"comment", essay}, {"title", "Long"}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QCOMPARE(tagOf(w.after.tags, "comment"), essay);
        int pagesAfter = 0;
        QVERIFY(oggPagesValid(fileBytes(path), &pagesAfter));
        QVERIFY(pagesAfter > pagesBefore);
        QCOMPARE(packetsOf(path), media);
        QVERIFY(ffmpegOpens(path));
        // And back to short: fewer pages again.
        QVERIFY(meta::write(path, {{"comment", "short"}}).ok);
        int pagesShort = 0;
        QVERIFY(oggPagesValid(fileBytes(path), &pagesShort));
        QCOMPARE(pagesShort, pagesBefore);
        QCOMPARE(packetsOf(path), media);
    }

    // A plain WebP gets the VP8X header its EXIF needs; the image stays as it was.
    void webpGetsItsExtendedHeader()
    {
        const QString path = copy("sample.webp", "pic.webp");
        const auto before = webpChunks(fileBytes(path));
        QCOMPARE(before.size(), 1);
        QCOMPARE(before[0].first, QByteArray("VP8L"));
        const WriteResult w = meta::write(path, {{"title", "Pixel"}, {"rating", "4"}, {"copyright", "HexKitten"}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY(w.notStored.isEmpty());
        QCOMPARE(tagOf(w.after.tags, "title"), QString("Pixel"));
        QCOMPARE(tagOf(w.after.tags, "rating"), QString("4"));
        const auto after = webpChunks(fileBytes(path));
        QCOMPARE(after[0].first, QByteArray("VP8X"));
        QVERIFY(quint8(after[0].second[0]) & 0x08);   // "has EXIF"
        QVERIFY(quint8(after[0].second[0]) & 0x10);   // the image's transparency still declared
        QCOMPARE(after[1], before[0]);                // the image chunk, byte for byte
        QCOMPARE(after.last().first, QByteArray("EXIF"));
        QVERIFY(ffmpegOpens(path));
        // Again: VP8X kept, EXIF updated in place.
        QVERIFY(meta::write(path, {{"title", "Pixel 2"}}).ok);
        QCOMPARE(tagOf(meta::read(path).tags, "title"), QString("Pixel 2"));
        QCOMPARE(webpChunks(fileBytes(path)).size(), 3);
    }

    // A Word document: new properties parts are made and registered; the
    // document itself is copied byte for byte.
    void wordDocumentGetsItsParts()
    {
        const QString path = copy("sample.docx", "report.docx");
        const QByteArray body = entryOf(path, "word/document.xml");
        QVERIFY(!body.isEmpty());
        ReadResult r = meta::read(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(tagOf(r.tags, "title"), QString("Sample"));
        QCOMPARE(tagOf(r.tags, "artist"), QString("Someone"));
        QCOMPARE(infoOf(r.info, "Format"), QString("Word document"));

        const WriteResult w = meta::write(path, {{"title", "Tour notes"}, {"company", "HexKitten"}, {"copyright", "Einar Blackstone"},
                                                 {"category", "Music"}, {"keywords", "tour; notes"}, {"artist", ""}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY2(w.notStored.isEmpty(), qPrintable(w.notStored.join(", ")));
        QCOMPARE(tagOf(w.after.tags, "title"), QString("Tour notes"));
        QCOMPARE(tagOf(w.after.tags, "company"), QString("HexKitten"));
        QCOMPARE(tagOf(w.after.tags, "copyright"), QString("Einar Blackstone"));
        QCOMPARE(tagOf(w.after.tags, "category"), QString("Music"));
        QVERIFY(!hasTag(w.after.tags, "artist"));
        QCOMPARE(entryOf(path, "word/document.xml"), body);
        QVERIFY(entryOf(path, "[Content_Types].xml").contains("custom-properties+xml"));
        QVERIFY(entryOf(path, "[Content_Types].xml").contains("extended-properties+xml"));
        QVERIFY(entryOf(path, "_rels/.rels").contains("relationships/custom-properties"));
        QVERIFY(entryOf(path, "docProps/custom.xml").contains("name=\"Copyright\""));
        QVERIFY(entryOf(path, "docProps/core.xml").contains("dcterms:created"));   // what wasn't touched stays
    }

    void openDocumentKeepsMimetypeFirst()
    {
        const QString path = copy("sample.odt", "letter.odt");
        const QByteArray content = entryOf(path, "content.xml");
        const WriteResult w = meta::write(path, {{"keywords", "rock; single"}, {"copyright", "Einar Blackstone"}, {"subject", "Liner notes"}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY(w.notStored.isEmpty());
        QCOMPARE(tagOf(w.after.tags, "keywords"), QString("rock; single"));
        QCOMPARE(tagOf(w.after.tags, "copyright"), QString("Einar Blackstone"));
        QCOMPARE(tagOf(w.after.tags, "title"), QString("Sample"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto a = zip::read(f);
        QVERIFY(a);
        QCOMPARE(a->entries.first().name, QByteArray("mimetype"));
        QCOMPARE(a->entries.first().method, quint16(0));
        QCOMPARE(entryOf(path, "content.xml"), content);
    }

    void pdfProperties()
    {
        const QString path = copy("sample.pdf", "liner.pdf");
        ReadResult r = meta::read(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(tagOf(r.tags, "title"), QString("Sample"));
        QCOMPARE(infoOf(r.info, "Pages"), QString("1"));
        const QString title = QString::fromUtf8("Liner notes \u2713");
        const WriteResult w = meta::write(path, {{"title", title}, {"artist", "Einar Blackstone"}, {"copyright", "HexKitten"},
                                                 {"keywords", "liner, notes"}, {"studio", "Home"}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY2(w.notStored.isEmpty(), qPrintable(w.notStored.join(", ")));
        QCOMPARE(tagOf(w.after.tags, "title"), title);
        QCOMPARE(tagOf(w.after.tags, "artist"), QString("Einar Blackstone"));
        QCOMPARE(tagOf(w.after.tags, "copyright"), QString("HexKitten"));
        QCOMPARE(tagOf(w.after.tags, "studio"), QString("Home"));
        QCOMPARE(infoOf(w.after.info, "Pages"), QString("1"));
    }

    // Windows reads a FLAC's comment from DESCRIPTION; a comment goes there too,
    // unless the description says something of its own.
    void flacCommentGoesWhereWindowsLooks()
    {
        const QString path = copy("sample.flac", "comment.flac");
        WriteResult w = meta::write(path, {{"comment", "Mixed at home"}});
        QVERIFY(w.ok);
        QCOMPARE(tagOf(w.after.tags, "comment"), QString("Mixed at home"));
        QCOMPARE(tagOf(w.after.tags, "description"), QString("Mixed at home"));
        w = meta::write(path, {{"description", "A song about leaving"}});
        QVERIFY(w.ok);
        w = meta::write(path, {{"comment", "Remastered"}});   // the description has its own text now: left alone
        QCOMPARE(tagOf(w.after.tags, "comment"), QString("Remastered"));
        QCOMPARE(tagOf(w.after.tags, "description"), QString("A song about leaving"));
    }

    void wavSaysWhatItCantHold()
    {
        const QString path = copy("sample.wav", "limits.wav");
        const WriteResult w = meta::write(path, {{"album_artist", "Nobody"}, {"title", "Kept"}});
        QVERIFY(w.ok);
        QCOMPARE(w.notStored, QStringList{"album_artist"});
        QCOMPARE(tagOf(w.after.tags, "title"), QString("Kept"));
    }

    void png()
    {
        const QString path = copy("sample.png");
        const ReadResult before = meta::read(path);
        QVERIFY2(before.ok, qPrintable(before.error));
        QCOMPARE(tagOf(before.tags, "title"), QString("Old title"));
        QCOMPARE(tagOf(before.tags, "artist"), QString("Someone"));
        QCOMPARE(infoOf(before.info, "Dimensions"), QString("48 × 32"));

        const QString title = QString::fromUtf8("Neuer Titel ✓");
        const WriteResult w = meta::write(path, {{"title", title}, {"comment", "two\nlines"}, {"artist", ""}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY(w.notStored.isEmpty());
        QCOMPARE(tagOf(w.after.tags, "title"), title);
        QCOMPARE(tagOf(w.after.tags, "comment"), QString("two\nlines"));
        QVERIFY(!hasTag(w.after.tags, "artist"));
        // Other programs see it too, and the picture is the same.
        QImageReader reader(path);
        QCOMPARE(reader.text("Title"), title);
        QCOMPARE(QImage(path).convertToFormat(QImage::Format_RGB32), testImage());
    }

    void jpegWithoutExif()
    {
        const QString path = copy("sample.jpg");
        const QImage pixels = QImage(path);
        QVERIFY(meta::read(path).tags.isEmpty());
        const QString title = QString::fromUtf8("Schönes Bild");
        const WriteResult w = meta::write(path, {{"title", title}, {"artist", "Me"}, {"keywords", "sea; sky"},
                                                 {"description", "A test"}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY2(w.notStored.isEmpty(), qPrintable(w.notStored.join(", ")));
        QCOMPARE(tagOf(w.after.tags, "title"), title);
        QCOMPARE(tagOf(w.after.tags, "artist"), QString("Me"));
        QCOMPARE(tagOf(w.after.tags, "keywords"), QString("sea; sky"));
        QCOMPARE(tagOf(w.after.tags, "description"), QString("A test"));
        QCOMPARE(QImage(path), pixels);   // the image data wasn't touched
        // JPEG has no album.
        const WriteResult w2 = meta::write(path, {{"album", "X"}});
        QVERIFY(w2.ok);
        QCOMPARE(w2.notStored, QStringList{"album"});
    }

    void jpegKeepsTheCameraData()
    {
        const QString path = copy("camera.jpg");
        const QImage pixels = QImage(path);
        QVERIFY(!pixels.isNull());
        ReadResult r = meta::read(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(tagOf(r.tags, "make"), QString("Canon"));
        QCOMPARE(infoOf(r.info, "Taken"), QString("2024:05:06 07:08:09"));

        WriteResult w = meta::write(path, {{"title", "First"}, {"copyright", "(c) Me"}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QCOMPARE(tagOf(w.after.tags, "title"), QString("First"));
        QCOMPARE(tagOf(w.after.tags, "make"), QString("Canon"));
        QCOMPARE(infoOf(w.after.info, "Taken"), QString("2024:05:06 07:08:09"));

        // Again, over the IFD0 written the first time.
        w = meta::write(path, {{"title", "Second"}, {"make", ""}});
        QVERIFY(w.ok);
        QCOMPARE(tagOf(w.after.tags, "title"), QString("Second"));
        QCOMPARE(tagOf(w.after.tags, "copyright"), QString("(c) Me"));
        QVERIFY(!hasTag(w.after.tags, "make"));
        QCOMPARE(infoOf(w.after.info, "Taken"), QString("2024:05:06 07:08:09"));
        QCOMPARE(QImage(path), pixels);
    }

    // The packet Photoshop puts in a PNG shows as fields, not as a wall of XML,
    // and edits reach it too.
    void xmpInAPng()
    {
        QImage img = testImage();
        img.setText("XML:com.adobe.xmp", QString::fromUtf8(photoshopXmp()));
        const QString path = work_.filePath("photoshop.png");
        QVERIFY(img.save(path));

        const ReadResult r = meta::read(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        for (const auto& [k, v] : r.tags) QVERIFY2(!k.startsWith("XML:"), qPrintable(k));
        QCOMPARE(tagOf(r.tags, "encoder"), QString("Adobe Photoshop 27.9 (Windows)"));
        QCOMPARE(infoOf(r.info, "Created"), QString("2026-10-16 21:20"));
        QCOMPARE(infoOf(r.info, "Modified"), QString("2026-11-03 07:04"));

        const WriteResult w = meta::write(path, {{"title", "Poster"}, {"artist", "Ann; Bo"}, {"encoder", ""}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY2(w.notStored.isEmpty(), qPrintable(w.notStored.join(", ")));
        QCOMPARE(tagOf(w.after.tags, "title"), QString("Poster"));
        QCOMPARE(tagOf(w.after.tags, "artist"), QString("Ann; Bo"));
        QVERIFY(!hasTag(w.after.tags, "encoder"));

        // What Photoshop will read: the new values, the rest of the packet kept.
        // (The XMP chunk is stored uncompressed, so its text is in the file as is.)
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray packet = file.readAll();
        QVERIFY(packet.contains(R"(<rdf:li xml:lang="x-default">Poster</rdf:li>)"));
        QVERIFY(packet.contains("<rdf:li>Ann</rdf:li>"));
        QVERIFY(packet.contains("<rdf:li>Bo</rdf:li>"));
        QVERIFY(!packet.contains("CreatorTool"));
        QVERIFY(packet.contains("xmpMM:History"));
        QVERIFY(packet.contains("xmp:CreateDate"));
        QCOMPARE(QImage(path).convertToFormat(QImage::Format_RGB32), testImage());
    }

    void xmpInAJpeg()
    {
        QFile plain(samples_.filePath("sample.jpg"));
        QVERIFY(plain.open(QIODevice::ReadOnly));
        const QByteArray jpeg = plain.readAll();
        const QString path = work_.filePath("lightroom.jpg");
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(jpeg.left(2) + app1(QByteArray("http://ns.adobe.com/xap/1.0/", 28) + '\0' + kNestedXmp) + jpeg.mid(2));
        out.close();

        const ReadResult r = meta::read(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(tagOf(r.tags, "title"), QString("Old XMP title"));
        QCOMPARE(tagOf(r.tags, "artist"), QString("Ann; Bo"));

        const WriteResult w = meta::write(path, {{"title", "Fresh"}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QCOMPARE(tagOf(w.after.tags, "title"), QString("Fresh"));
        QCOMPARE(tagOf(w.after.tags, "artist"), QString("Ann; Bo"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QVERIFY(!bytes.contains("Old XMP title"));   // the XMP copy changed too
        QVERIFY(bytes.contains(R"(<rdf:li xml:lang="x-default">Fresh</rdf:li>)"));
        QCOMPARE(QImage(path), QImage(samples_.filePath("sample.jpg")));
    }

    // --- The editor ----------------------------------------------------------

    void editingOneFile()
    {
        MetaEditor e;
        e.addPaths({copy("sample.mp3", "one.mp3")});
        QVERIFY(e.waitForLoads(10000));
        QCOMPARE(e.selectedCount(), 1);   // the first file is shown straight away
        // Every field an MP3 holds is listed, filled in or not.
        const FieldListModel* f = e.fields();
        QCOMPARE(f->rowCount(), fieldsFor("x.mp3").size());
        QCOMPARE(f->data(rowOf(f, "title"), FieldListModel::ValueRole).toString(), QString("Sample"));
        QCOMPARE(f->data(rowOf(f, "copyright"), FieldListModel::ValueRole).toString(), QString());
        e.setField("title", "Changed");
        QCOMPARE(e.modifiedCount(), 1);
        e.setField("title", "Sample");          // back to what it was: no change
        QCOMPARE(e.modifiedCount(), 0);
        e.setField("genre", "Ambient");
        e.removeField("artist");
        QVERIFY(rowOf(f, "artist").isValid());   // the row stays, empty, as a change
        QCOMPARE(f->data(rowOf(f, "artist"), FieldListModel::ValueRole).toString(), QString());
        QVERIFY(f->data(rowOf(f, "artist"), FieldListModel::EditedRole).toBool());
        e.addField("studio");                   // a custom one: an MP3 can hold it
        QVERIFY(rowOf(f, "studio").isValid());
        QCOMPARE(tagOf(meta::read(e.list()[0].path).tags, "genre"), QString());   // nothing written yet
        e.apply();
        QVERIFY(e.waitForApply(20000));
        const ReadResult r = meta::read(e.list()[0].path);
        QCOMPARE(tagOf(r.tags, "genre"), QString("Ambient"));
        QVERIFY(!hasTag(r.tags, "artist"));
        QCOMPARE(e.modifiedCount(), 0);
        QCOMPARE(e.messageKind(), QString("success"));
    }

    void bulkEditing()
    {
        MetaEditor e;
        const QStringList paths{copy("sample.mp3", "bulk.mp3"), copy("sample.png", "bulk.png"), copy("sample.mp4", "bulk.mp4")};
        e.addPaths(paths);
        QVERIFY(e.waitForLoads(10000));
        QCOMPARE(e.count(), 3);
        e.selectAll();
        QCOMPARE(e.selectedCount(), 3);
        // Every field the three formats hold. The titles differ; the artist doesn't.
        const FieldListModel* f = e.fields();
        const QModelIndex title = f->index(0);
        QCOMPARE(f->data(title, FieldListModel::KeyRole).toString(), QString("title"));
        QVERIFY(f->data(title, FieldListModel::MixedRole).toBool());
        QVERIFY(rowOf(f, "encoder").isValid());
        QVERIFY(rowOf(f, "comment").isValid());
        const QModelIndex artist = rowOf(f, "artist");
        QCOMPARE(f->data(artist, FieldListModel::ValueRole).toString(), QString("Someone"));
        QVERIFY(!f->data(artist, FieldListModel::MixedRole).toBool());

        e.setField("title", "Same for all");
        QCOMPARE(e.modifiedCount(), 3);
        QVERIFY(!f->data(f->index(0), FieldListModel::MixedRole).toBool());
        for (const QString& p : paths) QVERIFY(tagOf(meta::read(p).tags, "title") != "Same for all");   // not yet

        // Discard puts it all back, with the files untouched.
        e.revertAll();
        QCOMPARE(e.modifiedCount(), 0);
        QVERIFY(f->data(f->index(0), FieldListModel::MixedRole).toBool());

        e.setField("title", "Same for all");
        e.setField("comment", "Batch");
        e.apply();
        QVERIFY(e.waitForApply(30000));
        for (const QString& p : paths) {
            const ReadResult r = meta::read(p);
            QCOMPARE(tagOf(r.tags, "title"), QString("Same for all"));
            QCOMPARE(tagOf(r.tags, "comment"), QString("Batch"));
        }

        // Shift and Ctrl clicks.
        e.click(0, 0);
        QCOMPARE(e.selectedCount(), 1);
        e.click(2, Qt::ShiftModifier);
        QCOMPARE(e.selectedCount(), 3);
        e.click(1, Qt::ControlModifier);
        QCOMPARE(e.selectedCount(), 2);
        e.removeSelected();
        QCOMPARE(e.count(), 1);
    }

    // WAVs and a photo together: every field any of them has is shown, the
    // differing ones say so, and each change goes only where it fits.
    void mixedKindsGetOnlyTheirFields()
    {
        const QString a = copy("sample.wav", "mix_a.wav"), b = copy("sample.wav", "mix_b.wav");
        const QString bare = work_.filePath("mix_bare.wav");
        QVERIFY(meta::write(a, {{"comment", "first take"}, {"album", "Single"}}).ok);
        QVERIFY(meta::write(b, {{"comment", "second take"}}).ok);
        QVERIFY(QFile::copy(b, bare));
        QFile::setPermissions(bare, QFile::ReadOwner | QFile::WriteOwner);
        QVERIFY(meta::write(bare, {{"comment", ""}, {"title", ""}, {"artist", ""}}).ok);   // no tags at all
        const QString photo = copy("camera.jpg", "mix_photo.jpg");

        MetaEditor e;
        e.addPaths({a, b, bare, photo});
        QVERIFY(e.waitForLoads(10000));
        e.selectAll();
        const FieldListModel* f = e.fields();
        auto row = [&](const QString& key) {
            for (int i = 0; i < f->rowCount(); ++i)
                if (f->data(f->index(i), FieldListModel::KeyRole).toString() == key) return f->index(i);
            return QModelIndex();
        };
        // The bare WAV doesn't hide the others' fields.
        QVERIFY(row("comment").isValid());
        QVERIFY(f->data(row("comment"), FieldListModel::MixedRole).toBool());
        QCOMPARE(f->data(row("comment"), FieldListModel::AppliesToRole).toInt(), 4);   // a photo can have a comment
        QVERIFY(row("album").isValid());
        QCOMPARE(f->data(row("album"), FieldListModel::AppliesToRole).toInt(), 3);     // but not an album
        QCOMPARE(f->data(row("album"), FieldListModel::TotalRole).toInt(), 4);
        QVERIFY(row("make").isValid());                                                // the camera's, from the photo
        QCOMPARE(f->data(row("make"), FieldListModel::AppliesToRole).toInt(), 1);

        // "Add field" offers what any of them can hold; rating is the photo's.
        QVERIFY(row("rating").isValid());
        QCOMPARE(f->data(row("rating"), FieldListModel::AppliesToRole).toInt(), 1);

        e.setField("comment", "Same everywhere");
        e.setField("album", "Who Wants Forever");
        e.addField("rating");
        e.setField("rating", "4");
        QCOMPARE(e.modifiedCount(), 4);
        e.apply();
        QVERIFY(e.waitForApply(30000));
        QCOMPARE(e.messageKind(), QString("success"));   // nothing refused: nothing was sent where it doesn't fit

        for (const QString& p : {a, b, bare}) {
            const ReadResult r = meta::read(p);
            QCOMPARE(tagOf(r.tags, "comment"), QString("Same everywhere"));
            QCOMPARE(tagOf(r.tags, "album"), QString("Who Wants Forever"));
            QVERIFY(!hasTag(r.tags, "rating"));
        }
        const ReadResult r = meta::read(photo);
        QCOMPARE(tagOf(r.tags, "comment"), QString("Same everywhere"));
        QCOMPARE(tagOf(r.tags, "rating"), QString("4"));
        QVERIFY(!hasTag(r.tags, "album"));
        QCOMPARE(tagOf(r.tags, "make"), QString("Canon"));
    }

    void aFailedSaveKeepsTheChanges()
    {
        MetaEditor e;
        const QString path = copy("sample.mp3", "locked.mp3");
        e.addPaths({path});
        QVERIFY(e.waitForLoads(10000));
        e.setField("title", "Never");
        QFile::setPermissions(path, QFile::ReadOwner);   // read-only
        e.apply();
        QVERIFY(e.waitForApply(10000));
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        QCOMPARE(e.modifiedCount(), 1);
        QVERIFY(!e.list()[0].error.isEmpty());
        QCOMPARE(e.messageKind(), QString("error"));
    }

    void addingAFolderFindsTheFiles()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("sub");
        QFile::copy(samples_.filePath("sample.wav"), dir.filePath("sub/a.wav"));
        QFile::copy(samples_.filePath("sample.png"), dir.filePath("b.png"));
        QVERIFY(QFile(dir.filePath("notes.txt")).open(QIODevice::WriteOnly));
        MetaEditor e;
        e.addUrls({QUrl::fromLocalFile(dir.path())});
        QCOMPARE(e.count(), 2);
        e.addUrls({QUrl::fromLocalFile(dir.filePath("b.png"))});   // already there
        QCOMPARE(e.count(), 2);
        QVERIFY(e.waitForLoads(10000));
    }

    // --- The page --------------------------------------------------------------

    // --- Content Credentials ---------------------------------------------------

    void credentials_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("home");
        QTest::addColumn<bool>("split");
        QTest::newRow("png") << "sample.png" << int(C2paHome::Png) << false;
        QTest::newRow("jpg") << "camera.jpg" << int(C2paHome::Jpeg) << false;
        QTest::newRow("jpg in three segments") << "sample.jpg" << int(C2paHome::Jpeg) << true;
        QTest::newRow("webp") << "sample.webp" << int(C2paHome::Webp) << false;
        QTest::newRow("wav") << "sample.wav" << int(C2paHome::Wav) << false;
        QTest::newRow("mp3") << "sample.mp3" << int(C2paHome::Mp3) << false;
        QTest::newRow("mp4") << "sample.mp4" << int(C2paHome::Mp4) << false;
        QTest::newRow("faststart mp4") << "faststart.mp4" << int(C2paHome::Mp4) << false;
        QTest::newRow("m4a") << "sample.m4a" << int(C2paHome::Mp4) << false;
    }

    // Read from where each format keeps them, still there after editing other
    // fields (no longer matching the file), and removed without touching
    // anything else.
    void credentials()
    {
        QFETCH(QString, file);
        QFETCH(int, home);
        QFETCH(bool, split);
        const C2paHome where = C2paHome(home);
        const bool mp4 = where == C2paHome::Mp4;
        const bool image = where == C2paHome::Png || where == C2paHome::Jpeg || where == C2paHome::Webp;
        const QString plain = samples_.filePath(file);
        const QByteArray withCr = withCredentials(fileBytes(plain), where, split);
        QVERIFY(!withCr.isEmpty());
        const QString path = work_.filePath("cr_" + QString(QTest::currentDataTag()).replace(' ', '_') + "." + QFileInfo(file).suffix());
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(withCr);
        }

        // The same picture or sound as before, whatever is in it.
        auto sameContent = [&] {
            if (image) return QImage(plain).isNull() || QImage(path) == QImage(plain);
            return packetsOf(path) == packetsOf(plain);
        };
        QVERIFY(sameContent());

        const ReadResult r = meta::read(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        const QVariantMap c = r.credentials;
        QVERIFY(c.value("present").toBool());
        QVERIFY(c.value("ai").toBool());
        QCOMPARE(c.value("source").toString(), QString("Made by AI"));
        QCOMPARE(c.value("tool").toString(), QString("Test Generator 2.0"));
        QCOMPARE(c.value("recordedBy").toString(), QString("Test Maker 1.0"));
        QVERIFY(c.value("actions").toStringList().contains("Created with Test Generator 2.0"));
        QCOMPARE(c.value("history").toInt(), 0);
        QCOMPARE(c.value("match").toString(), mp4 ? QString() : QString("unchanged"));
        QCOMPARE(c.value("notes").toStringList().size(), 1);   // the hidden watermark
        QVERIFY(!c.value("when").toString().isEmpty());

        // Other fields edited: the record stays, saying what it said.
        WriteResult w = meta::write(path, {{"copyright", "2026 Einar Blackstone"}});
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY(w.after.credentials.value("present").toBool());
        QCOMPARE(w.after.credentials.value("tool").toString(), QString("Test Generator 2.0"));
        if (!mp4) QCOMPARE(w.after.credentials.value("match").toString(), QString("changed"));

        // Removed: the record goes, nothing else does.
        w = meta::write(path, {}, true);
        QVERIFY2(w.ok, qPrintable(w.error));
        QVERIFY(w.after.credentials.isEmpty());
        QCOMPARE(tagOf(w.after.tags, "copyright"), QString("2026 Einar Blackstone"));
        QVERIFY(sameContent());
        const QByteArray after = fileBytes(path);
        QVERIFY(!after.contains("c2pa.claim"));
        if (where == C2paHome::Wav || where == C2paHome::Webp)
            QCOMPARE(qint64(qFromLittleEndian<quint32>(after.constData() + 4)), qint64(after.size()) - 8);
        if (mp4) QVERIFY(ffmpegOpens(path));
        if (where == C2paHome::Mp3) QVERIFY(!id3Frames(after).contains("GEOB"));
        // Nothing left: removing again is fine and changes nothing.
        QVERIFY(meta::write(path, {}, true).ok);
        QCOMPARE(fileBytes(path), after);
    }

    // A damaged record is reported, never a crash, and can still be removed.
    void damagedCredentials()
    {
        const QByteArray png = fileBytes(samples_.filePath("sample.png"));
        const QString path = work_.filePath("damaged_cr.png");
        auto save = [&](const QByteArray& b) {
            QFile f(path);
            return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(b) == b.size();
        };
        QVERIFY(save(withStore(png, C2paHome::Png, damagedStore())));
        const ReadResult r = meta::read(path);
        QVERIFY(r.ok);
        QVERIFY(r.credentials.value("present").toBool());
        QCOMPARE(r.credentials.value("notes").toStringList().size(), 1);
        QVERIFY(meta::write(path, {}, true).ok);
        QVERIFY(meta::read(path).credentials.isEmpty());
        QCOMPARE(QImage(path), QImage(samples_.filePath("sample.png")));

        // Every truncation, and a byte spoiled at a time all through a good one.
        const QByteArray cut = damagedStore();
        for (qsizetype n = 0; n < cut.size(); n += 5) {
            QVERIFY(save(withStore(png, C2paHome::Png, cut.left(n))));
            meta::read(path);
        }
        const QByteArray good = withCredentials(png, C2paHome::Png);
        const qsizetype to = good.size() - (png.size() - 33);
        for (qsizetype i = 33; i < to; i += 3) {
            QByteArray spoiled = good;
            spoiled[i] = char(spoiled[i] ^ 0x5A);
            QVERIFY(save(spoiled));
            meta::read(path);
        }
    }

    // In the editor: marked in the list, removed on Apply along with other
    // edits, and Keep takes the removal back.
    void editorRemovesCredentials()
    {
        auto make = [&](const QString& sample, C2paHome home, const QString& as) {
            const QString to = work_.filePath(as);
            QFile f(to);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(withCredentials(fileBytes(samples_.filePath(sample)), home));
            return to;
        };
        const QStringList paths{make("sample.png", C2paHome::Png, "ed_a.png"), make("sample.mp3", C2paHome::Mp3, "ed_b.mp3"),
                                copy("sample.wav", "ed_c.wav")};
        MetaEditor e;
        e.addPaths(paths);
        QVERIFY(e.waitForLoads(10000));
        QCOMPARE(e.files()->data(e.files()->index(0), FileListModel::CredentialsRole).toString(), QString("ai"));
        QCOMPARE(e.files()->data(e.files()->index(2), FileListModel::CredentialsRole).toString(), QString());
        QVERIFY(e.current().value("credentials").toMap().value("present").toBool());   // the first file, shown

        e.selectAll();
        QCOMPARE(e.credentialsCount(), 2);
        e.removeCredentials();
        QCOMPARE(e.credentialsDropping(), 2);
        QCOMPARE(e.modifiedCount(), 2);   // the WAV has none to remove
        QVERIFY(meta::read(paths[0]).credentials.value("present").toBool());   // nothing written yet
        e.keepCredentials();
        QCOMPARE(e.modifiedCount(), 0);

        e.removeCredentials();
        e.setField("copyright", "2026 HexKitten");
        e.apply();
        QVERIFY(e.waitForApply(20000));
        QCOMPARE(e.messageKind(), QString("success"));
        QCOMPARE(e.modifiedCount(), 0);
        QCOMPARE(e.credentialsCount(), 0);
        for (const QString& p : paths) {
            const ReadResult r = meta::read(p);
            QVERIFY(r.credentials.isEmpty());
            QCOMPARE(tagOf(r.tags, "copyright"), QString("2026 HexKitten"));
        }
    }

    void credentialsOnThePage()
    {
        const QString path = work_.filePath("page_cr.png");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(withCredentials(fileBytes(samples_.filePath("sample.png")), C2paHome::Png));
        }
        MetaEditor e;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor", &e);
        engine.loadFromModule("LokiMetadata", "Main");
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window);
        window->resize(1240, 1000);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        e.addPaths({path, copy("sample.wav", "page_cr.wav")});
        QVERIFY(e.waitForLoads(10000));
        e.click(0, 0);
        QTest::qWait(150);
        auto find = [&](const QString& name) {
            QQuickItem* found = nullptr;
            std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
                if (i->objectName() == name && i->isVisible()) found = i;
                for (QQuickItem* c : i->childItems()) walk(c);
            };
            walk(window->contentItem());
            return found;
        };
        const QString shots = qEnvironmentVariable("LOKI_UI_SHOTS");
        auto shot = [&](const QString& name) {
            if (shots.isEmpty()) return;
            QTest::qWait(150);
            window->grabWindow().save(QDir(shots).filePath(name + ".png"));
        };
        QQuickItem* button = find("credentialsButton");
        QVERIFY(button);
        shot("6_credentials");
        QMetaObject::invokeMethod(button, "clicked");
        QCOMPARE(e.credentialsDropping(), 1);
        QCOMPARE(e.modifiedCount(), 1);
        shot("6b_credentials_removing");
        e.click(1, 0);   // the WAV: no record, no section
        QTest::qWait(100);
        QVERIFY(!find("credentialsButton"));
        e.selectAll();
        QTest::qWait(100);
        QVERIFY(find("credentialsButton"));
        shot("6c_credentials_several");
    }

    void thePage()
    {
        MetaEditor e;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor", &e);
        engine.loadFromModule("LokiMetadata", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1240, 780);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(window->title(), QString("Loki Metadata " LOKI_METADATA_VERSION));   // the version in the title bar
        const QString shots = qEnvironmentVariable("LOKI_UI_SHOTS");
        auto shot = [&](const QString& name) {
            if (shots.isEmpty()) return;
            QTest::qWait(150);
            window->grabWindow().save(QDir(shots).filePath(name + ".png"));
        };
        shot("1_empty");

        e.addPaths({copy("sample.mp3", "page.mp3"), copy("sample.png", "page.png"), copy("camera.jpg", "page.jpg"),
                    copy("sample.mp4", "page.mp4")});
        QVERIFY(e.waitForLoads(10000));
        QTest::qWait(100);
        shot("2_one_file");

        auto find = [&](const QString& name) {
            QQuickItem* found = nullptr;
            std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
                if (i->objectName() == name && i->isVisible()) found = i;
                for (QQuickItem* c : i->childItems()) walk(c);
            };
            walk(window->contentItem());
            return found;
        };

        // The list is sorted: page.jpg, page.mp3, page.mp4, page.png. The last
        // three share title and artist; select them and type a new title.
        e.click(1, 0);
        e.click(3, Qt::ShiftModifier);
        QCOMPARE(e.selectedCount(), 3);
        QTest::qWait(100);
        QQuickItem* title = find("field_title");
        QVERIFY(title);
        QCOMPARE(title->property("placeholderText").toString(), QString("Different values"));
        shot("3a_mixed");
        QQuickItem* add = find("customFieldButton");
        QVERIFY(add);
        QMetaObject::invokeMethod(add, "clicked");
        shot("3b_custom_field");
        QTest::keyClick(window, Qt::Key_Escape);
        title->forceActiveFocus();
        QCOMPARE(title->property("placeholderText").toString(), QString());   // gone once you click in
        for (char c : QByteArray("Road trip")) QTest::keyClick(window, c);
        QCOMPARE(title->property("text").toString(), QString("Road trip"));   // focus stayed while typing
        QCOMPARE(e.modifiedCount(), 3);
        for (const MetaFile& f : e.list())
            if (f.selected) QCOMPARE(f.pending.value("title").value_or(QString()), QString("Road trip"));
        shot("3_bulk_edit");

        // Everything, photo included, with a rating to set.
        e.selectAll();
        e.addField("rating");
        QTest::qWait(100);
        shot("3c_everything");

        // One photo: its preview, EXIF fields and camera info.
        e.click(0, 0);
        QTest::qWait(300);
        shot("4_photo");

        // Apply from the button.
        QQuickItem* apply = find("applyButton");
        QVERIFY(apply && apply->isEnabled());
        QMetaObject::invokeMethod(apply, "clicked");
        QVERIFY(e.waitForApply(30000));
        QCOMPARE(e.modifiedCount(), 0);
        e.click(1, 0);
        QTest::qWait(100);
        shot("5_after_apply");
        QCOMPARE(tagOf(meta::read(e.list()[1].path).tags, "title"), QString("Road trip"));
    }

    // Colours belong to the theme: no other QML file may name one.
    void coloursOnlyInTheTheme()
    {
        const QRegularExpression colour(R"re(#[0-9a-fA-F]{3,8}\b|Qt\.(rgba|hsla|hsva|lighter|darker|tint)\(|"(white|black|red|green|blue|gray|grey|yellow|orange|purple)")re");
        QStringList offenders;
        QDirIterator it(QString(SOURCE_DIR) + "/app/qml", {"*.qml"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.contains("/qml/theme/")) continue;
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            int line = 0;
            while (!f.atEnd()) {
                ++line;
                const QString text = QString::fromUtf8(f.readLine());
                if (colour.match(text).hasMatch()) offenders.append(QString("%1:%2: %3").arg(path).arg(line).arg(text.trimmed()));
            }
        }
        QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join("\n")));
    }
};

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR")) qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM_PLUGIN_PATH")) qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QT_PLATFORMS_DIR);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    app.setApplicationVersion(LOKI_METADATA_VERSION);
    QQuickStyle::setStyle("Basic");
    TestMetadata t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_metadata.moc"
