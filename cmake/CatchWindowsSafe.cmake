include(Catch)

set(
    _CATCH_DISCOVER_TESTS_SCRIPT
    "${CMAKE_SOURCE_DIR}/cmake/CatchAddWindowsSafeTests.cmake"
    CACHE INTERNAL "OFPR Catch2 discovery with Windows-safe CTest names" FORCE
)
include("${_CATCH_DISCOVER_TESTS_SCRIPT}")

option(OFPR_CTEST_PER_BINARY "Register each Catch2 binary as one CTest test instead of one per case" OFF)

function(ofpr_catch_discover_tests TARGET)
    if(OFPR_CTEST_PER_BINARY)
        cmake_parse_arguments(PARSE_ARGV 1 ARG "" "WORKING_DIRECTORY" "TEST_SPEC;PROPERTIES")
        if(NOT ARG_WORKING_DIRECTORY)
            set(ARG_WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        endif()
        add_test(NAME ${TARGET}
            COMMAND ${TARGET} --allow-running-no-tests ${ARG_TEST_SPEC}
            WORKING_DIRECTORY ${ARG_WORKING_DIRECTORY}
        )
        if(ARG_PROPERTIES)
            set_tests_properties(${TARGET} PROPERTIES ${ARG_PROPERTIES})
        endif()
        return()
    endif()
    catch_discover_tests(${TARGET}
        TEST_PREFIX "${TARGET} - "
        DISCOVERY_MODE PRE_TEST
        # A Catch2 test that SKIP()s (e.g. audio tests when no OpenAL device is
        # available on a headless CI runner) runs no assertions and otherwise exits 4,
        # which ctest reports as a failure. Treat a skipped/no-assertion run as a pass;
        # real assertion failures still return their own non-zero code.
        EXTRA_ARGS --allow-running-no-tests
        ${ARGN}
    )
endfunction()
