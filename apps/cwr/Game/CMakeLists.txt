# PoseidonGame - Game client executable

set(GAME_SOURCES
    WinMain.cpp
    GameApplication.cpp
    HighPerformanceGpuHint.cpp
)


if(WIN32)
    list(APPEND GAME_SOURCES resource.rc)
    add_executable(PoseidonGame WIN32 ${GAME_SOURCES})
else()
    add_executable(PoseidonGame ${GAME_SOURCES})
endif()

# Executable-specific compile definitions
target_compile_definitions(PoseidonGame PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>     # Enable debug mode (affects Assert/Fail macros in debugLog.hpp)
)
if(WIN32)
    target_compile_definitions(PoseidonGame PRIVATE _WINDOWS)
endif()

# Match MSVC compiler settings
if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    target_compile_options(PoseidonGame PRIVATE
        /W3                    # Warning level 3
        /GR                    # Enable RTTI
        $<$<CONFIG:Debug>:/Od>     # Disable optimization (Debug only)
        $<$<CONFIG:Debug>:/MDd>    # Runtime library: MultiThreadedDebugDLL (Debug only)
    )
    
    # Disable SafeSEH for compatibility with external .obj files.
    target_link_options(PoseidonGame PRIVATE /SAFESEH:NO)
endif()

# Include paths: only need root and Poseidon (all libs/externals handled by Poseidon.lib)
target_include_directories(PoseidonGame PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    # resource.rc references resrc1.h/resource.h (Core) and versionNo.h
    # (Foundation/Platform) by bare name; expose those dirs to the RC compiler only.
    $<$<COMPILE_LANGUAGE:RC>:${CMAKE_SOURCE_DIR}/engine/Poseidon/Core>
    $<$<COMPILE_LANGUAGE:RC>:${CMAKE_SOURCE_DIR}/engine/Poseidon/Foundation/Platform>
)

# Link against libraries (Poseidon transitively brings in folded support/runtime code and externals)
if(WIN32)
    target_link_libraries(PoseidonGame PRIVATE
        GameBase
        Poseidon
        PoseidonGL33
        PoseidonOpenAL
        # Windows system libraries
        winmm.lib
        version.lib
        ws2_32.lib
        legacy_stdio_definitions.lib
    )
elseif(POSEIDON_TARGET_IOS)
    # iOS has no desktop OpenGL (no PoseidonGL33 target built for this
    # platform) -- Metal is the only renderer.
    target_link_libraries(PoseidonGame PRIVATE
        GameBase
        PoseidonMTL
        Poseidon
        PoseidonOpenAL
        pthread
        dl
    )
elseif(APPLE)
    # Apple's linker has no GNU --start-group/--end-group equivalent (and
    # doesn't need one — it resolves the same circular static-lib
    # dependencies without help).
    target_link_libraries(PoseidonGame PRIVATE
        GameBase
        PoseidonGL33
        PoseidonMTL
        Poseidon
        PoseidonOpenAL
        pthread
        dl
    )
else()
    target_link_libraries(PoseidonGame PRIVATE
        -Wl,--start-group
        GameBase
        PoseidonGL33
        Poseidon
        PoseidonOpenAL
        -Wl,--end-group
        pthread
        dl
    )
endif()

# Use WINDOWS subsystem (WIN32 flag); AttachConsole at runtime handles CLI output
if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    set_target_properties(PoseidonGame PROPERTIES
        LINK_FLAGS "/IGNORE:4099"
    )
endif()

set_target_properties(PoseidonGame PROPERTIES
    OUTPUT_NAME "PoseidonGame"
)

if(POSEIDON_TARGET_IOS)
    # iOS executables are always app bundles -- give it a real bundle ID
    # and name so it's installable (CMake leaves these blank by default).
    # Custom Info.plist (vs. CMake's built-in default template) so we can
    # lock orientation to landscape -- this engine has no portrait UI at
    # all (desktop-style HUD/menus laid out for landscape).
    set_target_properties(PoseidonGame PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info-iOS.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.mcardle.cwr.poseidongame"
        MACOSX_BUNDLE_BUNDLE_NAME "PoseidonGame"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
endif()

if(POSEIDON_TARGET_IOS)
    # Bundle the game's data files (DTA/AddOns/fonts/etc.) straight into the
    # .app, preserving their relative directory layout, so the app is
    # self-contained on install -- no manual `devicectl device copy` step
    # needed. ~420 files, ~700MB; fine for Xcode's resource-copy phase.
    # iOS .app bundles are flat (no Contents/Resources nesting like macOS),
    # and [NSBundle mainBundle].resourcePath / SDL_GetBasePath() return the
    # bundle root -- so these land exactly where the runtime expects them,
    # with no extra path segment to account for (see WinMain.cpp's iOS
    # branch, which chdir()s there when no explicit -C is given).
    file(GLOB_RECURSE POSEIDON_GAME_ASSET_FILES
        RELATIVE "${CMAKE_SOURCE_DIR}/packages/Combined"
        "${CMAKE_SOURCE_DIR}/packages/Combined/*"
    )
    set(POSEIDON_GAME_ASSET_ABS_PATHS "")
    foreach(_asset_rel ${POSEIDON_GAME_ASSET_FILES})
        set(_asset_abs "${CMAKE_SOURCE_DIR}/packages/Combined/${_asset_rel}")
        get_filename_component(_asset_reldir "${_asset_rel}" DIRECTORY)
        if(NOT _asset_reldir)
            set(_asset_reldir ".")
        endif()
        set_source_files_properties("${_asset_abs}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "${_asset_reldir}"
            HEADER_FILE_ONLY TRUE
        )
        list(APPEND POSEIDON_GAME_ASSET_ABS_PATHS "${_asset_abs}")
    endforeach()
    target_sources(PoseidonGame PRIVATE ${POSEIDON_GAME_ASSET_ABS_PATHS})
    source_group("Resources" FILES ${POSEIDON_GAME_ASSET_ABS_PATHS})
endif()

if(NOT POSEIDON_TARGET_IOS)
    # dist_copy() flat-copies the executable out of its build tree --
    # meaningless for an iOS .app bundle (and its custom-command script
    # doesn't resolve Xcode's per-config/platform path variables anyway,
    # since this is the one target built with the Xcode generator).
    include(${CMAKE_SOURCE_DIR}/cmake/DistCopy.cmake)
    dist_copy(PoseidonGame)
endif()
