set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Native build - prevent CMake from treating this as cross-compilation
set(CMAKE_CROSSCOMPILING FALSE)

set(CMAKE_C_FLAGS_INIT "-m64")
set(CMAKE_CXX_FLAGS_INIT "-m64")

# vptr checks a downcast against RTTI for the target type. The core engine downcasts to
# backend types such as TextureGL33 whose key function lives in a library the tool and
# test binaries do not link, so the check leaves their typeinfo undefined at link time.
set(SANITIZER_FLAGS "-fsanitize=address,undefined -fno-sanitize=alignment,vptr -fno-omit-frame-pointer -g")

set(CMAKE_C_FLAGS_DEBUG_INIT "${SANITIZER_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "${SANITIZER_FLAGS}")

# --as-needed: drop libraries pulled in by pkgconfig deps (e.g. mimalloc's -latomic) that have
# no referenced symbols. Without this, libatomic.so.1 ends up as a NEEDED entry and the static
# ASan runtime rejects it via AsanCheckIncompatibleRT() before main() runs.
# Use CACHE ... FORCE so this overrides a stale cached value on reconfigure.
set(CMAKE_EXE_LINKER_FLAGS "${SANITIZER_FLAGS} -Wl,--as-needed" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${SANITIZER_FLAGS}")
