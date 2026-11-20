qt_add_executable(test_metadata test_metadata.cpp DocSamples.h DocSamples.cpp C2paSamples.h C2paSamples.cpp)
target_link_libraries(test_metadata PRIVATE metadata_ui metadata_uiplugin Qt6::Test)
get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qtbin "${_qmake}" DIRECTORY)
target_compile_definitions(test_metadata PRIVATE
    SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    QT_PLATFORMS_DIR="${_qtbin}/../plugins/platforms"
    SAMPLES_EXE="$<TARGET_FILE:runa_samples>")
add_dependencies(test_metadata runa_samples LokiMetadata)

add_test(NAME metadata COMMAND test_metadata)
set_tests_properties(metadata PROPERTIES TIMEOUT 300)

# Qt's test library beside the test (windeployqt only deploys for the app).
add_custom_command(TARGET test_metadata POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:test_metadata>")

# meta_cli: read and write fields from the command line, for checking by hand. Not shipped.
qt_add_executable(meta_cli meta_cli.cpp DocSamples.h DocSamples.cpp)
target_link_libraries(meta_cli PRIVATE metadata_ui)

# winprops: what Windows' own property system reads and writes. Not shipped.
add_executable(winprops winprops.cpp)
target_compile_definitions(winprops PRIVATE UNICODE _UNICODE)
target_link_libraries(winprops PRIVATE propsys ole32 shell32)

# load_bench: how adding thousands of files behaves (time, UI stalls, memory). Not shipped.
qt_add_executable(load_bench load_bench.cpp)
target_link_libraries(load_bench PRIVATE metadata_ui metadata_uiplugin psapi)
