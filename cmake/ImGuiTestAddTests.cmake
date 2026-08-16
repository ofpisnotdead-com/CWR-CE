# ImGuiTestAddTests.cmake — post-build script that discovers tests
# Called by ImGuiTestDiscovery.cmake after building the test binary.

# Run the binary with --list-tests to get test names
execute_process(
    COMMAND "${TEST_EXECUTABLE}" --list-tests
    WORKING_DIRECTORY "${TEST_WORKING_DIR}"
    OUTPUT_VARIABLE test_output
    RESULT_VARIABLE result
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT result EQUAL 0)
    file(WRITE "${CTEST_FILE}"
        "add_test(\"${TEST_TARGET}\" \"${TEST_EXECUTABLE}\" ${TEST_EXTRA_ARGS})\n"
        "set_tests_properties(\"${TEST_TARGET}\" PROPERTIES WORKING_DIRECTORY \"${TEST_WORKING_DIR}\")\n"
    )
    return()
endif()

# Parse output: one test per line as "category/name"
string(REPLACE "\n" ";" test_list "${test_output}")

file(WRITE "${CTEST_FILE}" "")

foreach(test_line IN LISTS test_list)
    if(test_line STREQUAL "")
        continue()
    endif()

    # CTest display name: "Target - category - name"
    string(REPLACE "/" " - " test_name "${test_line}")
    string(REGEX REPLACE "^[^/]*/" "" test_filter "${test_line}")

    # ImGui Test Engine filters category and name independently, so use the
    # exact (suite-unique) test name rather than the category/name display path.
    file(APPEND "${CTEST_FILE}"
        "add_test(\"${TEST_TARGET} - ${test_name}\" \"${TEST_EXECUTABLE}\" ${TEST_EXTRA_ARGS} \"--filter\" \"^${test_filter}$\")\n"
        "set_tests_properties(\"${TEST_TARGET} - ${test_name}\" PROPERTIES WORKING_DIRECTORY \"${TEST_WORKING_DIR}\")\n"
    )

    if(TEST_PROPERTIES)
        file(APPEND "${CTEST_FILE}"
            "set_tests_properties(\"${TEST_TARGET} - ${test_name}\" PROPERTIES ${TEST_PROPERTIES})\n"
        )
    endif()
endforeach()
