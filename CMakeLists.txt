cmake_minimum_required(VERSION 3.21)
project(LokiMetadata VERSION 0.1.1 LANGUAGES CXX)

# Loki Metadata: a bulk metadata editor for audio, video and images.
#
#   cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=<Qt>/msvc2022_64 -DFFMPEG_ROOT=<ffmpeg>
#   cmake --build build --config Release
#
# Everything lands in build/bin/Release, and the app laid out as an install
# in "Loki Metadata/" at the top of the project.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Needs a static FFmpeg (FFMPEG_ROOT, see cmake/FFmpeg.cmake), and qpdf and
# zlib from vcpkg's x64-windows-static-md (see app/CMakeLists.txt).

if(MSVC)
    add_compile_options(/utf-8 /W3)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Xml Test)
qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(engine)
add_subdirectory(app)

enable_testing()
add_subdirectory(tests)
