# PoseidonServer - Dedicated server executable

set(SERVER_SOURCES
    ServerMain.cpp
    ServerApplication.cpp
)

if(WIN32)
    list(APPEND SERVER_SOURCES resource.rc)
endif()

add_executable(PoseidonServer ${SERVER_SOURCES})

# Executable-specific compile definitions
target_compile_definitions(PoseidonServer PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>     # Enable debug mode (affects Assert/Fail macros in debugLog.hpp)
)

# Match MSVC compiler settings
if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    target_compile_options(PoseidonServer PRIVATE
        /W3                    # Warning level 3
        /GR                    # Enable RTTI
        $<$<CONFIG:Debug>:/Od>     # Disable optimization (Debug only)
        $<$<CONFIG:Debug>:/MDd>    # Runtime library: MultiThreadedDebugDLL (Debug only)
    )
    
    # Disable SafeSEH for compatibility with external .obj files.
    target_link_options(PoseidonServer PRIVATE /SAFESEH:NO)
endif()

# Include paths: only need root and Poseidon (all libs/externals handled by Poseidon.lib)
target_include_directories(PoseidonServer PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    # resource.rc references resrc1.h/resource.h (Core) and versionNo.h
    # (Foundation/Platform) by bare name; expose those dirs to the RC compiler only.
    $<$<COMPILE_LANGUAGE:RC>:${CMAKE_SOURCE_DIR}/engine/Poseidon/Core>
    $<$<COMPILE_LANGUAGE:RC>:${CMAKE_SOURCE_DIR}/engine/Poseidon/Foundation/Platform>
)

# Link against libraries (Poseidon transitively brings in folded support/runtime code and externals)
if(WIN32)
    target_link_libraries(PoseidonServer PRIVATE
        GameBase
        Poseidon
        # Windows system libraries
        winmm.lib
        ws2_32.lib
        legacy_stdio_definitions.lib
    )
elseif(APPLE)
    target_link_libraries(PoseidonServer PRIVATE
        GameBase
        Poseidon
        pthread
        dl
    )
else()
    target_link_libraries(PoseidonServer PRIVATE
        -Wl,--start-group
        GameBase
        Poseidon
        -Wl,--end-group
        pthread
        dl
    )
endif()

# Set subsystem to Console (Windows only)
if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    set_target_properties(PoseidonServer PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE /IGNORE:4099"
    )
endif()

include(${CMAKE_SOURCE_DIR}/cmake/DistCopy.cmake)
dist_copy(PoseidonServer)
