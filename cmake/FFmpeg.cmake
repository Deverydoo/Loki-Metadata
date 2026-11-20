# A static FFmpeg build for runa.dll. Any LGPL build works; set FFMPEG_ROOT to
# its install prefix (with include/ and lib/). For example with vcpkg:
#
#   vcpkg install ffmpeg[avcodec,avformat]:x64-windows-static-md
#   -DFFMPEG_ROOT=<vcpkg>/installed/x64-windows-static-md
#
# Libraries FFmpeg itself was built against (dav1d, zlib...) are looked for in
# FFMPEG_EXTRA_LIB_DIR (by default FFMPEG_ROOT/lib) and linked when present.
#
# Gives FFMPEG_INCLUDE_DIR and FFMPEG_LIBRARIES.

set(FFMPEG_ROOT "" CACHE PATH "Install prefix of a static FFmpeg build (include/, lib/)")
set(FFMPEG_EXTRA_LIB_DIR "${FFMPEG_ROOT}/lib" CACHE PATH "Where the libraries FFmpeg was built against are")

if(NOT EXISTS "${FFMPEG_ROOT}/include/libavformat/avformat.h")
    message(FATAL_ERROR
        "FFmpeg not found. Set FFMPEG_ROOT to a static FFmpeg build, for example:\n"
        "  vcpkg install ffmpeg[avcodec,avformat]:x64-windows-static-md\n"
        "  cmake -B build ... -DFFMPEG_ROOT=<vcpkg>/installed/x64-windows-static-md")
endif()

set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIBRARIES)
# Higher-level libraries first on the link line.
foreach(lib avformat avcodec swresample avutil)
    unset(found CACHE)
    find_library(found NAMES ${lib} lib${lib} PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
    if(found)
        list(APPEND FFMPEG_LIBRARIES "${found}")
    elseif(NOT lib STREQUAL "swresample")
        message(FATAL_ERROR "FFmpeg's ${lib} library isn't in ${FFMPEG_ROOT}/lib")
    endif()
endforeach()
unset(found CACHE)

foreach(extra dav1d zs zlib bz2 lzma)
    if(EXISTS "${FFMPEG_EXTRA_LIB_DIR}/${extra}.lib")
        list(APPEND FFMPEG_LIBRARIES "${FFMPEG_EXTRA_LIB_DIR}/${extra}.lib")
    endif()
endforeach()

# Windows libraries a static FFmpeg needs.
list(APPEND FFMPEG_LIBRARIES bcrypt crypt32 gdi32 mfuuid mfplat ncrypt ole32 secur32 strmiids user32 ws2_32)
