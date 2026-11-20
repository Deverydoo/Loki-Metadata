# Third-party software in Loki Metadata

Loki Metadata itself is licensed under the Apache License 2.0 (see
`LICENSE`). The Windows release includes the components below, each under its
own license. The full license texts are in the `licenses` folder.

## FFmpeg 9.0.2 (LGPL 2.1 or later)

Inside `runa.dll` (linked statically), to read audio and video files and to
write MKV, WebM and AVI tags. Built without any GPL or non-free parts:

```
--toolchain=msvc --target-os=win64 --arch=x86_64 --enable-static --disable-shared
--disable-debug --disable-programs --disable-doc --enable-libass --enable-libdav1d
--enable-ffnvcodec --enable-nvenc --enable-nvdec --enable-cuvid
--extra-cflags='-MD -GL' --extra-cxxflags='-MD -GL' --extra-ldflags=-LTCG
```

- License: GNU Lesser General Public License 2.1 or later (`licenses/LGPL-2.1.txt`)
- Source: https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz
- To relink `runa.dll` against your own build of FFmpeg, build this project
  from source (https://github.com/Deverydoo/Loki-Metadata) with `FFMPEG_ROOT`
  pointing at it; see the README.

## dav1d 1.5.4 (BSD 2-Clause)

AV1 decoding inside FFmpeg. `licenses/dav1d.txt`. Source: https://code.videolan.org/videolan/dav1d

## Qt 6.12.0 (LGPL 3)

The user interface. Shipped as separate DLLs and plugins, which may be
replaced with other builds of the same Qt version.

- License: GNU Lesser General Public License 3 (`licenses/LGPL-3.0.txt`, which
  builds on `licenses/GPL-3.0.txt`)
- Source: https://download.qt.io/official_releases/qt/6.12/

## qpdf 12.4.2 (Apache 2.0)

Reading and writing PDF properties; linked statically.
Copyright (c) 2005-2021 Jay Berkenbilt, 2022-2026 Jay Berkenbilt and Manfred Holger.
`licenses/qpdf.txt` (its NOTICE and license). Source: https://github.com/qpdf/qpdf

## zlib 1.3.2 (zlib license)

Office document archives, and used by qpdf and FFmpeg; linked statically.
(C) 1995-2026 Jean-loup Gailly and Mark Adler. `licenses/zlib.txt`.
Source: https://zlib.net

## libjpeg-turbo 3.2.0 (IJG and BSD 3-Clause)

Used by qpdf; linked statically. This software is based in part on the work
of the Independent JPEG Group. `licenses/libjpeg-turbo.txt`.
Source: https://libjpeg-turbo.org

## Microsoft Visual C++ runtime

`vcruntime140*.dll`, `msvcp140*.dll` and `concrt140.dll`, redistributed
as permitted by the Visual Studio license.

## Icons

The interface's icons come from the Segoe MDL2 Assets font that is part of
Windows; the font itself isn't included.
