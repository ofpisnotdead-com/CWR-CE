# ImGuiTestDiscovery.cmake — CTest discovery for ImGui Test Engine binaries
# Runs the test binary with --list-tests and registers each test case.
#
# Usage in CMakeLists.txt:
#   include(${CMAKE_SOURCE_DIR}/cmake/ImGuiTestDiscovery.cmake)
#   imgui_discover_tests(TargetName EXTRA_ARGS "fixtures_dir" ...)

function(imgui_discover_tests TARGET)
    cmake_parse_arguments(ARG "" "WORKING_DIRECTORY" "EXTRA_ARGS;PROPERTIES" ${ARGN})

    if(NOT ARG_WORKING_DIRECTORY)
        set(ARG_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    set(CTEST_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_tests.cmake")

    add_custom_command(
        TARGET ${TARGET} POST_BUILD
        BYPRODUCTS "${CTEST_FILE}"
        COMMAND "${CMAKE_COMMAND}"
            -D "TEST_TARGET=${TARGET}"
            -D "TEST_EXECUTABLE=$<TARGET_FILE:${TARGET}>"
            -D "TEST_WORKING_DIR=${ARG_WORKING_DIRECTORY}"
            -D "TEST_EXTRA_ARGS=${ARG_EXTRA_ARGS}"
            -D "TEST_PROPERTIES=${ARG_PROPERTIES}"
            -D "CTEST_FILE=${CTEST_FILE}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ImGuiTestAddTests.cmake"
        VERBATIM
    )

    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CTEST_FILE}")
endfunction()
