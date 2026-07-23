project(PoseidonStudio VERSION 1.0.0)

set(STUDIO_SOURCES
    main.cpp
    StudioApp.cpp
    StudioApp.hpp
    StudioConfig.cpp
    StudioConfig.hpp
)

if(WIN32)
    list(APPEND STUDIO_SOURCES ${CMAKE_SOURCE_DIR}/resources/icon-tool.rc)
endif()
add_executable(PoseidonStudio ${STUDIO_SOURCES})

target_compile_definitions(PoseidonStudio PRIVATE
    IMGUI_ENABLE_FREETYPE
    $<$<CONFIG:Debug>:_DEBUG>
)

find_package(SDL3 CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

target_link_libraries(PoseidonStudio PRIVATE
    Poseidon
    PoseidonOpenAL
    SDL3::SDL3
    Freetype::Freetype
    CLI11::CLI11
    imgui::imgui
)

if(WIN32)
    target_link_libraries(PoseidonStudio PRIVATE
        ws2_32 winmm version comctl32 ole32 shell32
        legacy_stdio_definitions.lib
    )
elseif(APPLE)
    target_link_libraries(PoseidonStudio PRIVATE pthread dl)
else()
    target_link_libraries(PoseidonStudio PRIVATE
        -Wl,--start-group
        Poseidon PoseidonOpenAL
        -Wl,--end-group
        pthread dl
    )
endif()

target_include_directories(PoseidonStudio PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)

include(${CMAKE_SOURCE_DIR}/cmake/DistCopy.cmake)
dist_copy(PoseidonStudio)
