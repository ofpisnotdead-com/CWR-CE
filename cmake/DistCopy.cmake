# DistCopy.cmake — Helper to copy build artifacts to dist/<preset>/
#
# Usage:
#   include(${CMAKE_SOURCE_DIR}/cmake/DistCopy.cmake)
#   dist_copy(PoseidonGame)                              # copy binary + PDB
#   dist_copy(TcPbo RENAME pbo${WCX_SUFFIX})             # copy with rename
#   dist_copy(TcPbo EXTRA pluginst.inf)                  # copy extra file from source dir

function(dist_copy TARGET)
    cmake_parse_arguments(ARG "" "RENAME" "EXTRA" ${ARGN})

    if(ARG_RENAME)
        set(_dst "${DIST_DIR}/${ARG_RENAME}")
    else()
        set(_dst "${DIST_DIR}/$<TARGET_FILE_NAME:${TARGET}>")
    endif()

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${TARGET}> ${_dst}
        COMMENT "Copying ${TARGET} to ${DIST_DIR}"
        VERBATIM
    )

    # Copy PDB on Windows debug builds
    if(WIN32 AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        get_target_property(_type ${TARGET} TYPE)
        if(_type STREQUAL "EXECUTABLE" OR _type STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_PDB_FILE:${TARGET}> ${DIST_DIR}/
                VERBATIM
            )
        endif()
    endif()

    # Copy runtime DLLs (e.g., OpenAL32.dll/libopenal.dylib — LGPL dynamic linkage)
    if((WIN32 OR APPLE) AND TARGET OpenAL::OpenAL)
        get_target_property(_openal_lib OpenAL::OpenAL IMPORTED_LOCATION)
        if(NOT _openal_lib)
            get_target_property(_openal_lib OpenAL::OpenAL IMPORTED_LOCATION_RELEASE)
        endif()
        if(_openal_lib AND ((WIN32 AND _openal_lib MATCHES "\\.dll$") OR (APPLE AND _openal_lib MATCHES "\\.dylib$")))
            if(WIN32)
                set(_openal_dst "${DIST_DIR}")
            else()
                set(_openal_dst "${DIST_DIR}/libopenal.dylib")
            endif()
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_openal_lib}" "${_openal_dst}"
                VERBATIM
            )
            unset(_openal_dst)
        endif()
        unset(_openal_lib)
    endif()

    # Copy extra files from the target's source directory
    foreach(_extra ${ARG_EXTRA})
        get_filename_component(_name "${_extra}" NAME)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/${_extra} ${DIST_DIR}/${_name}
            VERBATIM
        )
    endforeach()
endfunction()
