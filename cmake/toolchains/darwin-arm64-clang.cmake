set(CMAKE_SYSTEM_NAME Darwin)

# Native macOS build — prevent CMake from treating this as cross-compilation
set(CMAKE_CROSSCOMPILING FALSE)

# Build for Apple Silicon.
set(CMAKE_OSX_ARCHITECTURES "arm64")

# 64-bit macOS
set(CMAKE_C_FLAGS_INIT   "-arch arm64")
set(CMAKE_CXX_FLAGS_INIT "-arch arm64")
