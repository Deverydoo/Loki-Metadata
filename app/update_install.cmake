# Lays the app out as an install (run by the "stage" target with cmake -P).
# When the app is open from there its files are locked, but Windows lets a
# running file be renamed: the open one is moved aside to *.old and the new
# one put in place, so the next start runs the new build. Old files left by
# earlier runs are cleared once nothing holds them.
#
#   -DEXE -DRUNA -DDEST -DWINDEPLOYQT -DDEPLOY_FLAGS -DQMLDIR -DRUNTIME -DDOCS -DLICENSES
#   (lists separated by |)

file(MAKE_DIRECTORY "${DEST}")
file(GLOB leftovers "${DEST}/*.old")
foreach(old ${leftovers})
    file(REMOVE "${old}")   # quietly fails while still in use
endforeach()

set(moved_aside FALSE)
function(put source target)
    file(COPY_FILE "${source}" "${target}" RESULT result ONLY_IF_DIFFERENT)
    if(result EQUAL 0)
        return()
    endif()
    file(REMOVE "${target}.old")
    file(RENAME "${target}" "${target}.old" RESULT renamed)
    file(COPY_FILE "${source}" "${target}" RESULT result)
    if(NOT result EQUAL 0)
        message(WARNING "Couldn't update '${target}': ${result}")
    endif()
    set(moved_aside TRUE PARENT_SCOPE)
endfunction()

string(REPLACE "|" ";" runtime "${RUNTIME}")
string(REPLACE "|" ";" docs "${DOCS}")
foreach(f "${EXE}" "${RUNA}" ${runtime} ${docs})
    get_filename_component(name "${f}" NAME)
    put("${f}" "${DEST}/${name}")
endforeach()
file(MAKE_DIRECTORY "${DEST}/licenses")
file(GLOB license_texts "${LICENSES}/*.txt")
foreach(f ${license_texts})
    get_filename_component(name "${f}" NAME)
    put("${f}" "${DEST}/licenses/${name}")
endforeach()
if(moved_aside)
    message(STATUS "Loki Metadata is open from '${DEST}'. The new version is in place and runs next time it's opened.")
endif()

string(REPLACE "|" ";" flags "${DEPLOY_FLAGS}")
execute_process(
    COMMAND "${WINDEPLOYQT}" ${flags} --qmldir "${QMLDIR}" "${DEST}/LokiMetadata.exe"
    OUTPUT_QUIET ERROR_QUIET
    RESULT_VARIABLE deployed)
if(NOT deployed EQUAL 0)
    message(WARNING "windeployqt couldn't finish the copy in '${DEST}' (a Qt file may be in use). Close Loki Metadata and build again.")
endif()
