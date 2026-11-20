// Runa: reads and writes the tags of audio and video files (MP3, WAV, MP4,
// MOV, MKV, WebM, AVI...). FFmpeg is linked statically inside runa.dll, kept
// away from Qt (the Muninn pattern from Loki Studio).
//
// Writing rewrites the file with every stream copied as it is (no
// re-encoding); only the container's tags change. The new file is written
// beside the old one and swapped in when complete, so a failure leaves the
// original untouched.
//
// All strings are UTF-8.

#pragma once

#ifdef RUNA_BUILD
#define RUNA_API extern "C" __declspec(dllexport)
#else
#define RUNA_API extern "C" __declspec(dllimport)
#endif

/// Called once per tag (key, value) or per line of file info (label, text).
typedef void (*runa_pair_fn)(void* ctx, const char* key, const char* value);

/// Reads the file's tags and a short summary of what is in it (format,
/// duration, streams). Returns 0 on success; otherwise @p error holds why.
RUNA_API int runa_read(const char* path, runa_pair_fn tag, runa_pair_fn info, void* ctx, char* error, int errorSize);

/// Changes the file's tags: keys[i] is set to values[i], or removed when
/// values[i] is NULL or empty. Tags not named are kept. Returns 0 on success.
RUNA_API int runa_write(const char* path, const char* const* keys, const char* const* values, int count, char* error,
                        int errorSize);
