#include "Fields.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace meta {

const QList<FieldDef>& knownFields()
{
    static const QList<FieldDef> fields{
        {"title", "Title"},
        {"subtitle", "Subtitle"},
        {"rating", "Rating"},
        {"artist", "Artist / Author"},
        {"album_artist", "Album artist / Band"},
        {"album", "Album"},
        {"composer", "Composer"},
        {"lyricist", "Lyricist"},
        {"conductor", "Conductor"},
        {"remixer", "Remixed by"},
        {"original_artist", "Original artist"},
        {"genre", "Genre"},
        {"date", "Date"},
        {"track", "Track"},
        {"disc", "Disc"},
        {"grouping", "Grouping"},
        {"bpm", "BPM"},
        {"key", "Musical key"},
        {"mood", "Mood"},
        {"isrc", "ISRC"},
        {"subject", "Subject"},
        {"description", "Description", true},
        {"comment", "Comment", true},
        {"keywords", "Keywords"},
        {"copyright", "Copyright"},
        {"publisher", "Publisher"},
        {"category", "Category"},
        {"status", "Status"},
        {"company", "Company"},
        {"manager", "Manager"},
        {"language", "Language"},
        {"lyrics", "Lyrics", true},
        {"encoded_by", "Encoded by"},
        {"encoder", "Software"},
        {"make", "Camera make"},
        {"model", "Camera model"},
        {"creation_time", "Creation time"},
    };
    return fields;
}

static const FieldDef* find(const QString& key)
{
    for (const FieldDef& f : knownFields())
        if (f.key == key) return &f;
    return nullptr;
}

QString fieldLabel(const QString& key)
{
    if (const FieldDef* f = find(key)) return f->label;
    if (key.startsWith("lyrics-")) return "Lyrics (" + key.mid(7) + ")";
    return key;
}

bool fieldMultiline(const QString& key)
{
    if (const FieldDef* f = find(key)) return f->multiline;
    return key.startsWith("lyrics");
}

int fieldOrder(const QString& key)
{
    const QList<FieldDef>& all = knownFields();
    for (int i = 0; i < all.size(); ++i)
        if (all[i].key == key) return i;
    return int(all.size());
}

bool isTechnicalTag(const QString& key)
{
    static const QSet<QString> technical{"major_brand", "minor_version", "compatible_brands", "handler_name",
                                         "vendor_id", "com.apple.quicktime.creationdate"};
    return technical.contains(key.toLower());
}

QStringList fieldsFor(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    // Tagged the way music players expect: the full set of music fields.
    static const QStringList music{"title", "subtitle", "artist", "album_artist", "album", "composer", "lyricist",
                                   "conductor", "remixer", "genre", "date", "track", "disc", "grouping", "bpm", "key",
                                   "mood", "isrc", "description", "comment", "lyrics", "copyright", "publisher",
                                   "language", "encoded_by", "encoder"};
    if (ext == "flac" || ext == "ogg" || ext == "oga" || ext == "opus") return music;   // Vorbis comments
    if (ext == "m4a" || ext == "mp4" || ext == "m4v" || ext == "mov") {                // iTunes items, Windows' rating
        QStringList l = music;
        l.removeAll("encoded_by");
        l.insert(1, "rating");
        return l;
    }
    if (ext == "docx" || ext == "xlsx" || ext == "pptx")
        return {"title", "subject", "artist", "keywords", "comment", "category", "status", "company", "manager", "copyright",
                "language"};
    if (ext == "odt" || ext == "ods" || ext == "odp") return {"title", "subject", "artist", "keywords", "comment", "copyright", "language"};
    if (ext == "pdf") return {"title", "artist", "subject", "keywords", "copyright", "encoder"};
    if (ext == "png") return {"title", "artist", "description", "comment", "keywords", "copyright", "date", "encoder"};
    if (ext == "jpg" || ext == "jpeg" || ext == "webp")
        return {"title", "rating", "artist", "subject", "description", "comment", "keywords", "copyright", "date", "encoder",
                "make", "model"};
    if (ext == "wav")   // what a WAV's INFO list holds
        return {"title", "artist", "album", "genre", "date", "track", "subject", "comment", "keywords", "copyright",
                "language", "encoded_by", "encoder"};
    if (ext == "avi") return {"title", "artist", "genre", "date", "track", "comment", "copyright", "language"};
    if (ext == "mp3" || ext == "aif" || ext == "aiff" || ext == "aifc")   // ID3   // ID3v2: every music field Windows shows, and any other as a TXXX frame
        return {"title", "subtitle", "rating", "artist", "album_artist", "album", "composer", "lyricist", "conductor",
                "remixer", "original_artist", "genre", "date", "track", "disc", "grouping", "bpm", "key", "mood", "isrc",
                "comment", "lyrics", "copyright", "publisher", "language", "encoded_by", "encoder"};
    // Matroska (MKV, WebM) takes any name.
    return {"title", "subtitle", "artist", "album_artist", "album", "composer", "lyricist", "conductor", "genre", "date",
            "track", "disc", "description", "comment", "copyright", "publisher", "language", "lyrics", "encoded_by",
            "grouping", "bpm", "mood", "isrc"};
}

bool canHold(const QString& path, const QString& key)
{
    if (fieldsFor(path).contains(key)) return true;
    if (find(key)) return false;   // a named field this format has no place for
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "wav" || ext == "avi") {   // INFO list: only its four-letter IDs
        static const QRegularExpression infoId("^I[A-Z0-9]{3}$");
        return infoId.match(key).hasMatch();
    }
    // Formats that take fields of your own.
    static const QStringList freeForm{"mp3", "mkv", "webm", "png", "flac", "ogg", "oga", "opus", "aif", "aiff", "aifc",
                                      "m4a", "mp4", "m4v", "mov", "docx", "xlsx", "pptx", "odt", "ods", "odp", "pdf"};
    return freeForm.contains(ext);
}

QStringList supportedExtensions()
{
    return {"mp3", "wav", "flac", "ogg", "oga", "opus", "aif", "aiff", "aifc", "m4a",   // audio
            "mp4", "m4v", "mov", "mkv", "webm", "avi",                                // video
            "png", "jpg", "jpeg", "webp",                                             // images
            "docx", "xlsx", "pptx", "odt", "ods", "odp", "pdf"};                      // documents
}

Kind kindOf(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    static const QStringList audio{"mp3", "wav", "flac", "ogg", "oga", "opus", "aif", "aiff", "aifc", "m4a"};
    static const QStringList image{"png", "jpg", "jpeg", "webp"};
    static const QStringList document{"docx", "xlsx", "pptx", "odt", "ods", "odp", "pdf"};
    if (audio.contains(ext)) return Kind::Audio;
    if (image.contains(ext)) return Kind::Image;
    if (document.contains(ext)) return Kind::Document;
    if (supportedExtensions().contains(ext)) return Kind::Video;
    return Kind::Unknown;
}

bool isSupported(const QString& path) { return kindOf(path) != Kind::Unknown; }

QString formatBadge(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toUpper();
    if (ext == "JPEG") return "JPG";
    if (ext == "AIFF" || ext == "AIFC") return "AIF";
    return ext;
}

} // namespace meta
