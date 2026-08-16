include(Catch)

set(
    _CATCH_DISCOVER_TESTS_SCRIPT
    "${CMAKE_SOURCE_DIR}/cmake/CatchAddWindowsSafeTests.cmake"
    CACHE INTERNAL "OFPR Catch2 discovery with Windows-safe CTest names" FORCE
)
include("${_CATCH_DISCOVER_TESTS_SCRIPT}")

function(ofpr_catch_discover_tests TARGET)
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
