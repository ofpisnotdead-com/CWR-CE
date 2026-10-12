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

include(${CMAKE_SOURCE_DIR}/cmake/DistCopy.cmake)
dist_copy(PoseidonGame)
