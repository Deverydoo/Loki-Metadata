// XMP: the XML metadata packet Adobe's tools (Photoshop, Lightroom, Bridge)
// and many others write into PNG and JPG files. Its common properties are
// shown as the usual fields, and updated when those fields are edited, so
// those tools see the same values as Windows does.
//
//   dc:title        -> title         dc:creator  -> artist   (list, "; ")
//   dc:description  -> description   dc:subject  -> keywords (list, "; ")
//   dc:rights       -> copyright     xmp:CreatorTool -> encoder
//   xmp:Rating      -> rating
//
// Everything else in the packet (edit history, IDs, camera raw settings...)
// is kept as it is.

#pragma once

#include "MetaIO.h"

namespace meta::xmp {

/// Fills in fields the packet holds that @p tags doesn't have yet, and adds
/// the dates to @p info.
void read(const QByteArray& packet, Pairs* tags, Pairs* info);

/// Whether @p field is one XMP holds.
bool holds(const QString& field);

/// The packet with @p changes made to the fields it holds. Returns the packet
/// as it was when no change touches it, and an empty array if it can't be
/// read as XML.
QByteArray update(const QByteArray& packet, const QHash<QString, QString>& changes);

} // namespace meta::xmp
