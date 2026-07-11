# PoseidonGameDemo - Demo game client (Single Missions + Editor only)

set(GAMEDEMO_SOURCES
    WinMain.cpp
    GameDemoApplication.cpp
    # Reuse GameApplication from Game app (base class)
    ${CMAKE_SOURCE_DIR}/apps/cwr/Game/GameApplication.cpp
    # Hybrid-GPU export hints — must be compiled into the EXE
    ${CMAKE_SOURCE_DIR}/apps/cwr/Game/HighPerformanceGpuHint.cpp
)


if(WIN32)
    list(APPEND GAMEDEMO_SOURCES ${CMAKE_SOURCE_DIR}/resources/icon-demo.rc)
    add_executable(PoseidonGameDemo WIN32 ${GAMEDEMO_SOURCES})
else()
    add_executable(PoseidonGameDemo ${GAMEDEMO_SOURCES})
endif()

target_compile_definitions(PoseidonGameDemo PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
if(WIN32)
    target_compile_definitions(PoseidonGameDemo PRIVATE _WINDOWS)
endif()

if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    target_compile_options(PoseidonGameDemo PRIVATE
        /W3 /GR
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/MDd>
    )
    target_link_options(PoseidonGameDemo PRIVATE /SAFESEH:NO)
endif()

target_include_directories(PoseidonGameDemo PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    # GameApplication.hpp lives in apps/cwr/Game
    ${CMAKE_SOURCE_DIR}/apps/cwr/Game
)

if(WIN32)
    target_link_libraries(PoseidonGameDemo PRIVATE
        GameBase Poseidon PoseidonGL33 PoseidonOpenAL
        winmm.lib version.lib ws2_32.lib legacy_stdio_definitions.lib
    )
elseif(POSEIDON_TARGET_IOS)
    # iOS has no desktop OpenGL (no PoseidonGL33 target built for this
    # platform) -- Metal is the only renderer.
    target_link_libraries(PoseidonGameDemo PRIVATE
        GameBase PoseidonMTL Poseidon PoseidonOpenAL
        pthread dl
    )
elseif(APPLE)
    # Apple's linker has no GNU --start-group/--end-group equivalent (and
    # doesn't need one — it resolves the same circular static-lib
    # dependencies without help). GameApplication.cpp (compiled into this
    # target) calls RegisterMetalGraphicsBackend() under __APPLE__, so
    # PoseidonMTL must be linked too.
    target_link_libraries(PoseidonGameDemo PRIVATE
        GameBase PoseidonGL33 PoseidonMTL Poseidon PoseidonOpenAL
        pthread dl
    )
else()
    target_link_libraries(PoseidonGameDemo PRIVATE
        -Wl,--start-group GameBase PoseidonGL33 Poseidon PoseidonOpenAL -Wl,--end-group
        pthread dl
    )
endif()

if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    set_target_properties(PoseidonGameDemo PROPERTIES LINK_FLAGS "/IGNORE:4099")
endif()

set_target_properties(PoseidonGameDemo PROPERTIES OUTPUT_NAME "PoseidonGameDemo")

include(${CMAKE_SOURCE_DIR}/cmake/DistCopy.cmake)
dist_copy(PoseidonGameDemo)
