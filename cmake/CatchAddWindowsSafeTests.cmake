# Based on Catch2's CatchAddTests.cmake, with CTest display names sanitized for
# Windows-safe, unique CTest registration. The original Catch2 test name is
# still passed to the executable unchanged.

function(add_command NAME)
  set(_args "")
  math(EXPR _last_arg ${ARGC}-1)
  foreach(_n RANGE 1 ${_last_arg})
    set(_arg "${ARGV${_n}}")
    if(_arg MATCHES "[^-./:a-zA-Z0-9_]")
      set(_args "${_args} [==[${_arg}]==]")
    else()
      set(_args "${_args} ${_arg}")
    endif()
  endforeach()
  set(script "${script}${NAME}(${_args})\n" PARENT_SCOPE)
endfunction()

function(make_windows_safe_ctest_name OUT_VAR INPUT)
  set(_name "${INPUT}")
  string(REPLACE "\\" " slash " _name "${_name}")
  string(REPLACE "/" " slash " _name "${_name}")
  string(REPLACE ":" " - " _name "${_name}")
  string(REPLACE "<" " less-than " _name "${_name}")
  string(REPLACE ">" " greater-than " _name "${_name}")
  string(REPLACE "\"" "'" _name "${_name}")
  string(REPLACE "|" " pipe " _name "${_name}")
  string(REPLACE "?" " question " _name "${_name}")
  string(REPLACE "*" " star " _name "${_name}")
  string(REGEX REPLACE "[\r\n\t]+" " " _name "${_name}")
  string(REGEX REPLACE "[ ]+" " " _name "${_name}")
  while(_name MATCHES " -  - ")
    string(REPLACE " -  - " " - " _name "${_name}")
  endwhile()
  while(_name MATCHES " - - ")
    string(REPLACE " - - " " - " _name "${_name}")
  endwhile()
  string(STRIP "${_name}" _name)
  while(_name MATCHES "[ \\.]$")
    string(REGEX REPLACE "[ \\.]$" "" _name "${_name}")
  endwhile()
  if(_name STREQUAL "")
    set(_name "Unnamed")
  endif()
  set(${OUT_VAR} "${_name}" PARENT_SCOPE)
endfunction()

function(catch_discover_tests_impl)

  cmake_parse_arguments(
    ""
    ""
    "TEST_EXECUTABLE;TEST_WORKING_DIR;TEST_DL_PATHS;TEST_OUTPUT_DIR;TEST_OUTPUT_PREFIX;TEST_OUTPUT_SUFFIX;TEST_PREFIX;TEST_REPORTER;TEST_SPEC;TEST_SUFFIX;TEST_LIST;CTEST_FILE"
    "TEST_EXTRA_ARGS;TEST_PROPERTIES;TEST_EXECUTOR"
    ${ARGN}
  )

  set(prefix "${_TEST_PREFIX}")
  set(suffix "${_TEST_SUFFIX}")
  set(spec ${_TEST_SPEC})
  set(extra_args ${_TEST_EXTRA_ARGS})
  set(properties ${_TEST_PROPERTIES})
  list(FIND properties TIMEOUT _timeout_property_index)
  if(_timeout_property_index EQUAL -1)
    list(APPEND properties TIMEOUT 30)
  endif()
  set(reporter ${_TEST_REPORTER})
  set(output_dir ${_TEST_OUTPUT_DIR})
  set(output_prefix ${_TEST_OUTPUT_PREFIX})
  set(output_suffix ${_TEST_OUTPUT_SUFFIX})
  set(dl_paths ${_TEST_DL_PATHS})
  set(script)
  set(suite)
  set(tests)

  if(WIN32)
    set(dl_paths_variable_name PATH)
  elseif(APPLE)
    set(dl_paths_variable_name DYLD_LIBRARY_PATH)
  else()
    set(dl_paths_variable_name LD_LIBRARY_PATH)
  endif()

  if(NOT EXISTS "${_TEST_EXECUTABLE}")
    message(FATAL_ERROR
      "Specified test executable '${_TEST_EXECUTABLE}' does not exist"
    )
  endif()

  if(dl_paths)
    cmake_path(CONVERT "${dl_paths}" TO_NATIVE_PATH_LIST paths)
    set(ENV{${dl_paths_variable_name}} "${paths}")
  endif()

  execute_process(
    COMMAND ${_TEST_EXECUTOR} "${_TEST_EXECUTABLE}" ${spec} --list-tests --verbosity quiet
    OUTPUT_VARIABLE output
    RESULT_VARIABLE result
    WORKING_DIRECTORY "${_TEST_WORKING_DIR}"
  )
  if(NOT ${result} EQUAL 0)
    message(FATAL_ERROR
      "Error running test executable '${_TEST_EXECUTABLE}':\n"
      "  Result: ${result}\n"
      "  Output: ${output}\n"
    )
  endif()

  string(REPLACE ";" "\;" output "${output}")
  string(REPLACE "\n" ";" output "${output}")

  if(reporter)
    set(reporter_arg "--reporter ${reporter}")

    execute_process(
      COMMAND ${_TEST_EXECUTOR} "${_TEST_EXECUTABLE}" ${spec} ${reporter_arg} --list-reporters
      OUTPUT_VARIABLE reporter_check_output
      RESULT_VARIABLE reporter_check_result
      WORKING_DIRECTORY "${_TEST_WORKING_DIR}"
    )
    if(${reporter_check_result} EQUAL 255)
      message(FATAL_ERROR
        "\"${reporter}\" is not a valid reporter!\n"
      )
    elseif(NOT ${reporter_check_result} EQUAL 0)
      message(FATAL_ERROR
        "Error running test executable '${_TEST_EXECUTABLE}':\n"
        "  Result: ${reporter_check_result}\n"
        "  Output: ${reporter_check_output}\n"
      )
    endif()
  endif()

  if(output_dir AND NOT IS_ABSOLUTE ${output_dir})
    set(output_dir "${_TEST_WORKING_DIR}/${output_dir}")
    if(NOT EXISTS ${output_dir})
      file(MAKE_DIRECTORY ${output_dir})
    endif()
  endif()

  if(dl_paths)
    foreach(path ${dl_paths})
      cmake_path(NATIVE_PATH path native_path)
      list(APPEND environment_modifications "${dl_paths_variable_name}=path_list_prepend:${native_path}")
    endforeach()
  endif()

  foreach(line ${output})
    set(test "${line}")
    set(test_name "${test}")
    foreach(char \\ , [ ])
      string(REPLACE ${char} "\\${char}" test_name "${test_name}")
    endforeach(char)

    make_windows_safe_ctest_name(display_name "${prefix}${test}${suffix}")
    string(SHA1 display_hash "${display_name}")
    set(display_count_var "_display_count_${display_hash}")
    if(DEFINED ${display_count_var})
      math(EXPR display_count "${${display_count_var}} + 1")
    else()
      set(display_count 1)
    endif()
    set(${display_count_var} ${display_count})
    if(display_count GREATER 1)
      set(display_name "${display_name} - ${display_count}")
    endif()

    if(output_dir)
      string(REGEX REPLACE "[^A-Za-z0-9_]" "_" test_name_clean "${test_name}")
      set(output_dir_arg "--out ${output_dir}/${output_prefix}${test_name_clean}${output_suffix}")
    endif()

    add_command(add_test
      "${display_name}"
      ${_TEST_EXECUTOR}
      "${_TEST_EXECUTABLE}"
      "${test_name}"
      ${extra_args}
      "${reporter_arg}"
      "${output_dir_arg}"
    )
    add_command(set_tests_properties
      "${display_name}"
      PROPERTIES
      WORKING_DIRECTORY "${_TEST_WORKING_DIR}"
      ${properties}
    )

    if(environment_modifications)
      add_command(set_tests_properties
        "${display_name}"
        PROPERTIES
        ENVIRONMENT_MODIFICATION "${environment_modifications}")
    endif()

    list(APPEND tests "${display_name}")
  endforeach()

  add_command(set ${_TEST_LIST} ${tests})

  file(WRITE "${_CTEST_FILE}" "${script}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
  catch_discover_tests_impl(
    TEST_EXECUTABLE ${TEST_EXECUTABLE}
    TEST_EXECUTOR ${TEST_EXECUTOR}
    TEST_WORKING_DIR ${TEST_WORKING_DIR}
    TEST_SPEC ${TEST_SPEC}
    TEST_EXTRA_ARGS ${TEST_EXTRA_ARGS}
    TEST_PROPERTIES ${TEST_PROPERTIES}
    TEST_PREFIX ${TEST_PREFIX}
    TEST_SUFFIX ${TEST_SUFFIX}
    TEST_LIST ${TEST_LIST}
    TEST_REPORTER ${TEST_REPORTER}
    TEST_OUTPUT_DIR ${TEST_OUTPUT_DIR}
    TEST_OUTPUT_PREFIX ${TEST_OUTPUT_PREFIX}
    TEST_OUTPUT_SUFFIX ${TEST_OUTPUT_SUFFIX}
    TEST_DL_PATHS ${TEST_DL_PATHS}
    CTEST_FILE ${CTEST_FILE}
  )
endif()
