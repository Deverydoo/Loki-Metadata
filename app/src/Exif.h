// EXIF's first directory (IFD0), where Windows and most photo tools keep a
// picture's title, author, comments, keywords and rating. Shared by JPG and
// WebP, which store the same TIFF block in different wrappers.
//
// Writing adds a new IFD0 at the end of the block and points the header at
// it; everything else in the block (camera settings, GPS, maker notes, the
// thumbnail) stays byte for byte where it was, so no offset inside it breaks.

#pragma once

#include "MetaIO.h"

namespace meta::exif {

/// Fills in the fields @p tiff holds that @p tags doesn't have yet, and adds
/// when the picture was taken to @p info.
void read(const QByteArray& tiff, Pairs* tags, Pairs* info);

/// Whether EXIF has a place for @p field.
bool holds(const QString& field);

/// The block with @p changes made (a new block when @p tiff is empty).
/// Fields EXIF can't hold go to @p notStored. Sets @p changed when any
/// field EXIF holds was among the changes.
QByteArray update(const QByteArray& tiff, const QHash<QString, QString>& changes, QStringList* notStored, bool* changed);

} // namespace meta::exif
