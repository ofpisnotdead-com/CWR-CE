set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Native build - prevent CMake from treating this as cross-compilation
set(CMAKE_CROSSCOMPILING FALSE)

# Apple Silicon only for now — Intel Macs are out of scope. This is just the
# arch/deployment-target pin; GL33 runs fine on Apple Silicon today (Apple's
# OpenGL is deprecated but still shipped, internally translated to Metal) --
# this toolchain doesn't imply or require any particular graphics backend.
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
