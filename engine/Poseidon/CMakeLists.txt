find_package(spdlog CONFIG REQUIRED)

# Poseidon engine static library.
#
# Single target — headless apps link this directly; client apps (Game,
# GameDemo, Tetris) also link PoseidonGL33 + PoseidonOpenAL for the
# concrete graphics/audio backends.

# ============================================================
# Source Collection
# ============================================================

# Collect ALL modern source files
file(GLOB_RECURSE POSEIDON_ALL_SOURCES CONFIGURE_DEPENDS
    "AI/*.cpp"
    "Audio/*.cpp"
    "Asset/Addon/*.cpp"
    "Asset/Formats/*.cpp"
    "Asset/Probes/*.cpp"
    "Core/*.cpp"
    "Dev/Debug/*.cpp"
    "Dev/Diag/*.cpp"
    "Dev/Harness/*.cpp"
    "Game/*.cpp"
    "Graphics/*.cpp"
    "Input/*.cpp"
    "IO/*.cpp"
    "IO/Serialization/*.cpp"
    "Foundation/Memory/*.cpp"
    "Network/*.cpp"
    "Foundation/Platform/*.cpp"
    "Security/*.cpp"
    "UI/*.cpp"
    "Foundation/Math/*.cpp"
    "World/*.cpp"
    "World/Terrain/WrpReader.cpp"
)

set(ALL_SOURCES ${POSEIDON_ALL_SOURCES})
set(POSEIDON_BUILD_INFO_HEADER "${CMAKE_BINARY_DIR}/generated/Poseidon/Core/BuildInfo.hpp")

# ============================================================
# Platform Exclusions (apply to ALL sources before split)
# ============================================================

if(WIN32)
    # Exclude POSIX/Linux-specific files - not needed for Windows build
    list(FILTER ALL_SOURCES EXCLUDE REGEX ".*Posix\.cpp$")
    list(FILTER ALL_SOURCES EXCLUDE REGEX "PlatformLinux\.cpp$")
else()
    # Windows-only debug/platform
    list(FILTER ALL_SOURCES EXCLUDE REGEX "Dev/Debug/DebugWin")
    list(FILTER ALL_SOURCES EXCLUDE REGEX "Foundation/Platform/TickCountWin32\.")
    # Linux replacement
    list(APPEND ALL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Platform/PlatformLinux.cpp)
endif()

# SDL3 input processing (provides SDLInput_Buffer* for SDL renderers) + dispatch.
# Same on both platforms.
list(APPEND ALL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Input/InputProcessingSdl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Input/InputDispatch.cpp
)

# Global exclusions
list(FILTER ALL_SOURCES EXCLUDE REGEX "Memory/MemTable\.cpp$")
list(FILTER ALL_SOURCES EXCLUDE REGEX ".*soundDummy\.cpp$")
list(FILTER ALL_SOURCES EXCLUDE REGEX "Audio/OpenAL/")
# IO/Filesystem platform sources are added explicitly below via MERGED_SUPPORT_SOURCES.
list(FILTER ALL_SOURCES EXCLUDE REGEX "IO/Filesystem/platform/")
# paramFileUsePreprocC.cpp wires PreprocC as the ParamFile preprocessor.
# paramFileUsePreprocDefault.cpp is the no-preproc fallback; only the PreprocC
# variant ships in the engine — the default would silently drop comments and
# break CfgScenePreload parsing.
list(FILTER ALL_SOURCES EXCLUDE REGEX "IO/ParamFile/ParamFileUsePreprocDefault\.cpp$")

# Foundation/ has no top-level glob (these subdirs hold low-level support code
# we want listed by hand). Math/ and Memory/ have explicit globs above so their
# files are not repeated here.
# IO/Filesystem/platform/* uses a per-OS generator expression — added explicitly
# because the glob filters it out via REGEX above.
# IO/Streams, IO/PreprocC, IO/ParamFile, Asset/Formats/Common, UI/Locale/Stringtable
# all come from their parent subsystem's GLOB_RECURSE.
set(MERGED_SUPPORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Modules/Modules.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Containers/Bitmask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Containers/Maps.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Algorithms/Crc32.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Algorithms/Crc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Time/Time.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Math/Interpol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Math/Statistics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Strings/StrFormat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Strings/Mbcs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Strings/LocaleCollate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Types/LLinks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Threads/PoCritical.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Threads/PoSemaphore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Threads/PoThread.cpp
    $<IF:$<PLATFORM_ID:Windows>,${CMAKE_CURRENT_SOURCE_DIR}/IO/Filesystem/platform/DirScanner_win.cpp,${CMAKE_CURRENT_SOURCE_DIR}/IO/Filesystem/platform/DirScanner_posix.cpp>
    $<IF:$<PLATFORM_ID:Windows>,${CMAKE_CURRENT_SOURCE_DIR}/IO/Filesystem/platform/FileOps_win.cpp,${CMAKE_CURRENT_SOURCE_DIR}/IO/Filesystem/platform/FileOps_posix.cpp>
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Enums/EnumNames.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Common/Platform.cpp
    $<IF:$<PLATFORM_ID:Windows>,${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Common/PlatformPaths_win.cpp,${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Common/PlatformPaths_posix.cpp>
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Common/PlayerPrefs.cpp
    $<IF:$<PLATFORM_ID:Windows>,${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Common/ConsoleUtils_win.cpp,${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Common/ConsoleUtils_posix.cpp>
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Common/GamePaths.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Framework/AppFrame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Framework/ConsoleBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Framework/NetLog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Framework/PoTime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Logging/Logging.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Framework/UseAppFrameDefault.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Strings/RString.cpp
    ${CMAKE_SOURCE_DIR}/engine/Random/randomGen.cpp
    ${CMAKE_SOURCE_DIR}/engine/Evaluator/express.cpp
    ${CMAKE_SOURCE_DIR}/engine/Evaluator/EvalState.cpp
    ${CMAKE_SOURCE_DIR}/engine/Evaluator/MockObjects.cpp
    ${CMAKE_SOURCE_DIR}/engine/Evaluator/SqsRunner.cpp
    ${CMAKE_SOURCE_DIR}/engine/Evaluator/Validate.cpp
    ${CMAKE_SOURCE_DIR}/engine/Evaluator/EvaluatorHost.cpp
    ${CMAKE_SOURCE_DIR}/engine/Evaluator/EvaluatorRuntimeAnchor.cpp
)
list(APPEND ALL_SOURCES ${MERGED_SUPPORT_SOURCES})

# ============================================================
# Source filtering — drop sources that ship in their own backend targets.
# ============================================================

# GL33 backend lives in the dedicated PoseidonGL33 target.
list(FILTER ALL_SOURCES EXCLUDE REGEX "Graphics/OpenGL33/")

# libogg + libvorbis come from vcpkg (find_package below). The 2002-era
# vendored copies under `thirdparty/libogg/` are retired in favour of the
# modern stable releases (libogg 1.3.5, libvorbis 1.3.7) — ~20 years of
# bug fixes, security patches, and decode-perf work. API is stable
# across 1.0 → 1.3 so the engine call sites (`ov_open_callbacks`,
# `ov_read`, `ov_pcm_seek`, `ov_clear`) are unchanged.

# ============================================================
# Create Library Target
# ============================================================

# Single Poseidon static lib. Headless apps (Server, Tools, Studio,
# Evaluator) link Poseidon directly; client apps (Game, GameDemo, Tetris)
# add PoseidonGL33 + PoseidonOpenAL on top. GraphicsEngineFactory is a
# registry — backends in the per-backend libs self-register at static init.
add_library(Poseidon STATIC
    ${ALL_SOURCES}
)

add_custom_target(PoseidonBuildInfo
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_FILE=${POSEIDON_BUILD_INFO_HEADER}
        -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
    BYPRODUCTS "${POSEIDON_BUILD_INFO_HEADER}"
    VERBATIM
)
add_dependencies(Poseidon PoseidonBuildInfo)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Core/Version.cpp" PROPERTIES
    OBJECT_DEPENDS "${POSEIDON_BUILD_INFO_HEADER}"
)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Foundation/Platform/AppConfig.cpp" PROPERTIES
    OBJECT_DEPENDS "${POSEIDON_BUILD_INFO_HEADER}"
)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Game/Scripting/ExpressExt.cpp" PROPERTIES
    OBJECT_DEPENDS "${POSEIDON_BUILD_INFO_HEADER}"
)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/UI/Locale/StringtableExt.cpp" PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/IO/Serialization/StringIds.hpp"
)

# Precompiled header: PoseidonPCH.hpp is the engine-wide prelude the bulk of TUs
# include (extra compiler options, platform + C runtime, core types, Math3D,
# AppFrame). Wiring it as a real PCH cuts cold/uncached clean builds of this
# target ~40%; ccache stays effective so warm builds are unaffected. The app,
# test, and backend targets precompile the same header (see their CMakeLists).
if(NOT POSEIDON_DISABLE_PCH)
    target_precompile_headers(Poseidon PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Foundation/PoseidonPCH.hpp")
endif()

# Export sources for clang-format
set(POSEIDON_MODERN_SOURCES ${POSEIDON_ALL_SOURCES} PARENT_SCOPE)

# ============================================================
# Performance-Critical File Optimization
# ============================================================

set(OPTIMIZED_FILES
    Graphics/Rendering/Draw/ClipDraw.cpp
    Graphics/Rendering/Draw/DrawPoly.cpp
    Core/StdLib.cpp
    Graphics/Rendering/Lighting/TransLight.cpp
)

if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    foreach(file ${OPTIMIZED_FILES})
        set_source_files_properties(${file} PROPERTIES
            COMPILE_FLAGS "/O2"
            SKIP_PRECOMPILE_HEADERS ON
        )
    endforeach()
endif()

# ============================================================
# Compile Definitions
# ============================================================

target_compile_definitions(Poseidon PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
if(WIN32)
    target_compile_definitions(Poseidon PRIVATE
        __MSC__=1100
        _WINDOWS
    )
endif()

# ============================================================
# Compiler Options
# ============================================================

if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    target_compile_options(Poseidon PRIVATE
        /GR /Oi /GF
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Ob1>
        $<$<CONFIG:Debug>:/MDd>
        /permissive
        /wd4291 /wd5030 /wd4799 /wd4068 /wd4996
        /wd4244 /wd4146 /wd4018 /wd4101 /wd4305 /wd4838
        $<$<CXX_COMPILER_ID:Clang>:-Wno-error=non-pod-varargs>
        $<$<CXX_COMPILER_ID:Clang>:-Wno-error=c++11-narrowing>
        $<$<CXX_COMPILER_ID:Clang>:-Wno-error=ordered-compare-function-pointers>
        $<$<CXX_COMPILER_ID:Clang>:-Wno-error>
    )
else()
    target_compile_options(Poseidon PRIVATE
        -Wno-error
        -Wno-c++11-narrowing
        -Wno-non-pod-varargs
        -Wno-char-subscripts
        -Wno-deprecated-volatile
        -Wno-missing-field-initializers
        -Wno-int-to-void-pointer-cast
        -Wno-void-pointer-to-int-cast
    )
endif()

# ============================================================
# Include Directories
# ============================================================

target_include_directories(Poseidon PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/thirdparty
    ${CMAKE_SOURCE_DIR}/thirdparty/glad/include
)

# ============================================================
# Link Dependencies
# ============================================================

# Core dependencies (inherited by Client via PUBLIC link)
target_link_libraries(Poseidon PUBLIC
    spdlog::spdlog
)
if(NOT WIN32)
    target_link_libraries(Poseidon PUBLIC pthread dl)
endif()

if(WIN32)
    # MasterServerServiceClient + master-server publisher call WinSock /
    # WinInet directly — expose those in the public link interface so every
    # consumer of Poseidon picks them up automatically. CrashHandler uses
    # DbgHelp for shared Windows minidump + stack reporting.
    target_link_libraries(Poseidon PUBLIC ws2_32 wininet dbghelp)
endif()

find_package(cJSON CONFIG REQUIRED)
target_link_libraries(Poseidon PUBLIC cjson)

# zstd: downloaded mods ship as zstd-wrapped PBOs (.pbo.zst); ModArchive streams
# the decode on unpack. BSD-3-Clause libzstd.
find_package(zstd CONFIG REQUIRED)
target_link_libraries(Poseidon PRIVATE zstd::libzstd)

# libcurl: the single cross-platform HTTP/HTTPS client for the master-server publisher
# (Schannel on Windows, OpenSSL elsewhere — see vcpkg.json).
find_package(CURL CONFIG REQUIRED)
target_link_libraries(Poseidon PRIVATE CURL::libcurl)

find_package(Opus CONFIG REQUIRED)
target_link_libraries(Poseidon PUBLIC Opus::opus)

# libogg + libvorbis (vcpkg).  Vorbisfile is the high-level API that
# WaveStreamOGG.cpp uses (ov_open_callbacks / ov_read / ov_pcm_seek /
# ov_clear) — pulls in vorbis and ogg transitively.
find_package(Ogg     CONFIG REQUIRED)
find_package(Vorbis  CONFIG REQUIRED)
target_link_libraries(Poseidon PUBLIC Vorbis::vorbisfile Vorbis::vorbis Ogg::ogg)

find_package(CLI11 CONFIG REQUIRED)
target_link_libraries(Poseidon PRIVATE CLI11::CLI11)

find_package(freetype CONFIG REQUIRED)
target_link_libraries(Poseidon PUBLIC Freetype::Freetype)

# stb — PNGWriter/TGAWriter and textures
find_package(Stb REQUIRED)
target_include_directories(Poseidon PRIVATE ${Stb_INCLUDE_DIR})

# mimalloc
find_package(mimalloc CONFIG REQUIRED)
target_link_libraries(Poseidon PUBLIC mimalloc-static)
target_compile_definitions(Poseidon PUBLIC MI_STATIC_LIB)

# SDL3 — input + window
find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(Poseidon PUBLIC SDL3::SDL3)

target_link_libraries(Poseidon PUBLIC enkiTS::enkiTS)

# ImGui — debug overlay (font tuner panel + GL3 backend hooks)
find_package(imgui CONFIG REQUIRED)
target_link_libraries(Poseidon PUBLIC imgui::imgui)

# ============================================================
# C++ Standard
# ============================================================

set_target_properties(Poseidon PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)
