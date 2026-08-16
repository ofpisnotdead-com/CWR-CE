set(SERVER_TEST_SOURCES
    test_main.cpp
    test_harness.cpp
    test_simulate.cpp
)

add_executable(PoseidonServerTests ${SERVER_TEST_SOURCES})

target_link_libraries(PoseidonServerTests PRIVATE
    Poseidon
    Catch2::Catch2
)
if(WIN32)
    target_link_libraries(PoseidonServerTests PRIVATE
        ws2_32
        winmm
        version
        comctl32
        legacy_stdio_definitions.lib
    )
elseif(APPLE)
    target_link_libraries(PoseidonServerTests PRIVATE
        Poseidon
        pthread dl
    )
else()
    target_link_libraries(PoseidonServerTests PRIVATE
        -Wl,--start-group
        Poseidon
        -Wl,--end-group
        pthread dl
    )
endif()

# Disable SafeSEH for compatibility with legacy external .obj files
if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    target_link_options(PoseidonServerTests PRIVATE /SAFESEH:NO)
endif()

target_compile_definitions(PoseidonServerTests PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    POSEIDON_TEST_BUILD
)

target_include_directories(PoseidonServerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include(${CMAKE_SOURCE_DIR}/cmake/CatchWindowsSafe.cmake)
ofpr_catch_discover_tests(PoseidonServerTests
    PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/ubsan-suppressions.txt;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan-suppressions.txt"
)
