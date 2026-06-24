set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

# Match cmake/toolchains/macos-arm64-clang.cmake's CMAKE_OSX_DEPLOYMENT_TARGET --
# without this, vcpkg's port builds fall back to the host SDK's default deployment
# target (whatever macOS version is installed), which doesn't match the main
# project's pin and produces "built for newer macOS than being linked" warnings.
set(VCPKG_OSX_DEPLOYMENT_TARGET "14.0")
