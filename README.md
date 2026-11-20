<p align="center"><img src="docs/logo.png" width="520" alt="Loki Metadata"></p>

# Loki Metadata

Edit the tags and properties of music, video, pictures and documents, one
file at a time or many at once. For Windows 10 and 11 (64-bit).

**Download:** [the latest release](https://github.com/Deverydoo/Loki-Metadata/releases/latest):
the installer (`LokiMetadata-<version>-Setup.exe`), or the portable zip
(unzip anywhere and run `LokiMetadata.exe`).

- Drop files or folders on the window, or use **Add files** / **Add folder**.
- Click a file to see every field its type can hold, filled in or not.
  Ctrl-click, Shift-click or Ctrl+A to select several: every field any of
  them can hold is shown. Where the values differ the box says "Different
  values"; typing there makes them all the same. Fields only some of the
  files can hold say how many ("2 of 5 files"), and each change goes only to
  the files that can hold it.
- **Add a custom field** adds one of your own where the format allows it.
  A field none of the selected files can hold is refused, with a note.
- **Content Credentials** (the signed C2PA record that ChatGPT, Suno, Adobe
  and some cameras put in a file) are shown under the fields: what made the
  file, whether it says AI made it, who signed it, the steps it records, and
  whether the file still matches what was signed. Files that carry them are
  marked **AI** or **CR** in the list. They can be removed, but never edited:
  they're signed by whoever made the file, so changing them would forge them.
- Nothing is written until **Apply**. Changed files carry an amber dot;
  **Discard** forgets every change. After Apply each file is read back to
  check every change stuck.

## Formats, and how they're written

Every format is written beside the original and swapped in with
`ReplaceFile`, so a failure leaves the original alone and the file keeps its
creation date. Only the metadata changes; sound, pictures and document
content are copied byte for byte (the tests fingerprint every audio and video
packet before and after).

| Kind | Where the tags go |
|------|-------------------|
| MP3 | ID3v2 (version kept; new tags are 2.3). Frames for changed fields rebuilt; cover art, other players' ratings, private data and the rest kept. ID3v1 at the end brought in line. Rating: Windows' POPM frame. |
| WAV | The LIST/INFO chunk; every other chunk (bext, cue, C2PA...) kept. |
| FLAC | The VORBIS_COMMENT block; pictures, seek table and the rest kept. The comment also goes to DESCRIPTION, where Windows reads it. |
| OGG, Opus | The comment header, header pages laid out again; audio pages renumbered with fresh checksums when needed. |
| AIFF | An ID3v2 tag in an "ID3 " chunk. |
| MP4, M4A, M4V, MOV | The iTunes item list (moov/udta/meta/ilst); MOV's QuickTime text items kept in step. Rating: Windows' Xtra box. Offsets after the moov (fast-start files, fragmented recordings) are moved when its size changes. |
| MKV, WebM, AVI | `runa.dll` (FFmpeg, static inside the DLL): every stream copied as it is into a new file with the new tags. |
| JPG, WebP | EXIF IFD0 (title, author, comment, keywords, rating...), added as a new IFD0 so camera data, GPS and thumbnails stay where they were. XMP updated too, if there is some. |
| PNG | The text chunks; XMP updated too, if there is some. |
| Word, Excel, PowerPoint | docProps/core.xml, app.xml and custom.xml (copyright and your own fields) inside the zip; every other entry copied byte for byte. |
| OpenDocument | meta.xml inside the zip (mimetype stays first and stored). |
| PDF | The information dictionary and XMP, through qpdf; signed PDFs are refused. |

### Content Credentials

Read from the PNG `caBX` chunk, JPG APP11 (JUMBF) segments, the WebP and WAV
`C2PA` chunk, the MP3 ID3 GEOB frame, and the MP4/M4A/MOV top-level `uuid`
box. The current manifest (the last in the store) is described; earlier ones
are counted. "Since signing" recomputes the record's data hash
(`c2pa.hash.data`, with its exclusions) over the file. The signature itself
isn't checked; [contentcredentials.org/verify](https://contentcredentials.org/verify)
does that. Removal deletes only that chunk, segment, frame or box (in MP4
files the media offsets are moved back to match). An invisible watermark in
the pixels or sound, if the maker added one, isn't touched; the app says so.

Editing any field of a file with Content Credentials changes the file, so its
record then shows "Changed": it still says what it said, it just no longer
matches byte for byte.

### What Windows Explorer shows

Checked with Windows' own property system (`winprops`): MP3 everything;
FLAC everything but rating; MP4/M4A/MOV everything but copyright;
OGG/Opus everything but copyright and rating; JPG everything; Word title,
author, comment and keywords. Windows 10 reads nothing from WAV, AIFF, PNG,
ODT or PDF files (other programs do), and only the copyright from WebP.

## Building

Needs Visual Studio 2022, CMake 3.21+, Qt 6.8+ (released with 6.12), and from
vcpkg (`x64-windows-static-md`): qpdf, zlib, and a static FFmpeg (any LGPL
build; the release uses FFmpeg 9.0.2).

```
vcpkg install qpdf:x64-windows-static-md ffmpeg[avcodec,avformat]:x64-windows-static-md
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64 ^
      -DFFMPEG_ROOT=C:/vcpkg/installed/x64-windows-static-md
cmake --build build --config Release
cd build && ctest -C Release
```

(vcpkg is looked for in `VCPKG_ROOT`, or `C:/vcpkg`; set `VCPKG_STATIC_MD`
to point elsewhere.) Each build refreshes `Loki Metadata/` at the project
root: the app laid out as an install. If the app is open there, its files are
moved aside and the new ones put in place for next time.

To make a release (tests, a fresh layout, the zip and the installer in
`dist/`; needs Inno Setup 6):

```
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

Developer tools in `build/bin/Release` (not shipped):

- `test_metadata.exe`: the tests (`LOKI_UI_SHOTS=<folder>` saves pictures of the page).
- `meta_cli.exe <file> [key=value ...] [--remove-credentials]`: read or write fields from the command line.
- `winprops.exe get|set <file> ...`: what Windows' property system reads, or writes.
- `runa_samples.exe <folder>` / `--packets <file>`: sample files; a fingerprint of every packet.

## License

Copyright 2026 Chaos Rift Entertainment, LLC. Licensed under the
[Apache License 2.0](LICENSE). Loki Metadata uses FFmpeg (LGPL), Qt (LGPL),
qpdf, zlib, libjpeg-turbo and dav1d; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Themes

All styling lives in `app/qml/theme/`. Components read only `Theme.color.*`,
`Theme.size.*`, `Theme.font.*` and `Theme.logo`, and a test fails if any other QML file
names a colour. To add a theme, copy `themes/DarkTheme.qml`, change the
values, list it in `Theme.qml`'s `available` and in `app/CMakeLists.txt`,
then set `Theme.name`.
