cmake_minimum_required(VERSION 3.25)

project(PoseidonTools
    VERSION 1.0.0
    DESCRIPTION "Command-line tools for Operation Flashpoint model formats"
    LANGUAGES CXX
)
find_package(CLI11 CONFIG REQUIRED)
set(TOOLS_RC_SOURCES "")
if(WIN32)
    set(TOOLS_RC_SOURCES ${CMAKE_SOURCE_DIR}/resources/icon-tool.rc)
endif()
add_executable(PoseidonTools
    ${TOOLS_RC_SOURCES}
    main.cpp
    SDLPreview.cpp
    SDLPreview.hpp
    commands/VersionCommand.cpp
    commands/VersionCommand.hpp
    commands/ModelCommand.cpp
    commands/ModelCommand.hpp
    commands/ImageCommand.cpp
    commands/ImageCommand.hpp
    commands/PboCommand.cpp
    commands/PboCommand.hpp
    commands/TerrainCommand.cpp
    commands/TerrainCommand.hpp
    commands/FontCommand.cpp
    commands/FontCommand.hpp
    commands/SoundCommand.cpp
    commands/SoundCommand.hpp
    commands/ConfigCommand.cpp
    commands/ConfigCommand.hpp
    commands/StringtableCommand.cpp
    commands/StringtableCommand.hpp
    commands/LintCommand.cpp
    commands/LintCommand.hpp
    commands/ScanCommand.cpp
    commands/ScanCommand.hpp
    commands/MineCommand.cpp
    commands/MineCommand.hpp
    commands/InputCommand.cpp
    commands/InputCommand.hpp
    commands/VonCommand.cpp
    commands/VonCommand.hpp
    commands/ShadowCommand.cpp
    commands/ShadowCommand.hpp
)

find_package(SDL3 CONFIG REQUIRED)

if(WIN32)
    target_link_libraries(PoseidonTools
        PRIVATE
            Poseidon
            PoseidonOpenAL
            CLI11::CLI11
            SDL3::SDL3
            winmm
    )
elseif(APPLE)
    target_link_libraries(PoseidonTools
        PRIVATE
            Poseidon
            PoseidonOpenAL
            CLI11::CLI11
            SDL3::SDL3
    )
else()
    target_link_libraries(PoseidonTools
        PRIVATE
            -Wl,--start-group
            Poseidon
            PoseidonOpenAL
            -Wl,--end-group
            CLI11::CLI11
            SDL3::SDL3
    )
endif()

target_include_directories(PoseidonTools
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(PoseidonTools PRIVATE cxx_std_20)
install(TARGETS PoseidonTools
    RUNTIME DESTINATION bin
)

include(${CMAKE_SOURCE_DIR}/cmake/DistCopy.cmake)
dist_copy(PoseidonTools)
