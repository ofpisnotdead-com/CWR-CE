set(POSEIDON_BASE_TEST_SOURCES
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/engine/Random/test_randomGen.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/engine/Random/test_isaac.cpp
    Algorithms/test_bsearch.cpp
    Algorithms/test_crc32.cpp
    Algorithms/test_qsort.cpp
    Algorithms/test_spline.cpp
    Enums/test_enumNames.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/engine/Poseidon/IO/Filesystem/test_dir_scanner.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/engine/Poseidon/IO/Filesystem/test_file_ops.cpp
    Common/test_netglobal.cpp
    Modules/test_modules_lifecycle.cpp
    Modules/test_modules_registration.cpp
    Memory/test_freeondemand.cpp
    Memory/test_fastalloc.cpp
    Memory/test_memory_budget.cpp
    Math/test_mathDefs.cpp
    Math/test_mathOpt.cpp
    Math/test_matrix.cpp
    Common/test_filenames.cpp
    Common/test_findfirst.cpp
    Common/test_fltopts.cpp
    Common/test_gamePaths.cpp
    Common/test_mathND.cpp
    Common/test_platform.cpp
    Common/test_platformPaths.cpp
    Platform/test_crash_handler.cpp
    Containers/test_cachelist.cpp
    Containers/test_compactbuf.cpp
    Containers/test_array.cpp
    Containers/test_bankarray.cpp
    Containers/test_bigarray.cpp
    Containers/test_bool_array2d.cpp
    Containers/test_hashmap.cpp
    Containers/test_list_bitmask.cpp
    Containers/test_quadtree.cpp
    Containers/test_quadtreeCont.cpp
    Containers/test_rStringArray.cpp
    Containers/test_smallArray.cpp
    Containers/test_specialized_arrays.cpp
    Containers/test_streamArray.cpp
    Containers/test_typeOpts.cpp
    Containers/test_traits_array_ops.cpp
    Containers/test_traits_compactbuf_ops.cpp
    Containers/test_traits_findarray_ops.cpp
    Containers/test_traits_smallarray_ops.cpp
    Containers/test_traits_staticarray_ops.cpp
    Framework/test_potime.cpp
    Strings/test_strings.cpp
    Types/test_enum_decl.cpp
    Types/test_llinks.cpp
    Types/test_pointers.cpp
    Types/test_removelinks.cpp
    Types/test_scopelock.cpp
    Threads/test_pocritical.cpp
    Threads/test_posemaphore.cpp
    Threads/test_pothread.cpp
    Math/test_vector3.cpp
    Math/test_vector3d.cpp
)

add_executable(PoseidonFoundationTests ${POSEIDON_BASE_TEST_SOURCES})

target_link_libraries(PoseidonFoundationTests PRIVATE
    Poseidon
    Catch2::Catch2WithMain
    $<$<BOOL:${WIN32}>:ws2_32>
)

target_compile_definitions(PoseidonFoundationTests PRIVATE
    GAME_DATA_DIR="${CMAKE_SOURCE_DIR}/packages/Combined"
)

target_include_directories(PoseidonFoundationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tests/unit/engine/Random
)

source_group(TREE ${CMAKE_SOURCE_DIR}/tests/unit/engine FILES ${POSEIDON_BASE_TEST_SOURCES})

set_target_properties(PoseidonFoundationTests PROPERTIES
    OUTPUT_NAME "PoseidonFoundationTests"
)

include(${CMAKE_SOURCE_DIR}/cmake/CatchWindowsSafe.cmake)
ofpr_catch_discover_tests(PoseidonFoundationTests
    PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/ubsan-suppressions.txt;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan-suppressions.txt"
)
