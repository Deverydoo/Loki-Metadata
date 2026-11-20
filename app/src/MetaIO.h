// Reading and writing a file's metadata, whatever its kind. Audio and video
// go through runa.dll (FFmpeg); PNG and JPG are handled here directly.
// Safe to call from any thread.

#pragma once

#include "Fields.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <optional>

namespace meta {

using Pairs = QList<QPair<QString, QString>>;

struct ReadResult {
    bool ok = false;
    QString error;
    Pairs tags;   // field key -> value
    Pairs info;   // read-only facts: label -> text
};

struct WriteResult {
    bool ok = false;
    QString error;
    QStringList notStored;   // changed fields this format could not keep
    ReadResult after;        // the file as read back after writing
};

ReadResult read(const QString& path);

/// Applies @p changes (field key -> new value; an empty value removes the
/// field). Afterwards the file is read again to check every change stuck;
/// any that did not are listed in notStored.
WriteResult write(const QString& path, const QHash<QString, QString>& changes);

// The image formats (ImageMeta.cpp).
ReadResult readPng(const QString& path);
QString writePng(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored);
ReadResult readJpeg(const QString& path);
/// WAV tags (WavMeta.cpp): the INFO list is rewritten and every other chunk
/// copied byte for byte. Returns nothing when the file is one it can't edit
/// this way (RF64, damaged sizes), to fall back to runa.
std::optional<QString> writeWav(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored);
/// MP3 tags (Mp3Meta.cpp): ID3v2 frames for changed fields rebuilt, every
/// other frame and the audio copied as they are. Nothing when the tag is one
/// it can't edit (ID3v2.2), to fall back to runa.
std::optional<Pairs> readMp3Tags(const QString& path);
std::optional<QString> writeMp3(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored);

// The other formats written directly, each in its own file. The audio and
// video ones read only the tags (FFmpeg reads the facts) and return nothing
// when the file is one they can't edit, to fall back to runa.
std::optional<Pairs> readFlacTags(const QString& path);
std::optional<QString> writeFlac(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored);
std::optional<Pairs> readOggTags(const QString& path);
std::optional<QString> writeOgg(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored);
std::optional<Pairs> readAiffTags(const QString& path);
std::optional<QString> writeAiff(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored);
std::optional<Pairs> readMp4Tags(const QString& path);
std::optional<QString> writeMp4(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored);
ReadResult readWebp(const QString& path);
QString writeWebp(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored);
ReadResult readOffice(const QString& path);
QString writeOffice(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored);
ReadResult readPdf(const QString& path);
QString writePdf(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored);
QString writeJpeg(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored);

/// Swaps @p temp in for @p target, keeping the target's creation date and
/// permissions. Removes @p temp if it fails.
bool replaceFile(const QString& target, const QString& temp, QString* error);
/// Where to write the new version of @p path before swapping it in.
QString tempPathFor(const QString& path);

} // namespace meta
