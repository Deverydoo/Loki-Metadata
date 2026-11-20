# runa.dll: audio and video tags through FFmpeg, linked statically inside the
# DLL and kept away from Qt.

include(${PROJECT_SOURCE_DIR}/cmake/FFmpeg.cmake)

configure_file(runa.rc.in ${CMAKE_CURRENT_BINARY_DIR}/runa.rc @ONLY)
add_library(runa SHARED include/runa.h src/runa.cpp ${CMAKE_CURRENT_BINARY_DIR}/runa.rc)
target_compile_definitions(runa PRIVATE RUNA_BUILD _CRT_SECURE_NO_WARNINGS)
target_include_directories(runa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(runa PRIVATE ${FFMPEG_LIBRARIES})

# Writes small tagged media files for the tests. Not shipped.
add_executable(runa_samples tools/runa_samples.cpp)
target_compile_definitions(runa_samples PRIVATE _CRT_SECURE_NO_WARNINGS)
target_include_directories(runa_samples PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(runa_samples PRIVATE ${FFMPEG_LIBRARIES})
