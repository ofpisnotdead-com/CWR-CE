function(register_trident_integration_ctests)
    option(OFPR_REGISTER_TRIDENT_CTESTS "Register Trident integration tests with CTest" OFF)
    if(NOT OFPR_REGISTER_TRIDENT_CTESTS)
        message(STATUS "Trident integration CTests disabled (set OFPR_REGISTER_TRIDENT_CTESTS=ON to enable)")
        return()
    endif()

    set(_tri_data_dir "${CMAKE_SOURCE_DIR}/packages/Combined")
    if(NOT EXISTS "${_tri_data_dir}")
        message(STATUS "Trident integration CTests disabled: ${_tri_data_dir} not found")
        return()
    endif()

    set(_tri_name "tri${CMAKE_EXECUTABLE_SUFFIX}")
    set(_tri_dist "${DIST_DIR}/${_tri_name}")

    set(_cargo_profile "debug")
    if(PRESET_NAME MATCHES "(^|-)rwdi($|-)" OR PRESET_NAME MATCHES "(^|-)steamrt4($|-)")
        set(_cargo_profile "rwdi")
    elseif(PRESET_NAME MATCHES "(^|-)rel($|-)")
        set(_cargo_profile "rel")
    elseif(PRESET_NAME MATCHES "(^|-)dbg($|-)" OR PRESET_NAME MATCHES "(^|-)clang($|-)")
        set(_cargo_profile "debug")
    endif()
    set(_tri_cargo "${CMAKE_SOURCE_DIR}/engine/Trident/target/${_cargo_profile}/${_tri_name}")
    set(_tri_executables "${_tri_dist};${_tri_cargo}")

    set(_integration_roots
        flows
        ingame
        mp
        multiplayer
        render
        rendering
        scripting
        ui
    )

    foreach(_root IN LISTS _integration_roots)
        set(_path "${CMAKE_SOURCE_DIR}/tests/integration/${_root}")
        if(NOT EXISTS "${_path}")
            continue()
        endif()

        set(_test_name "TridentIntegration - ${_root}")
        add_test(
            NAME "${_test_name}"
            COMMAND
                "${CMAKE_COMMAND}"
                -D "TRIDENT_TEST_PATH=${_path}"
                -D "TRIDENT_GAME_DIR=${DIST_DIR}"
                -D "TRIDENT_DATA_DIR=${_tri_data_dir}"
                -D "TRIDENT_EXECUTABLES=${_tri_executables}"
                -P "${CMAKE_SOURCE_DIR}/cmake/RunTridentCTest.cmake"
        )
        set_tests_properties("${_test_name}" PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RUN_SERIAL TRUE
            RESOURCE_LOCK "trident-integration"
            LABELS "trident;integration"
            TIMEOUT 1800
        )
    endforeach()
endfunction()
