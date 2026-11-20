
# The editor and its QML, as a library the app and the tests share.
qt_add_library(metadata_ui STATIC
    src/Fields.h
    src/Fields.cpp
    src/MetaIO.h
    src/MetaIO.cpp
    src/ImageMeta.cpp
    src/WavMeta.cpp
    src/Mp3Meta.cpp
    src/Id3.h
    src/Id3.cpp
    src/Exif.h
    src/Exif.cpp
    src/Vorbis.h
    src/Vorbis.cpp
    src/FlacMeta.cpp
    src/OggMeta.cpp
    src/AiffMeta.cpp
    src/WebpMeta.cpp
    src/Mp4Meta.cpp
    src/C2pa.h
    src/C2pa.cpp
    src/Zip.h
    src/Zip.cpp
    src/OfficeMeta.cpp
    src/PdfMeta.cpp
    src/FbxMeta.cpp
    src/Xmp.h
    src/Xmp.cpp
    src/MetaEditor.h
    src/MetaEditor.cpp
)
target_include_directories(metadata_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(metadata_ui PUBLIC Qt6::Quick Qt6::QuickControls2 Qt6::Xml runa)
target_compile_definitions(metadata_ui PUBLIC LOKI_METADATA_VERSION="${PROJECT_VERSION}")

# zlib (Office archives) and qpdf (PDF), static from vcpkg, /MD like Qt.
if(DEFINED ENV{VCPKG_ROOT})
    set(_vcpkg "$ENV{VCPKG_ROOT}")
else()
    set(_vcpkg "C:/vcpkg")
endif()
set(VCPKG_STATIC_MD "${_vcpkg}/installed/x64-windows-static-md" CACHE PATH "vcpkg x64-windows-static-md prefix (qpdf, zlib)")
target_include_directories(metadata_ui PRIVATE "${VCPKG_STATIC_MD}/include")
target_link_libraries(metadata_ui PUBLIC "${VCPKG_STATIC_MD}/lib/qpdf.lib" "${VCPKG_STATIC_MD}/lib/jpeg.lib"
                      "${VCPKG_STATIC_MD}/lib/zs.lib" advapi32 crypt32)

# The theme: tokens, the theme in use, and the themes themselves. A new theme
# is one more file under qml/theme/themes/, listed here and in Theme.qml.
set(THEME_FILES
    qml/theme/Theme.qml
    qml/theme/ThemeSpec.qml
    qml/theme/ColorTokens.qml
    qml/theme/SizeTokens.qml
    qml/theme/FontTokens.qml
    qml/theme/Icons.qml
    qml/theme/themes/DarkTheme.qml
)
set_source_files_properties(qml/theme/Theme.qml qml/theme/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(metadata_ui
    URI LokiMetadata
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        ${THEME_FILES}
        qml/components/Glyph.qml
        qml/components/AppButton.qml
        qml/components/IconButton.qml
        qml/components/AppTextField.qml
        qml/components/AppTextArea.qml
        qml/components/AppScrollBar.qml
        qml/components/AppMenu.qml
        qml/components/AppMenuItem.qml
        qml/components/AppMenuSeparator.qml
        qml/components/AppDialog.qml
        qml/components/AppToolTip.qml
        qml/components/Badge.qml
        qml/components/Banner.qml
        qml/components/SectionHeading.qml
        qml/components/CredentialsChip.qml
        qml/components/AppCheckBox.qml
        qml/views/MainView.qml
        qml/views/TopBar.qml
        qml/views/StatusBar.qml
        qml/views/FileListPane.qml
        qml/views/FileRow.qml
        qml/views/DetailsPane.qml
        qml/views/FieldRow.qml
        qml/views/DropOverlay.qml
        qml/views/CredentialsSection.qml
    RESOURCES
        qml/images/logo.png
)

configure_file(resources/LokiMetadata.rc.in ${CMAKE_CURRENT_BINARY_DIR}/LokiMetadata.rc @ONLY)
qt_add_executable(LokiMetadata WIN32 src/main.cpp ${CMAKE_CURRENT_BINARY_DIR}/LokiMetadata.rc)
target_link_libraries(LokiMetadata PRIVATE metadata_ui metadata_uiplugin)

# Qt's DLLs and QML beside the exe in the build folder (for running and testing there).
set(DEPLOY_FLAGS --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw)
add_custom_command(TARGET LokiMetadata POST_BUILD
    COMMAND "$<TARGET_FILE:Qt6::windeployqt>" ${DEPLOY_FLAGS} --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE:LokiMetadata>"
    COMMENT "Deploying Qt beside LokiMetadata.exe")

# "stage": the app laid out as an install at the project root ("Loki Metadata/"):
# the exe, runa.dll, the Qt it needs, the Visual C++ runtime, and the license
# files. Runs with every build; scripts/package.ps1 zips it and builds the installer.
set(LOKI_INSTALL_DIR "${PROJECT_SOURCE_DIR}/Loki Metadata" CACHE PATH "Where the ready-to-run copy goes")
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)   # finds the Visual C++ runtime DLLs to ship
string(REPLACE ";" "|" RUNTIME_DLLS "${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
set(DOCS "${PROJECT_SOURCE_DIR}/LICENSE|${PROJECT_SOURCE_DIR}/NOTICE|${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md|${PROJECT_SOURCE_DIR}/README.md")
string(REPLACE ";" "|" DEPLOY_FLAGS_ARG "${DEPLOY_FLAGS}")
set(STAGE_ARGS
    "-DEXE=$<TARGET_FILE:LokiMetadata>" "-DRUNA=$<TARGET_FILE:runa>"
    "-DWINDEPLOYQT=$<TARGET_FILE:Qt6::windeployqt>" "-DDEPLOY_FLAGS=${DEPLOY_FLAGS_ARG}"
    "-DQMLDIR=${CMAKE_CURRENT_SOURCE_DIR}/qml" "-DRUNTIME=${RUNTIME_DLLS}" "-DDOCS=${DOCS}"
    "-DLICENSES=${PROJECT_SOURCE_DIR}/licenses")
add_custom_target(stage ALL
    COMMAND ${CMAKE_COMMAND} ${STAGE_ARGS} "-DDEST=${LOKI_INSTALL_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/update_install.cmake"
    DEPENDS LokiMetadata
    COMMENT "Updating the ready-to-run copy in '${LOKI_INSTALL_DIR}'"
    VERBATIM)

# "release_stage": the same, laid out fresh in build/release for packaging, so
# nothing left over from earlier builds can slip into a release.
set(RELEASE_STAGE_DIR "${CMAKE_BINARY_DIR}/release/Loki Metadata")
add_custom_target(release_stage
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/release"
    COMMAND ${CMAKE_COMMAND} ${STAGE_ARGS} "-DDEST=${RELEASE_STAGE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/update_install.cmake"
    DEPENDS LokiMetadata
    COMMENT "Laying out the release in '${RELEASE_STAGE_DIR}'"
    VERBATIM)
