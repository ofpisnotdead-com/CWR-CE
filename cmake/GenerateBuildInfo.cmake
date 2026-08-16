if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()
if(NOT DEFINED BUILD_TYPE)
    set(BUILD_TYPE "")
endif()

execute_process(
    COMMAND git -c safe.directory=${SOURCE_DIR} rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_SHA)
    set(GIT_SHA "unknown")
endif()

set(VERSION_TAG "$ENV{OFPR_BUILD_VERSION_TAG}")
if(VERSION_TAG AND NOT VERSION_TAG MATCHES "^[A-Za-z0-9._-]+$")
    message(FATAL_ERROR "OFPR_BUILD_VERSION_TAG must be a simple token (letters/digits/.-_), got '${VERSION_TAG}'")
endif()

if(VERSION_TAG STREQUAL "")
    set(EFFECTIVE_TAG "${GIT_SHA}")
    set(TAG_SOURCE "git")
    set(RELEASE_BUILD "false")
elseif(VERSION_TAG MATCHES "^[Rr][Ee][Ll][Ee][Aa][Ss][Ee]$")
    set(EFFECTIVE_TAG "release")
    set(TAG_SOURCE "release")
    set(RELEASE_BUILD "true")
else()
    set(EFFECTIVE_TAG "${VERSION_TAG}")
    set(TAG_SOURCE "env")
    set(RELEASE_BUILD "false")
endif()

set(DISPLAY_TAG "${EFFECTIVE_TAG}")
if(DISPLAY_TAG STREQUAL "")
    set(DISPLAY_TAG "<none>")
endif()
message(STATUS "OFPR build: git=${GIT_SHA} version-tag=${DISPLAY_TAG} source=${TAG_SOURCE}")

set(CONTENT [=[
#pragma once

namespace Poseidon::BuildInfo
{
inline constexpr char GitSha[] = "@GIT_SHA@";
inline constexpr char VersionTag[] = "@EFFECTIVE_TAG@";
inline constexpr char BuildType[] = "@BUILD_TYPE@";
inline constexpr bool ReleaseBuild = @RELEASE_BUILD@;
} // namespace Poseidon::BuildInfo
]=])

string(CONFIGURE "${CONTENT}" CONTENT @ONLY)

if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" OLD_CONTENT)
else()
    set(OLD_CONTENT "")
endif()

if(NOT CONTENT STREQUAL OLD_CONTENT)
    get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
    file(WRITE "${OUTPUT_FILE}" "${CONTENT}")
endif()
