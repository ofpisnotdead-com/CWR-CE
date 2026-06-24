# PoseidonStudioTests — Studio asset-editor UI suite driven by the Dear ImGui
# Test Engine. Runs StudioApp::render() in a headless SDL+ImGui loop against the
# synthetic fixtures under tests/fixtures/studio. Requires the imgui "test-engine"
# feature (see vcpkg.json).
project(PoseidonStudioTests)

set(STUDIO_TEST_SOURCES
    studio_ui_tests.cpp
    ${CMAKE_SOURCE_DIR}/apps/tools/Studio/StudioApp.cpp
    ${CMAKE_SOURCE_DIR}/apps/tools/Studio/StudioConfig.cpp
)

add_executable(PoseidonStudioTests ${STUDIO_TEST_SOURCES})

target_compile_definitions(PoseidonStudioTests PRIVATE
    IMGUI_ENABLE_TEST_ENGINE
    IMGUI_ENABLE_FREETYPE
    $<$<CONFIG:Debug>:_DEBUG>
)

find_package(SDL3 CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

target_link_libraries(PoseidonStudioTests PRIVATE
    Poseidon
    PoseidonOpenAL
    SDL3::SDL3
    Freetype::Freetype
    CLI11::CLI11
    imgui::imgui
)

if(WIN32)
    target_link_libraries(PoseidonStudioTests PRIVATE
        ws2_32 winmm version comctl32 ole32 shell32
        legacy_stdio_definitions.lib
    )
else()
    target_link_libraries(PoseidonStudioTests PRIVATE
        -Wl,--start-group
        Poseidon PoseidonOpenAL
        -Wl,--end-group
        pthread dl
    )
endif()

target_include_directories(PoseidonStudioTests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/apps/tools/Studio
)

# Copy fixture files next to the binary so the suite can run from its build dir.
set(FIXTURES_SRC ${CMAKE_SOURCE_DIR}/tests/fixtures/studio)
set(FIXTURES_DST ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
add_custom_command(TARGET PoseidonStudioTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${FIXTURES_SRC} ${FIXTURES_DST}
)

# Discover individual ImGui Test Engine test cases for CTest.
include(${CMAKE_SOURCE_DIR}/cmake/ImGuiTestDiscovery.cmake)
imgui_discover_tests(PoseidonStudioTests
    EXTRA_ARGS "${FIXTURES_DST}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
