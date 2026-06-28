cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(CWR
    VERSION 1.0.0
    LANGUAGES CXX C
    DESCRIPTION "Arma: Cold War Assault - Remastered"
)

# iOS has no desktop OpenGL (no OpenGL.framework) -- PoseidonGL33 and
# anything that requires it are skipped for this platform; Metal is the
# only renderer there.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(POSEIDON_TARGET_IOS TRUE)
    add_compile_definitions(POSEIDON_TARGET_IOS)
else()
    set(POSEIDON_TARGET_IOS FALSE)
endif()

if(POSEIDON_TARGET_IOS)
    # Resolve SDL3::SDL3-static once at root scope (visible to every
    # add_subdirectory() below, including sibling directories like
    # engine/Poseidon and engine/PoseidonMTL that each separately call
    # find_package(SDL3) -- without this, each of those calls would create
    # its own directory-scoped copy of the imported target).
    #
    # Also works around a CMake/vcpkg interaction on iOS: SDL3-static's
    # INTERFACE_LINK_LIBRARIES uses $<LINK_LIBRARY:FRAMEWORK,...> entries,
    # and when those follow a long plain-library link chain on the final
    # link line, the linker receives "-framework <path-to-libSDL3.a>"
    # instead of "-framework CoreMedia" for the first framework -- the
    # static lib's own path gets duplicated in as the bogus argument.
    # Move the framework list to plain INTERFACE_LINK_OPTIONS flags
    # (bypassing the LINK_LIBRARY feature) to sidestep it.
    find_package(SDL3 CONFIG REQUIRED)
    if(TARGET SDL3::SDL3-static)
        set(_sdl3_ios_frameworks
            CoreMedia CoreVideo CoreAudio AudioToolbox AVFoundation
            CoreBluetooth CoreGraphics CoreMotion Foundation GameController
            Metal OpenGLES QuartzCore UIKit
        )
        # Each entry must be a single "SHELL:-framework Name" string -- two
        # separate list items ("-framework", "Name") get hit by CMake's
        # link-line de-duplication, which treats repeated bare "-framework"
        # tokens as duplicate libraries and collapses them, orphaning the
        # framework names that followed. A plain combined "-framework Name"
        # item (no SHELL: prefix) avoids the dedup issue but then gets
        # quoted as one opaque shell token instead of word-split. The
        # SHELL: prefix tells CMake to treat the spaces as real argument
        # separators when emitting the command line.
        set(_sdl3_ios_link_options "")
        foreach(_fw IN LISTS _sdl3_ios_frameworks)
            list(APPEND _sdl3_ios_link_options "SHELL:-framework ${_fw}")
        endforeach()
        list(APPEND _sdl3_ios_link_options "SHELL:-weak_framework CoreHaptics")
        set_target_properties(SDL3::SDL3-static PROPERTIES
            INTERFACE_LINK_LIBRARIES "m;SDL3::Headers"
        )
        set_property(TARGET SDL3::SDL3-static APPEND PROPERTY
            INTERFACE_LINK_OPTIONS ${_sdl3_ios_link_options}
        )
        unset(_sdl3_ios_frameworks)
        unset(_sdl3_ios_link_options)
    endif()

    # imgui's vcpkg port has the same problem one level worse: its iOS
    # INTERFACE_LINK_LIBRARIES literally ships "-framework;OpenGLES" as two
    # separate plain list items (not even a $<LINK_LIBRARY:...> genex), so
    # CMake treats "-framework" and "OpenGLES" as two independent plain
    # libraries -- "OpenGLES" becomes "-lOpenGLES" and pairs with whatever
    # "-framework" happens to land next to during link-line de-duplication.
    # Recombine them into one "-framework OpenGLES" item, which plain
    # LINK_LIBRARIES entries (unlike LINK_OPTIONS) pass through correctly
    # without needing a SHELL: prefix -- see PoseidonMTL's own Metal/
    # QuartzCore/Foundation find_library() results for the same pattern.
    find_package(imgui CONFIG REQUIRED)
    if(TARGET imgui::imgui)
        get_target_property(_imgui_link_libs imgui::imgui INTERFACE_LINK_LIBRARIES)
        if(_imgui_link_libs)
            list(REMOVE_ITEM _imgui_link_libs "-framework" "OpenGLES")
            list(APPEND _imgui_link_libs "-framework OpenGLES")
            set_target_properties(imgui::imgui PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_imgui_link_libs}"
            )
        endif()
        unset(_imgui_link_libs)
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Version tag embedded in release builds (e.g. rc1, release). Kept as a cache
# variable for script compatibility, but the actual binary metadata is generated
# at build time from OFPR_BUILD_VERSION_TAG so raw `cmake --build` with no env
# clears stale tags instead of reusing this sticky cache value.
set(BUILD_VERSION_TAG "" CACHE STRING "Version tag (rc1 / release / ...); empty = git sha")

add_compile_definitions(
    $<$<CONFIG:Debug>:_DEBUG>  # Standard debug define (Debug only — avoids _ITERATOR_DEBUG_LEVEL mismatch in Release)
)

if(WIN32)
    add_compile_definitions(
        # Architecture
        x86
        WIN32

        _LIB
        _ALLOW_KEYWORD_MACROS # C++ language features

        # Minimal Windows.h — exclude rarely-used APIs, prevent min/max macro pollution
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        NOSERVICE
        NOMCX
        NOIME
    )

    # Function- and data-level COMDAT so each function lands in its own
    # section.  Executable targets opt into /OPT:REF /OPT:ICF to dead-strip
    # functions no reachable code calls — see apps/tetris/Tetris/CMakeLists.txt.
    add_compile_options(/Gy /Gw)
else()
    add_compile_definitions(
        HAS_CLOCK_GETTIME   # Linux: use clock_gettime in potime.cpp
    )

    # Equivalent of MSVC /Gy /Gw — per-function/per-data sections so the
    # linker (--gc-sections) can dead-strip unused symbols.
    add_compile_options(-ffunction-sections -fdata-sections)
endif()

# Make __FILE__ repo-root-relative so log/assert lines (Fail(), DoAssert(), LOG_*)
# read "engine/Poseidon/...:NN" instead of an absolute build-machine path. Affects only
# the __FILE__ macro string, not debug info.
#
# GNU-driver clang only. clang-cl silently ignores GCC-style -f flags on its own command
# line (a per-TU -Wunknown-argument warning), and forwarding via /clang: would rewrite
# __FILE__ to a repo-relative string — which breaks the __FILE__-based repo-root resolution
# several test helpers depend on (e.g. RepoPath() walks __FILE__'s parents expecting an
# absolute path). So leave clang-cl's default absolute __FILE__ alone.
if(NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=)
endif()

# Global warning suppressions for legacy codebase and intentional patterns
# These are all actually needed - verified 11,624 warnings without them (Dec 2025)
add_compile_options(
    # Most critical suppressions (cover 80% of warnings)
    -Wno-microsoft-enum-forward-reference # 1,987 instances - Forward enum declarations (MSVC extension)
    -Wno-nontrivial-memcall               # 4,000+ instances - Intentional memcpy/memmove on bitwise-movable types (ClassIsMovableZeroed)
    -Wno-microsoft-extra-qualification    # 324 instances - Extra qualifications on members (MSVC quirk)
    
    # Test/development suppressions
    -Wno-varargs                          # Suppress until EvalError enum in va_start (UB but works)
    
    # Other MSVC compatibility
    -Wno-microsoft-include                # MSVC-specific include behavior
    -Wno-microsoft-cast                   # MSVC-style casts
    -Wno-nonportable-include-path         # Case mismatches in includes (Windows filesystem)
    -Wno-inconsistent-dllimport           # DLL import inconsistencies
    -Wno-ignored-pragmas                  # Unrecognized pragmas (MSVC-specific)
    "-Wno-#pragma-messages"               # #pragma message directives (build config info)
    -Wno-pragma-pack                      # #pragma pack warnings
    
    # Intentional legacy patterns
    -Wno-switch                           # Incomplete switch statements (intentional in AI logic)
    -Wno-reorder-ctor                     # Constructor init order (legacy code, low risk)
    -Wno-reorder                          # Field initialization order (legacy code, low risk)
    -Wno-logical-op-parentheses           # Operator precedence (intentional boolean logic)
    -Wno-writable-strings                 # String literals to char* (legacy API contracts)
    -Wno-vexing-parse                     # Most vexing parse (legacy declarations)
    -Wno-dynamic-class-memaccess          # memset on classes (legacy pattern, works)
    -Wno-deprecated-non-prototype         # Old C-style function declarations
    -Wno-parentheses                      # Parentheses warnings
    -Wno-enum-compare                     # Enum comparisons (intentional)
    -Wno-bitwise-op-parentheses           # Bitwise operator precedence
    -Wno-delete-non-abstract-non-virtual-dtor        # Non-virtual dtors (legacy class design)
    -Wno-delete-non-virtual-dtor          # Delete on non-final classes with virtual functions but non-virtual dtor
    -Wno-implicit-const-int-float-conversion         # Implicit conversions (legacy code)
    -Wno-invalid-offsetof                 # offsetof on non-standard-layout types
    -Wno-non-pod-varargs                  # Non-POD types in varargs (legacy pattern)
    -Wno-pointer-sign                     # Pointer sign mismatches
    -Wno-pointer-bool-conversion          # Pointer to bool conversions
    -Wno-tautological-compare             # Tautological comparisons (defensive checks for invalid enum values)
    -Wno-tautological-bitwise-compare     # Bitwise comparisons (defensive programming)
    -Wno-bitwise-instead-of-logical       # Bitwise operators used for side effects
    
    # Debug/unused suppressions (may be used in other configs)
    -Wno-unused-function                  # Static helper functions
    -Wno-unused-private-field             # Private fields (may be used in debug builds)
    -Wno-unused-variable                  # Unused vars (may be used in debug builds)
    -Wno-unused-but-set-variable          # Variables set but not used
    -Wno-unused-const-variable            # Unused constants
    -Wno-unused-value                     # Unused expression values
    -Wno-unneeded-internal-declaration    # Static declarations not emitted
    
    # Defensive programming patterns
    -Wno-null-dereference                 # Intentional crash triggers in error paths
    -Wno-tautological-constant-out-of-range-compare # Range checks
    
    # Other
    -Wno-comment                          # Comment warnings (legacy formatting)
    -Wno-deprecated-declarations          # Deprecated functions (platform-specific)
    -Wno-undefined-bool-conversion        # Undefined bool conversions
    -Wno-unknown-warning-option           # Warning options not recognized by all clang versions
    -Wno-format                           # Format string mismatches (MSVC vs clang differences)
)

enable_testing()

# Backend availability — driven by platform, overridable
option(CWR_HAS_OPENGL    "OpenGL 4.5 graphics backend"  ON)
option(CWR_HAS_OPENAL    "OpenAL audio backend"          ON)

# Disable precompiled headers — for include self-containment audits. With PCH off
# the prelude is no longer force-included, so every TU must pull what it uses;
# building this way surfaces files that silently lean on the prelude. Off in CI.
option(POSEIDON_DISABLE_PCH "Disable precompiled headers (include self-containment audit)" OFF)

# libFuzzer harnesses (network decoder). Off by default; the win-x64-clang-fuzz
# preset turns it on. Requires a clang toolchain built with -fsanitize=fuzzer.
option(POSEIDON_BUILD_FUZZERS "Build libFuzzer network harnesses" OFF)

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/BuildConfig.h.in
    ${CMAKE_BINARY_DIR}/generated/BuildConfig.h
)
include_directories(${CMAKE_BINARY_DIR}/generated)

find_package(Catch2 CONFIG REQUIRED)
find_package(enkiTS CONFIG REQUIRED)

# dist/<preset>/{game,viewer,tools} - auto-create on build
get_filename_component(PRESET_NAME "${CMAKE_BINARY_DIR}" NAME)
set(DIST_PRESET_NAME "${PRESET_NAME}")
if(DIST_PRESET_NAME MATCHES "^(win|linux)-([^-]+)(-(.*))?$")
    set(_dist_platform "${CMAKE_MATCH_1}")
    set(_dist_arch "${CMAKE_MATCH_2}")
    set(_dist_suffix "${CMAKE_MATCH_4}")
    if(_dist_suffix MATCHES "^clang-(.+)$")
        set(_dist_suffix "${CMAKE_MATCH_1}")
    elseif(_dist_suffix STREQUAL "clang")
        set(_dist_suffix "")
    endif()
    if(_dist_suffix)
        set(DIST_PRESET_NAME "${_dist_arch}-${_dist_platform}-${_dist_suffix}")
    else()
        set(DIST_PRESET_NAME "${_dist_arch}-${_dist_platform}")
    endif()
endif()
set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist/${DIST_PRESET_NAME}")

add_subdirectory(engine/Poseidon)
if(NOT POSEIDON_TARGET_IOS)
    add_subdirectory(engine/PoseidonGL33)
endif()
add_subdirectory(engine/PoseidonOpenAL)
add_subdirectory(engine/PoseidonFormats)

if(APPLE)
    # Milestone-0 native Metal backend scaffold — macOS/Apple Silicon and iOS.
    # See engine/PoseidonMTL/EngineMTL.hpp for scope notes.
    add_subdirectory(engine/PoseidonMTL)
    add_subdirectory(apps/tools/MetalSmokeTest)
    if(NOT POSEIDON_TARGET_IOS)
        # Depends on PoseidonGL33, which iOS doesn't build (no desktop OpenGL).
        add_subdirectory(apps/tools/GL33SmokeTest)
    endif()
endif()
add_subdirectory(apps/cwr/GameBase)
add_subdirectory(apps/cwr/Game)

if(POSEIDON_TARGET_IOS)
    # iOS bring-up is scoped to the Metal-only engine + PoseidonGame itself.
    # Everything below this branch -- desktop CLI tools (Tools/TcPbo/TcLister),
    # the Studio editor, the dedicated server, GameDemo, Tetris, and the unit
    # test binaries -- has no iOS bundle/install setup yet (most aren't
    # meaningful as standalone iOS apps at all) and isn't part of this pass.
else()
    add_subdirectory(tests/unit/engine/Poseidon/Foundation)
    add_subdirectory(apps/tools/Evaluator)
    add_subdirectory(tests/unit/apps/Evaluator)
    add_subdirectory(tests/unit/engine/Poseidon)
    add_subdirectory(apps/cwr/Server)
    add_subdirectory(tests/unit/apps/Server)

    if(POSEIDON_BUILD_FUZZERS)
        add_subdirectory(apps/fuzzers/Fuzzer)
    endif()

    add_subdirectory(apps/cwr/GameDemo)
    add_subdirectory(apps/tools/Tools)
    add_subdirectory(apps/tools/TcPbo)
    add_subdirectory(apps/tools/TcLister)
    add_subdirectory(apps/tools/Studio)
    # tests/unit/apps/Studio's ImGui-Test-Engine UI suite is quarantined under
    # License-restricted UI test sources are not part of this public tree.
    add_subdirectory(apps/tetris/Tetris)
    add_subdirectory(tests/unit/apps/Tetris)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/TridentCTest.cmake)
register_trident_integration_ctests()

# ── Precompiled header ───────────────────────────────────────────────────────
# The Poseidon static lib precompiles PoseidonPCH.hpp (see engine/Poseidon).
# Apply the same prelude as a precompiled header to the other substantial,
# prelude-consuming targets so their TUs build against a precompiled blob instead
# of re-parsing the prelude per file. Per-target PRIVATE rather than REUSE_FROM:
# targets carry differing compile definitions (e.g. _WINDOWS) that clang bakes
# into the PCH, so a shared blob would be rejected. Trivial 1-2 TU targets
# (TcPbo, TcLister) and the standalone PoseidonFormats C-API are excluded.
set(POSEIDON_PCH_TARGETS
    PoseidonGL33 PoseidonOpenAL GameBase
    PoseidonGame PoseidonGameDemo PoseidonServer PoseidonTools
    PoseidonStudio PoseidonEvaluator PoseidonTetris
    PoseidonTests PoseidonCoreTests PoseidonFoundationTests
    PoseidonServerTests PoseidonTetrisTests PoseidonEvaluatorTests
)
if(NOT POSEIDON_DISABLE_PCH)
    foreach(pch_target ${POSEIDON_PCH_TARGETS})
        if(TARGET ${pch_target})
            # Restrict to C++ TUs: some targets (e.g. PoseidonGL33's glad.c) carry C
            # sources, and the prelude is C++ (operator new, namespaces).
            target_precompile_headers(${pch_target} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/engine/Poseidon/Foundation/PoseidonPCH.hpp>)
        endif()
    endforeach()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        foreach(pch_target Poseidon ${POSEIDON_PCH_TARGETS})
            if(TARGET ${pch_target})
                target_compile_options(${pch_target} PRIVATE
                    $<$<COMPILE_LANGUAGE:CXX>:-Xclang>
                    $<$<COMPILE_LANGUAGE:CXX>:-fno-pch-timestamp>)
            endif()
        endforeach()
    endif()
endif()

# Collect sources for static analysis and formatting
set(ANALYSIS_TARGETS
    PoseidonFoundationTests
    PoseidonCoreTests
    PoseidonTests
    PoseidonGame
    PoseidonServer
    PoseidonTools
    PoseidonStudio
    PoseidonEvaluator
    PoseidonEvaluatorTests
    PoseidonTetris
    PoseidonTetrisTests
    TcPbo
    TcLister
)

foreach(target ${ANALYSIS_TARGETS})
    if(NOT TARGET ${target})
        continue()
    endif()
    get_target_property(target_sources ${target} SOURCES)
    get_target_property(target_source_dir ${target} SOURCE_DIR)
    
    foreach(source ${target_sources})
        # Convert relative paths to absolute
        if(NOT IS_ABSOLUTE ${source})
            set(source "${target_source_dir}/${source}")
        endif()
        
        # Skip PCH files
        if(source MATCHES "pch\\.(cpp|hpp)$")
            continue()
        endif()
        
        # Collect .cpp files for clang-tidy (analyzes headers transitively)
        if(source MATCHES "\\.cpp$")
            list(APPEND ALL_CPP_SOURCES ${source})
        endif()
        
        # Collect .cpp and .hpp files for clang-format
        if(source MATCHES "\\.(cpp|hpp)$")
            list(APPEND ALL_FORMAT_SOURCES ${source})
        endif()
    endforeach()
endforeach()

# Add Poseidon sources to formatting
foreach(source ${POSEIDON_MODERN_SOURCES})
    # Convert relative paths to absolute
    if(NOT IS_ABSOLUTE ${source})
        set(source "${CMAKE_SOURCE_DIR}/engine/Poseidon/${source}")
    endif()
    
    # Collect .cpp files for clang-tidy
    if(source MATCHES "\\.cpp$")
        list(APPEND ALL_CPP_SOURCES ${source})
    endif()
    
    # Collect .cpp and .hpp files for clang-format
    if(source MATCHES "\\.(cpp|hpp)$")
        list(APPEND ALL_FORMAT_SOURCES ${source})
    endif()
endforeach()

list(REMOVE_DUPLICATES ALL_CPP_SOURCES)
list(REMOVE_DUPLICATES ALL_FORMAT_SOURCES)

# Write source lists to files (avoid Windows command line length limit)
file(WRITE ${CMAKE_BINARY_DIR}/format_sources.txt "")
foreach(source ${ALL_FORMAT_SOURCES})
    file(APPEND ${CMAKE_BINARY_DIR}/format_sources.txt "${source}\n")
endforeach()

file(WRITE ${CMAKE_BINARY_DIR}/tidy_sources.txt "")
foreach(source ${ALL_CPP_SOURCES})
    file(APPEND ${CMAKE_BINARY_DIR}/tidy_sources.txt "${source}\n")
endforeach()

# clang-format targets
find_program(CLANG_FORMAT_EXE NAMES clang-format REQUIRED)

if(WIN32)
    add_custom_target(Format
        COMMAND powershell -NoProfile -Command "Get-Content ${CMAKE_BINARY_DIR}/format_sources.txt | ForEach-Object { & '${CLANG_FORMAT_EXE}' --dry-run --Werror $_ }"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking C++ formatting (fails if changes needed)"
        VERBATIM
    )
    add_custom_target(FormatFix
        COMMAND powershell -NoProfile -Command "Get-Content ${CMAKE_BINARY_DIR}/format_sources.txt | ForEach-Object { & '${CLANG_FORMAT_EXE}' -i $_ }"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Formatting C++ sources with clang-format"
        VERBATIM
    )
else()
    add_custom_target(Format
        COMMAND xargs -a ${CMAKE_BINARY_DIR}/format_sources.txt ${CLANG_FORMAT_EXE} --dry-run --Werror
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking C++ formatting (fails if changes needed)"
        VERBATIM
    )
    add_custom_target(FormatFix
        COMMAND xargs -a ${CMAKE_BINARY_DIR}/format_sources.txt ${CLANG_FORMAT_EXE} -i
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Formatting C++ sources with clang-format"
        VERBATIM
    )
endif()

# Python lint targets (ruff via uv, optional)
find_program(UV_EXE NAMES uv)
set(BLENDER_ADDON_DIR ${CMAKE_SOURCE_DIR}/apps/tools/BlenderAddon)

if(UV_EXE AND EXISTS ${BLENDER_ADDON_DIR}/pyproject.toml)
    add_custom_target(PythonLint
        COMMAND ${UV_EXE} run ruff check ${BLENDER_ADDON_DIR}/io_import_p3d ${CMAKE_SOURCE_DIR}/tests/BlenderAddon
        COMMAND ${UV_EXE} run ruff format --check ${BLENDER_ADDON_DIR}/io_import_p3d ${CMAKE_SOURCE_DIR}/tests/BlenderAddon
        WORKING_DIRECTORY ${BLENDER_ADDON_DIR}
        COMMENT "Checking Python formatting and lint (ruff)"
        VERBATIM
    )
    add_custom_target(PythonLintFix
        COMMAND ${UV_EXE} run ruff format ${BLENDER_ADDON_DIR}/io_import_p3d ${CMAKE_SOURCE_DIR}/tests/BlenderAddon
        COMMAND ${UV_EXE} run ruff check --fix ${BLENDER_ADDON_DIR}/io_import_p3d ${CMAKE_SOURCE_DIR}/tests/BlenderAddon
        WORKING_DIRECTORY ${BLENDER_ADDON_DIR}
        COMMENT "Formatting Python sources with ruff"
        VERBATIM
    )

    # Combined targets: C++ + Python
    add_custom_target(Lint DEPENDS Format PythonLint)
    add_custom_target(LintFix DEPENDS FormatFix PythonLintFix)
else()
    # No Python tooling — Lint just runs C++ format
    add_custom_target(Lint DEPENDS Format)
    add_custom_target(LintFix DEPENDS FormatFix)
endif()

# clang-tidy targets
find_program(CLANG_TIDY_EXE NAMES clang-tidy)

if(CLANG_TIDY_EXE)
    if(WIN32)
        add_custom_target(Tidy
            COMMAND powershell -NoProfile -Command "Get-Content ${CMAKE_BINARY_DIR}/tidy_sources.txt | ForEach-Object { & '${CLANG_TIDY_EXE}' -p ${CMAKE_BINARY_DIR} --allow-no-checks $_ }"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running clang-tidy on all sources"
            VERBATIM
        )
        add_custom_target(TidyFix
            COMMAND powershell -NoProfile -Command "Get-Content ${CMAKE_BINARY_DIR}/tidy_sources.txt | ForEach-Object { & '${CLANG_TIDY_EXE}' -p ${CMAKE_BINARY_DIR} --allow-no-checks --fix-errors --fix $_ }"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running clang-tidy with automatic fixes (including compilation errors)"
            VERBATIM
        )
    else()
        add_custom_target(Tidy
            COMMAND xargs -a ${CMAKE_BINARY_DIR}/tidy_sources.txt ${CLANG_TIDY_EXE} -p ${CMAKE_BINARY_DIR} --allow-no-checks
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running clang-tidy on all sources"
            VERBATIM
        )
        add_custom_target(TidyFix
            COMMAND xargs -a ${CMAKE_BINARY_DIR}/tidy_sources.txt ${CLANG_TIDY_EXE} -p ${CMAKE_BINARY_DIR} --allow-no-checks --fix-errors --fix
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running clang-tidy with automatic fixes (including compilation errors)"
            VERBATIM
        )
    endif()
endif()

# File size check target (warn > 3000 lines, error > 5000 lines)
add_custom_target(FileSize
    COMMAND ${CMAKE_COMMAND} -DSOURCE_LIST=${CMAKE_BINARY_DIR}/format_sources.txt -P ${CMAKE_SOURCE_DIR}/cmake/CheckFileSize.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking source file sizes (warn > 3000, error > 5000)"
    VERBATIM
)
