// Vorbis comments: the tags of FLAC, Ogg Vorbis and Opus files. A list of
// NAME=value lines; any name is allowed, and a name may repeat.

#pragma once

#include "MetaIO.h"

#include <optional>

namespace meta::vorbis {

struct Comments {
    QByteArray vendor;          // the encoder's name
    QList<QByteArray> lines;    // "NAME=value", UTF-8
};

/// Parses the comment structure (vendor, count, lines); @p used says how
/// many bytes it took.
std::optional<Comments> parse(const QByteArray& bytes, qsizetype* used = nullptr);
QByteArray build(const Comments& c);

/// Every field the comments hold (pictures left out).
Pairs fields(const Comments& c);
/// Makes @p changes; every field fits (names are free-form).
void apply(Comments& c, const QHash<QString, QString>& changes);

} // namespace meta::vorbis
