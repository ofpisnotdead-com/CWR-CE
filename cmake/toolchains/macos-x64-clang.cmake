set(CMAKE_SYSTEM_NAME Darwin)

# Native macOS build — prevent CMake from treating this as cross-compilation
set(CMAKE_CROSSCOMPILING FALSE)

# Build for AMD64.
set(CMAKE_OSX_ARCHITECTURES "x86_64")

# 64-bit macOS
set(CMAKE_C_FLAGS_INIT   "-m64")
set(CMAKE_CXX_FLAGS_INIT "-m64")
