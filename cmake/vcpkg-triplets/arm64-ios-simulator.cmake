set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME iOS)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_SYSROOT iphonesimulator)

# Mirrors cmake/vcpkg-triplets/arm64-osx.cmake's deployment-target pin --
# without this, vcpkg's port builds fall back to the host SDK's default
# deployment target, which doesn't match the main project's pin.
set(VCPKG_OSX_DEPLOYMENT_TARGET "17.0")
