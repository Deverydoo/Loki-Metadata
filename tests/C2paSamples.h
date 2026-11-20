// Files carrying Content Credentials, built for the tests: a manifest store
// (JUMBF boxes, CBOR assertions) saying "created by Test Generator 2.0, made
// by AI", with a data hash that matches the file it is put in. No signature
// (there's no certificate to sign with), which the reader doesn't need.

#pragma once

#include <QByteArray>

enum class C2paHome { Png, Jpeg, Webp, Wav, Mp3, Mp4 };

/// @p file with a manifest store embedded where @p home keeps it. The store's
/// data hash covers the whole result except the store's own bytes (none for
/// MP4, whose files use a different kind of hash). Empty if @p file isn't
/// one it understands. @p split spreads a JPEG's store over several APP11
/// segments.
QByteArray withCredentials(const QByteArray& file, C2paHome home, bool split = false);

/// @p file with @p store put in as it is (no hash work).
QByteArray withStore(const QByteArray& file, C2paHome home, const QByteArray& store);

/// A store whose boxes are cut short, for checking damaged ones are survived.
QByteArray damagedStore();
