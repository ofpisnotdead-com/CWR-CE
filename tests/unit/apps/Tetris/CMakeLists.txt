project(PoseidonTetrisTests)

set(TETRIS_TEST_SOURCES
    test_tetris_app.cpp
    test_tetris_game.cpp
    ${CMAKE_SOURCE_DIR}/apps/tetris/Tetris/TetrisApplication.cpp
    ${CMAKE_SOURCE_DIR}/apps/tetris/Tetris/TetrisGame.cpp
    ${CMAKE_SOURCE_DIR}/apps/tetris/Tetris/TetrisNotebookUI.cpp
    ${CMAKE_SOURCE_DIR}/apps/tetris/Tetris/NotebookScene.cpp
    ${CMAKE_SOURCE_DIR}/apps/cwr/Game/GameApplication.cpp
)

add_executable(PoseidonTetrisTests ${TETRIS_TEST_SOURCES})

target_compile_definitions(PoseidonTetrisTests PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)

find_package(Catch2 3 CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    find_package(Freetype CONFIG REQUIRED)
else()
    find_package(freetype CONFIG REQUIRED)
endif()
find_package(CLI11 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

target_link_libraries(PoseidonTetrisTests PRIVATE
    GameBase PoseidonGL33 Poseidon PoseidonOpenAL
    SDL3::SDL3 Freetype::Freetype CLI11::CLI11 imgui::imgui
    Catch2::Catch2WithMain
)

if(WIN32)
    target_link_libraries(PoseidonTetrisTests PRIVATE
        ws2_32 winmm version comctl32 ole32 shell32
        legacy_stdio_definitions.lib
    )
endif()

target_include_directories(PoseidonTetrisTests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/apps/tetris/Tetris
    ${CMAKE_SOURCE_DIR}/apps/cwr/Game
)

include(${CMAKE_SOURCE_DIR}/cmake/CatchWindowsSafe.cmake)
ofpr_catch_discover_tests(PoseidonTetrisTests)
