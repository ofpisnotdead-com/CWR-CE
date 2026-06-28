set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Simulator only for now -- this is the bring-up target (no code signing
# required to run in Simulator). A device toolchain (CMAKE_OSX_SYSROOT
# iphoneos) is a later, separate phase once Simulator parity is proven.
set(CMAKE_OSX_SYSROOT iphonesimulator)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET "17.0")
