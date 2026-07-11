set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Real-device SDK -- mirrors cmake/toolchains/ios-arm64-clang.cmake (the
# Simulator toolchain) except for CMAKE_OSX_SYSROOT. Requires the Xcode
# generator (see cmake/presets/ios.json's ios-arm64-device preset) for
# code signing/provisioning to work -- Ninja can still cross-compile this
# SDK fine, but doesn't drive Xcode's automatic signing.
set(CMAKE_OSX_SYSROOT iphoneos)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET "17.0")
