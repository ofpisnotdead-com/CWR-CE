set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Native build - prevent CMake from treating this as cross-compilation
set(CMAKE_CROSSCOMPILING FALSE)

# 64-bit little-endian, baseline POWER8 (the first CPU to support ppc64le).
set(CMAKE_C_FLAGS_INIT   "-m64 -mcpu=power8")
set(CMAKE_CXX_FLAGS_INIT "-m64 -mcpu=power8")

set(CMAKE_SYSTEM_PROCESSOR ppc64le CACHE STRING "")
