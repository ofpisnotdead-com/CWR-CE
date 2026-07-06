set(TETRIS_SOURCES
    WinMain.cpp
    TetrisApplication.cpp
    TetrisGame.cpp
    TetrisNotebookUI.cpp
    NotebookScene.cpp
    # TetrisApplication subclasses GameApplication; compile the base
    # implementation in-tree because Game is an executable target.
    ${CMAKE_SOURCE_DIR}/apps/cwr/Game/GameApplication.cpp
    # Hybrid-GPU export hints must be compiled into the executable;
    # dllexport keeps them as link roots under /OPT:REF.
    ${CMAKE_SOURCE_DIR}/apps/cwr/Game/HighPerformanceGpuHint.cpp
)

if(WIN32)
    list(APPEND TETRIS_SOURCES ${CMAKE_SOURCE_DIR}/resources/icon-tool.rc)
    add_executable(PoseidonTetris WIN32 ${TETRIS_SOURCES})
else()
    add_executable(PoseidonTetris ${TETRIS_SOURCES})
endif()

target_compile_definitions(PoseidonTetris PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
if(WIN32)
    target_compile_definitions(PoseidonTetris PRIVATE _WINDOWS)
endif()

if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    target_compile_options(PoseidonTetris PRIVATE
        /W3 /GR
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/MDd>
        # Function- and data-level COMDATs let the linker drop unused
        # GameApplication.cpp and engine-library symbols.
        /Gy /Gw
    )
    target_link_options(PoseidonTetris PRIVATE
        /SAFESEH:NO
        # Drop unreachable COMDATs and merge identical functions; the
        # Tetris loop uses only a narrow slice of GameApplication.cpp.
        /OPT:REF
        /OPT:ICF
    )
endif()

target_include_directories(PoseidonTetris PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/apps/cwr/Game
)

if(WIN32)
    target_link_libraries(PoseidonTetris PRIVATE
        GameBase Poseidon PoseidonGL33 PoseidonOpenAL
        winmm.lib version.lib ws2_32.lib legacy_stdio_definitions.lib
    )
elseif(APPLE)
    # See apps/cwr/Game/CMakeLists.txt: Apple's linker doesn't support/need
    # --start-group/--end-group, and GameApplication.cpp (compiled into this
    # target) needs PoseidonMTL linked for RegisterMetalGraphicsBackend().
    target_link_libraries(PoseidonTetris PRIVATE
        GameBase PoseidonGL33 PoseidonMTL Poseidon PoseidonOpenAL
        pthread dl
    )
else()
    target_link_libraries(PoseidonTetris PRIVATE
        -Wl,--start-group GameBase PoseidonGL33 Poseidon PoseidonOpenAL -Wl,--end-group
        pthread dl
    )
endif()

if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    set_target_properties(PoseidonTetris PROPERTIES LINK_FLAGS "/IGNORE:4099")
endif()

set_target_properties(PoseidonTetris PROPERTIES OUTPUT_NAME "PoseidonTetris")

include(${CMAKE_SOURCE_DIR}/cmake/DistCopy.cmake)
dist_copy(PoseidonTetris)
