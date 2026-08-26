set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Native build - prevent CMake from treating this as cross-compilation
set(CMAKE_CROSSCOMPILING FALSE)

# 64-bit Linux
set(CMAKE_C_FLAGS_INIT   "-m64")
set(CMAKE_CXX_FLAGS_INIT "-m64")

# Ensure correct cpu type is set for libpng neon
set(CMAKE_SYSTEM_PROCESSOR aarch64 CACHE STRING "")

# Force mimalloc armv8.0 for Raspberry Pi compatibility
set(MI_NO_OPT_ARCH ON)
