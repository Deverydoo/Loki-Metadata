// The fields the editor knows by name. Every format's own tags are read into
// these keys (an MP3's TIT2, a PNG's "Title" and a JPG's XPTitle all become
// "title"), so files of different kinds can be edited together. Tags with no
// common name keep their own key.

#pragma once

#include <QString>
#include <QStringList>

namespace meta {

enum class Kind { Audio, Video, Image, Document, Model, Unknown };

struct FieldDef {
    QString key;
    QString label;
    bool multiline = false;
};

/// The named fields, in the order they are shown.
const QList<FieldDef>& knownFields();

QString fieldLabel(const QString& key);
bool fieldMultiline(const QString& key);
/// Where the field sorts: named fields in table order, then the rest by name.
int fieldOrder(const QString& key);
/// Bookkeeping tags written by the container itself (MP4 brands and the
/// like): shown as file info, never edited, always kept.
bool isTechnicalTag(const QString& key);

/// The named fields this file type can store, for "Add field".
QStringList fieldsFor(const QString& path);
/// Whether a file of this type can take @p key (a named field it has a
/// place for, or a free-form one where the format allows those). A file
/// that already has the field can of course keep it; callers check that.
bool canHold(const QString& path, const QString& key);

Kind kindOf(const QString& path);
QStringList supportedExtensions();   // lower case, no dot
bool isSupported(const QString& path);
/// "MP3", "PNG"... for the badge in the list.
QString formatBadge(const QString& path);

} // namespace meta
