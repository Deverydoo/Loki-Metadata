// Content Credentials (C2PA): the signed record of where a file came from,
// which AI tools (OpenAI, Suno, Adobe...) and some cameras embed. Shown
// read-only, and removable. Never edited: the record is signed by whoever
// made it, so changing a claim would only forge or break it.
//
// Where it lives: PNG caBX chunk, JPG APP11 (JUMBF) segments, WebP and WAV
// "C2PA" chunks, MP3 ID3 GEOB frame, MP4/MOV/M4A top-level uuid box.

#pragma once

#include <QString>
#include <QVariantMap>

namespace meta::c2pa {

/// What the file's credentials say; empty when it has none. Keys:
///   present (bool), source ("AI-generated"...), ai (bool), tool, recordedBy,
///   signer, when, actions (list of text), history (int: earlier records),
///   match ("unchanged" / "changed" / "" not checked), notes (list of text).
QVariantMap read(const QString& path);

/// Whether this kind of file can carry credentials here.
bool supports(const QString& path);

/// Deletes the credentials from the file (nothing else changes). "" on
/// success or when there were none; otherwise why not.
QString remove(const QString& path);

} // namespace meta::c2pa
