set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ppc64le)

if(NOT DEFINED ENV{CWR_PPC64LE_SYSROOT})
    message(FATAL_ERROR "CWR_PPC64LE_SYSROOT must point at a sysroot from docker/ppc64le-sysroot/export-sysroot.sh")
endif()
set(CMAKE_SYSROOT "$ENV{CWR_PPC64LE_SYSROOT}")

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET powerpc64le-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET powerpc64le-linux-gnu)
set(CMAKE_ASM_COMPILER_TARGET powerpc64le-linux-gnu)

set(CMAKE_C_FLAGS_INIT   "--target=powerpc64le-linux-gnu -mcpu=power8")
set(CMAKE_CXX_FLAGS_INIT "--target=powerpc64le-linux-gnu -mcpu=power8")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld")

set(CMAKE_CROSSCOMPILING_EMULATOR qemu-ppc64le-static -L "${CMAKE_SYSROOT}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/cwr-pkgconfig")
