#include "MetaIO.h"
#include "C2pa.h"

#include <runa.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>

#include <atomic>

namespace meta {

namespace {

struct Collected {
    Pairs tags, info;
};

void collectTag(void* ctx, const char* key, const char* value)
{
    static_cast<Collected*>(ctx)->tags.append({QString::fromUtf8(key), QString::fromUtf8(value)});
}

void collectInfo(void* ctx, const char* key, const char* value)
{
    static_cast<Collected*>(ctx)->info.append({QString::fromUtf8(key), QString::fromUtf8(value)});
}

ReadResult readMedia(const QString& path)
{
    Collected c;
    char error[512] = {};
    const QByteArray p = QDir::toNativeSeparators(path).toUtf8();
    ReadResult r;
    if (runa_read(p.constData(), &collectTag, &collectInfo, &c, error, sizeof error) != 0) {
        r.error = QString::fromUtf8(error);
        return r;
    }
    for (const auto& [key, value] : c.tags) {
        if (isTechnicalTag(key)) {
            c.info.append({key, value});
            continue;
        }
        // Matroska keeps tag names in capitals (ARTIST); FFmpeg matches them
        // either way, so a named field is shown by its usual lower-case key.
        // A WAV's INFO IDs that FFmpeg passes through as they are.
        static const QHash<QString, QString> infoIds{{"IKEY", "keywords"}, {"ISBJ", "subject"}};
        const QString lower = key.toLower();
        r.tags.append({infoIds.contains(key) ? infoIds.value(key) : fieldOrder(lower) < knownFields().size() ? lower : key,
                       value});
    }
    r.info = c.info;
    r.ok = true;
    return r;
}

QString writeMedia(const QString& path, const QHash<QString, QString>& changes)
{
    QList<QByteArray> keys, values;
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        keys.append(it.key().toUtf8());
        values.append(it.value().toUtf8());
    }
    QList<const char*> k, v;
    for (int i = 0; i < keys.size(); ++i) {
        k.append(keys[i].constData());
        v.append(values[i].constData());
    }
    char error[512] = {};
    const QByteArray p = QDir::toNativeSeparators(path).toUtf8();
    if (runa_write(p.constData(), k.constData(), v.constData(), int(k.size()), error, sizeof error) != 0)
        return QString::fromUtf8(error);
    return {};
}

bool is(const QString& path, std::initializer_list<const char*> exts)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    for (const char* e : exts)
        if (ext == e) return true;
    return false;
}
bool isPng(const QString& p) { return is(p, {"png"}); }
bool isJpeg(const QString& p) { return is(p, {"jpg", "jpeg"}); }
bool isWebp(const QString& p) { return is(p, {"webp"}); }
bool isOffice(const QString& p) { return is(p, {"docx", "xlsx", "pptx", "odt", "ods", "odp"}); }
bool isPdf(const QString& p) { return is(p, {"pdf"}); }
bool isMp3(const QString& p) { return is(p, {"mp3"}); }
bool isWav(const QString& p) { return is(p, {"wav"}); }
bool isFlac(const QString& p) { return is(p, {"flac"}); }
bool isOgg(const QString& p) { return is(p, {"ogg", "oga", "opus"}); }
bool isAiff(const QString& p) { return is(p, {"aif", "aiff", "aifc"}); }
bool isMp4(const QString& p) { return is(p, {"mp4", "m4a", "m4v", "mov"}); }

// The tags of formats read directly, when the file is one of them.
std::optional<Pairs> nativeTags(const QString& p)
{
    if (isMp3(p)) return readMp3Tags(p);
    if (isFlac(p)) return readFlacTags(p);
    if (isOgg(p)) return readOggTags(p);
    if (isAiff(p)) return readAiffTags(p);
    if (isMp4(p)) return readMp4Tags(p);
    return std::nullopt;
}

// Writes directly when the format has a writer here; nothing means "use runa".
std::optional<QString> nativeWrite(const QString& p, const QHash<QString, QString>& c, QStringList* refused)
{
    if (isWav(p)) return writeWav(p, c, refused);
    if (isMp3(p)) return writeMp3(p, c, refused);
    if (isFlac(p)) return writeFlac(p, c, refused);
    if (isOgg(p)) return writeOgg(p, c, refused);
    if (isAiff(p)) return writeAiff(p, c, refused);
    if (isMp4(p)) return writeMp4(p, c, refused);
    return std::nullopt;
}

QString valueOf(const Pairs& tags, const QString& key)
{
    for (const auto& [k, v] : tags)
        if (k == key) return v;
    return {};
}

} // namespace

ReadResult read(const QString& path)
{
    ReadResult r;
    const QFileInfo fi(path);
    if (!fi.isFile()) {
        r.error = "The file isn't there any more.";
        return r;
    }
    if (isPng(path)) r = readPng(path);
    else if (isJpeg(path)) r = readJpeg(path);
    else if (isWebp(path)) r = readWebp(path);
    else if (isOffice(path)) r = readOffice(path);
    else if (isPdf(path)) r = readPdf(path);
    else r = readMedia(path);
    if (r.ok)   // FFmpeg for the facts; the tags read directly where this app writes them
        if (const auto tags = nativeTags(path)) r.tags = *tags;
    if (r.ok) r.info.append({"Size", QLocale().formattedDataSize(fi.size(), 1, QLocale::DataSizeTraditionalFormat)});
    if (r.ok) r.credentials = c2pa::read(path, false);   // the hash check is done when the file is shown
    return r;
}

static WriteResult writeFitting(const QString& path, const QHash<QString, QString>& changes, QStringList refused);

WriteResult write(const QString& path, const QHash<QString, QString>& changes, bool dropCredentials)
{
    WriteResult w;
    if (changes.isEmpty() && !dropCredentials) {
        w.ok = true;
        return w;
    }
    if (!QFileInfo(path).isWritable()) {
        w.error = "The file is read-only.";
        return w;
    }
    if (dropCredentials) {   // first, so the tags are written to the file without them
        w.error = c2pa::remove(path);
        if (!w.error.isEmpty()) return w;
        if (!c2pa::read(path, false).isEmpty()) {
            w.error = "The Content Credentials are still there after removing them.";
            return w;
        }
        if (changes.isEmpty()) {
            w.after = read(path);
            w.ok = w.after.ok;
            if (!w.ok) w.error = "The file was written but can't be read back: " + w.after.error;
            return w;
        }
    }
    // A named field this format has no place for isn't written at all (not
    // even as a free-form tag nothing would read); it is reported instead.
    QStringList refused;
    QHash<QString, QString> fitting;
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        // (Clearing one is always fine.)
        if (!it.value().isEmpty() && fieldOrder(it.key()) < knownFields().size() && !canHold(path, it.key()))
            refused.append(it.key());
        else fitting.insert(it.key(), it.value());
    }
    if (fitting.isEmpty()) {
        w.ok = true;
        w.notStored = refused;
        w.after = read(path);
        return w;
    }
    w = writeFitting(path, fitting, refused);
    for (auto it = changes.begin(); it != changes.end(); ++it)
        if (!fitting.contains(it.key()) && !w.notStored.contains(it.key())) w.notStored.append(it.key());
    return w;
}

static WriteResult writeFitting(const QString& path, const QHash<QString, QString>& changes, QStringList refused)
{
    WriteResult w;
    if (isPng(path)) w.error = writePng(path, changes, &refused);
    else if (isJpeg(path)) w.error = writeJpeg(path, changes, &refused);
    else if (isWebp(path)) w.error = writeWebp(path, changes, &refused);
    else if (isOffice(path)) w.error = writeOffice(path, changes, &refused);
    else if (isPdf(path)) w.error = writePdf(path, changes, &refused);
    else if (const auto direct = nativeWrite(path, changes, &refused)) w.error = *direct;
    else w.error = writeMedia(path, changes);
    if (!w.error.isEmpty()) return w;

    // Read it back: a format can quietly drop a tag it has no place for.
    w.after = read(path);
    if (!w.after.ok) {
        w.error = "The file was written but can't be read back: " + w.after.error;
        return w;
    }
    for (auto it = changes.begin(); it != changes.end(); ++it)
        if (valueOf(w.after.tags, it.key()).trimmed() != it.value().trimmed() && !refused.contains(it.key()))
            refused.append(it.key());
    w.notStored = refused;
    w.ok = true;
    return w;
}

static std::atomic<qint64> inPlaceMin{64ll << 20};
qint64 inPlaceMinSize() { return inPlaceMin; }
void setInPlaceMinSize(qint64 bytes) { inPlaceMin = bytes; }

bool flushToDisk(QFile& f)
{
    if (!f.flush()) return false;
    const HANDLE h = HANDLE(_get_osfhandle(f.handle()));
    return h != INVALID_HANDLE_VALUE && FlushFileBuffers(h);
}

QString tempPathFor(const QString& path)
{
    const QFileInfo fi(path);
    return fi.dir().filePath(fi.completeBaseName() + ".runa-tmp." + fi.suffix());
}

bool replaceFile(const QString& target, const QString& temp, QString* error)
{
    const std::wstring t = QDir::toNativeSeparators(target).toStdWString();
    const std::wstring n = QDir::toNativeSeparators(temp).toStdWString();
    if (ReplaceFileW(t.c_str(), n.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) return true;
    const DWORD code = GetLastError();
    QFile::remove(temp);
    if (error) {
        if (code == ERROR_SHARING_VIOLATION || code == ERROR_ACCESS_DENIED || code == ERROR_UNABLE_TO_REMOVE_REPLACED)
            *error = "The file is open in another program, or read-only. Close it and try again.";
        else
            *error = QString("Couldn't replace the file (Windows error %1).").arg(code);
    }
    return false;
}

} // namespace meta
