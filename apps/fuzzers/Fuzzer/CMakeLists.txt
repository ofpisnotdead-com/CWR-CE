# libFuzzer harnesses for the network decoder and asset format readers.
# They are configured only when POSEIDON_BUILD_FUZZERS is enabled.
# The toolchain compiles every TU with ASan + SanitizerCoverage (fuzzer-no-link);
# each harness here links the full libFuzzer driver via -fsanitize=fuzzer and
# pulls in Poseidon core (the same static lib the dedicated server links).

function(add_fuzzer name source)
    add_executable(${name} ${source})

    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/engine
    )
    if(WIN32)
        # CMake links clang-cl targets with lld-link.exe directly, which ignores
        # -fsanitize=fuzzer. Name the clang_rt libraries explicitly and disable
        # the MSVC linker's conflicting ASan inference.
        target_link_options(${name} PRIVATE
            /INFERASANLIBS:NO
            clang_rt.asan_dynamic-x86_64.lib
            clang_rt.asan_static_runtime_thunk-x86_64.lib
            clang_rt.fuzzer-x86_64.lib
        )
        target_link_libraries(${name} PRIVATE
            Poseidon
            winmm.lib
            ws2_32.lib
            legacy_stdio_definitions.lib
        )
        set_target_properties(${name} PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:CONSOLE /IGNORE:4099 /SAFESEH:NO"
        )
    elseif(APPLE)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_libraries(${name} PRIVATE
            Poseidon
            pthread
            dl
        )
    else()
        # Linux clang links the full libFuzzer + ASan + UBSan runtime from the
        # driver flag; no explicit clang_rt naming is needed.
        target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_libraries(${name} PRIVATE
            -Wl,--start-group
            Poseidon
            -Wl,--end-group
            pthread
            dl
        )
    endif()
endfunction()

add_fuzzer(fuzz_decode_msg fuzz_decode_msg.cpp)
add_fuzzer(fuzz_rtm fuzz_rtm.cpp)
add_fuzzer(fuzz_p3d fuzz_p3d.cpp)
add_fuzzer(fuzz_paa fuzz_paa.cpp)
add_fuzzer(fuzz_paramfile fuzz_paramfile.cpp)
add_fuzzer(fuzz_pbo fuzz_pbo.cpp)
add_fuzzer(fuzz_wrp fuzz_wrp.cpp)
add_fuzzer(fuzz_wss fuzz_wss.cpp)
add_fuzzer(fuzz_stringtable fuzz_stringtable.cpp)
add_fuzzer(fuzz_wav fuzz_wav.cpp)
add_fuzzer(fuzz_sqf fuzz_sqf.cpp)
add_fuzzer(fuzz_sqs fuzz_sqs.cpp)
add_fuzzer(fuzz_sqf_exec fuzz_sqf_exec.cpp)
add_fuzzer(fuzz_savegame fuzz_savegame.cpp)
add_fuzzer(fuzz_lip fuzz_lip.cpp)
add_fuzzer(fuzz_shape fuzz_shape.cpp)
