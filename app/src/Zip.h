// Just enough ZIP for Office documents: read the directory, unpack an entry,
// and write the archive again with a few entries replaced or added. Every
// other entry is copied byte for byte, compressed data and all, in its old
// order (OpenDocument needs its "mimetype" entry to stay first). ZIP64 and
// encrypted archives aren't handled.

#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

#include <optional>

class QFile;

namespace meta::zip {

struct Entry {
    QByteArray name;
    quint16 madeBy = 20, needed = 20, flags = 0, method = 0, time = 0, date = 0, internalAttr = 0;
    quint32 crc = 0, packedSize = 0, size = 0, externalAttr = 0, localAt = 0;
    QByteArray extra, comment;   // as in the central directory
};

struct Archive {
    QList<Entry> entries;
    QByteArray comment;
    const Entry* find(const QByteArray& name) const;
};

std::optional<Archive> read(QFile& f);
/// An entry's contents, unpacked (stored or deflated).
std::optional<QByteArray> extract(QFile& f, const Entry& e);

/// Writes the archive to @p out: entries copied as they are, except those
/// named in @p replace (new contents), plus @p add at the end. Returns why
/// if it failed.
QString rewrite(QFile& in, const Archive& a, const QHash<QByteArray, QByteArray>& replace,
                const QList<QPair<QByteArray, QByteArray>>& add, QFile& out);

/// A new archive holding @p files, the first one stored as is (for tests,
/// and OpenDocument's "mimetype" rule), the rest deflated.
QByteArray create(const QList<QPair<QByteArray, QByteArray>>& files);

} // namespace meta::zip
