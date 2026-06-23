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

    get_target_property(_type ${TARGET} TYPE)

    if(WIN32)
        if(_type STREQUAL "EXECUTABLE" OR _type STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_PDB_FILE:${TARGET}> ${DIST_DIR}/
                VERBATIM
            )
        endif()
    endif()

    if(UNIX AND NOT APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND CMAKE_OBJCOPY AND CMAKE_STRIP)
        if(_type STREQUAL "EXECUTABLE" OR _type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "MODULE_LIBRARY")
            set(_debug_dst "${_dst}.debug")
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:${TARGET}> ${_debug_dst}
                COMMAND ${CMAKE_STRIP} --strip-debug --strip-unneeded ${_dst}
                COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=${_debug_dst} ${_dst}
                COMMENT "Writing debug symbols and stripping ${TARGET} in ${DIST_DIR}"
                VERBATIM
            )
        endif()
    endif()

    # Copy runtime DLLs (e.g., OpenAL32.dll — LGPL dynamic linkage)
    set(_copy_openal_runtime OFF)
    get_target_property(_link_libraries ${TARGET} LINK_LIBRARIES)
    if(_link_libraries)
        foreach(_link_library IN LISTS _link_libraries)
            if(_link_library STREQUAL "PoseidonOpenAL" OR _link_library STREQUAL "OpenAL::OpenAL")
                set(_copy_openal_runtime ON)
            endif()
        endforeach()
    endif()
    if(WIN32 AND TARGET OpenAL::OpenAL AND _copy_openal_runtime)
        get_target_property(_openal_dll OpenAL::OpenAL IMPORTED_LOCATION)
        if(NOT _openal_dll)
            get_target_property(_openal_dll OpenAL::OpenAL IMPORTED_LOCATION_RELEASE)
        endif()
        if(_openal_dll AND _openal_dll MATCHES "\\.dll$")
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_openal_dll}" "${DIST_DIR}"
                VERBATIM
            )

            get_filename_component(_openal_bin_dir "${_openal_dll}" DIRECTORY)
            get_filename_component(_openal_triplet_dir "${_openal_bin_dir}" DIRECTORY)
            set(_openal_copyright "${_openal_triplet_dir}/share/openal-soft/copyright")
            if(EXISTS "${_openal_copyright}")
                add_custom_command(TARGET ${TARGET} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_openal_copyright}" "${DIST_DIR}/OpenAL-Soft.LICENSE.txt"
                    VERBATIM
                )
            endif()
        endif()
        unset(_openal_dll)
        unset(_openal_bin_dir)
        unset(_openal_triplet_dir)
        unset(_openal_copyright)
    endif()
    unset(_copy_openal_runtime)
    unset(_link_libraries)
    unset(_link_library)

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
