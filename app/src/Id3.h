// ID3v2 (2.3 and 2.4) tags: MP3 files, and AIFF files (in their "ID3 "
// chunk). Only the frames for changed fields are rebuilt; every other frame
// (cover art, other players' ratings, private data, ReplayGain...) is kept as
// it is. ID3v2.2 isn't edited.

#pragma once

#include "MetaIO.h"

#include <optional>

namespace meta::id3 {

struct Frame {
    QByteArray id;
    quint16 flags = 0;
    QByteArray data;   // as stored
};

struct Tag {
    bool present = false;
    int version = 3;   // a new tag is 2.3: what Windows and every player read
    QList<Frame> frames;
};

/// The size of the tag @p header (its first 10 bytes) introduces, footer
/// included; 0 when it isn't an ID3v2 header.
qint64 tagSize(const QByteArray& header);

/// Parses a whole tag (header and all). An empty array is "no tag yet";
/// nothing comes back for 2.2 or a damaged tag.
std::optional<Tag> parse(const QByteArray& bytes);

/// Every field the tag holds.
Pairs fields(const Tag& tag);

/// Makes @p changes to the tag's frames; fields it can't take go to @p notStored.
void apply(Tag& tag, const QHash<QString, QString>& changes, QStringList* notStored);

/// The tag as bytes, with @p padding zero bytes for later in-place edits.
/// Empty when it has no frames.
QByteArray build(const Tag& tag, int padding);

/// ID3v1's numbered genres.
const QStringList& genres();

} // namespace meta::id3
