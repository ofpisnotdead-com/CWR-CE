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
        OpenAL::OpenAL
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
    set(POSEIDON_IOS_ASSET_CATALOG "${CMAKE_CURRENT_SOURCE_DIR}/Assets.xcassets")
    set_source_files_properties("${POSEIDON_IOS_ASSET_CATALOG}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )

    # Settings.bundle: adds the "Reset Game Data on Next Launch" toggle to the
    # system Settings app. Bundled unconditionally (not gated on
    # POSEIDON_IOS_BUNDLE_GAME_DATA below) so Clear-Data-via-Settings works in
    # dev/simulator builds too, not just the distributable configuration.
    set(POSEIDON_IOS_SETTINGS_BUNDLE "${CMAKE_CURRENT_SOURCE_DIR}/Settings.bundle")
    set_source_files_properties("${POSEIDON_IOS_SETTINGS_BUNDLE}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "."
    )

    # iOS executables are always app bundles -- give it a real bundle ID
    # and name so it's installable (CMake leaves these blank by default).
    # Custom Info.plist (vs. CMake's built-in default template) so we can
    # lock orientation to landscape -- this engine has no portrait UI at
    # all (desktop-style HUD/menus laid out for landscape).
    #
    # CFBundleVersion comes from MACOSX_BUNDLE_BUNDLE_VERSION, substituted by
    # CMake at *configure* time (not an Xcode build-setting substitution like
    # a hand-maintained Info.plist would use) -- so a TestFlight upload's
    # unique build number has to be injected via -DPOSEIDON_IOS_BUILD_NUMBER=
    # at configure time (see fastlane/Fastfile), not via xcodebuild xcargs.
    if(DEFINED POSEIDON_IOS_BUILD_NUMBER AND NOT POSEIDON_IOS_BUILD_NUMBER STREQUAL "")
        set(POSEIDON_IOS_BUNDLE_VERSION "${POSEIDON_IOS_BUILD_NUMBER}")
    else()
        set(POSEIDON_IOS_BUNDLE_VERSION "1.0")
    endif()
    set_target_properties(PoseidonGame PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info-iOS.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.mcardle.cwr"
        MACOSX_BUNDLE_BUNDLE_NAME "PoseidonGame"
        MACOSX_BUNDLE_BUNDLE_VERSION "${POSEIDON_IOS_BUNDLE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.mcardle.cwr"
        # iCloud entitlement for CloudSync (engine/Poseidon/Core/CloudSync) --
        # requires the "iCloud" capability + container enabled on this App ID
        # in the Apple Developer portal, and the provisioning profile
        # regenerated to include it, before this has any runtime effect.
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/PoseidonGame.entitlements"
        # For Xcode archives, only the app bundle should be installable.
        # The project-wide App Store preset defaults generated targets to
        # SKIP_INSTALL=YES; this target opts back in and gives Xcode the
        # standard iOS app install directory so the archive is shaped as an
        # App Store archive under Products/Applications instead of a Generic
        # archive under UninstalledProducts.
        XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
        XCODE_ATTRIBUTE_INSTALL_PATH "/Applications"
        # CMake's Xcode generator leaves this unset, which does not default
        # to Xcode's own "dwarf-with-dsym" -- without it, archive builds
        # produce an empty dSYMs/ folder, so Apple never gets a dSYM and
        # TestFlight crashes can't be symbolicated (Organizer's
        # "Symbolicate" button just errors with nothing to fetch).
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
        # Release defaults GCC_GENERATE_DEBUGGING_SYMBOLS to NO, so object
        # files carry no DWARF at all -- with nothing to extract, Xcode
        # silently skips the "Generate dSYM File" phase entirely even with
        # DEBUG_INFORMATION_FORMAT set above (this is why the dSYMs/ folder
        # stayed empty even after adding that attribute). This only enables
        # emitting -g info at compile time; COPY_PHASE_STRIP/
        # STRIP_INSTALLED_PRODUCT (both YES here) still strip the shipped
        # binary itself, so this doesn't bloat or de-optimize the archive.
        XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES"
    )
    target_sources(PoseidonGame PRIVATE
        "${POSEIDON_IOS_ASSET_CATALOG}"
        "${POSEIDON_IOS_SETTINGS_BUNDLE}"
    )
endif()

if(POSEIDON_TARGET_IOS AND POSEIDON_IOS_BUNDLE_GAME_DATA)
    # Bundle the game's data files (DTA/AddOns/fonts/etc.) straight into the
    # .app, preserving their relative directory layout, so the app is
    # self-contained on install -- no manual `devicectl device copy` step
    # needed. ~420 files, ~700MB; fine for Xcode's resource-copy phase.
    # iOS .app bundles are flat (no Contents/Resources nesting like macOS),
    # and [NSBundle mainBundle].resourcePath / SDL_GetBasePath() return the
    # bundle root -- so these land exactly where the runtime expects them,
    # with no extra path segment to account for (see WinMain.cpp's iOS
    # branch, which chdir()s there when no explicit -C is given).
    #
    # OFF (POSEIDON_IOS_BUNDLE_GAME_DATA=OFF) is the distributable/App Store
    # configuration -- this licensed data can't ship in the bundle there, so
    # WinMain.cpp runs the on-device acquisition gate instead.
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
