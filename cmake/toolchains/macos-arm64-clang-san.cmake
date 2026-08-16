set(CMAKE_SYSTEM_NAME Darwin)

# Native macOS build — prevent CMake from treating this as cross-compilation
set(CMAKE_CROSSCOMPILING FALSE)

# Build for Apple Silicon.
set(CMAKE_OSX_ARCHITECTURES "arm64")

# 64-bit macOS with AddressSanitizer + UBSan and debug symbols for symbolized output.
set(SANITIZER_FLAGS "-arch arm64 -fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer -g")

set(CMAKE_C_FLAGS_INIT   "${SANITIZER_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${SANITIZER_FLAGS}")
