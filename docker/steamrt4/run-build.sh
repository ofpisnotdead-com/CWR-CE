#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="${OFPR_REPO_DIR:-/work/ofpr}"
PRESET="${OFPR_PRESET:-linux-x64-steamrt4}"
TARGETS_STRING="${OFPR_TARGETS:-Release}"
SKIP_CARGO="${OFPR_SKIP_CARGO:-1}"
VERSION_TAG="${OFPR_VERSION_TAG:-}"
VERSION_TAG_SET="${OFPR_VERSION_TAG_SET:-0}"
RELEASE_MODE="${OFPR_RELEASE:-0}"
HOME_DIR="${OFPR_HOME:-$REPO_DIR/tmp/steamrt4-home}"
BUILD_DIR="$REPO_DIR/build/$PRESET"
TRIDENT_DIR="$REPO_DIR/engine/Trident"
SEED_VCPKG_ROOT="${STEAMRT_VCPKG_SEED:-/opt/vcpkg-seed}"
PARALLEL_LEVEL="${OFPR_PARALLEL:-$(nproc)}"
CCACHE_DIR="${OFPR_CCACHE_DIR:-$REPO_DIR/tmp/steamrt4-ccache}"

convert_to_dist_preset_name() {
    local preset="$1"
    if [[ "$preset" =~ ^(win|linux)-([^-]+)(-(.*))?$ ]]; then
        local platform="${BASH_REMATCH[1]}"
        local arch="${BASH_REMATCH[2]}"
        local suffix="${BASH_REMATCH[4]:-}"
        if [[ -n "$suffix" && "$suffix" == clang-* ]]; then
            suffix="${suffix#clang-}"
        elif [[ "$suffix" == "clang" ]]; then
            suffix=""
        fi

        if [[ -z "$suffix" ]]; then
            printf '%s-%s\n' "$arch" "$platform"
        else
            printf '%s-%s-%s\n' "$arch" "$platform" "$suffix"
        fi
        return
    fi

    printf '%s\n' "$preset"
}

DIST_PRESET="$(convert_to_dist_preset_name "$PRESET")"
DIST_DIR="$REPO_DIR/dist/$DIST_PRESET"

if [ "$RELEASE_MODE" = "1" ]; then
    if [ "$VERSION_TAG_SET" = "1" ] && [ "$VERSION_TAG" != "release" ]; then
        echo "error: OFPR_RELEASE=1 cannot be combined with OFPR_VERSION_TAG=$VERSION_TAG" >&2
        exit 1
    fi
    VERSION_TAG="release"
    VERSION_TAG_SET=1
fi

if [ "$VERSION_TAG_SET" = "1" ] && [[ -n "$VERSION_TAG" && ! "$VERSION_TAG" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "error: OFPR_VERSION_TAG must be a simple token (letters/digits/.-_), e.g. rc1 or release" >&2
    exit 1
fi

mkdir -p "$HOME_DIR"
export HOME="$HOME_DIR"
mkdir -p "$CCACHE_DIR"
export CCACHE_DIR
export CCACHE_BASEDIR="$REPO_DIR"
export CCACHE_NOHASHDIR=true

VCPKG_BASELINE="$(sed -n 's/.*"builtin-baseline"[[:space:]]*:[[:space:]]*"\([0-9a-fA-F]*\)".*/\1/p' "$REPO_DIR/vcpkg.json" | head -n 1)"

vcpkg_has_baseline() {
    local root="$1"
    [ -z "$VCPKG_BASELINE" ] || git -c safe.directory="$root" -C "$root" cat-file -e "$VCPKG_BASELINE:versions/baseline.json" >/dev/null 2>&1
}

if [ ! -f "$VCPKG_ROOT/vcpkg" ] || [ -f "$VCPKG_ROOT/.git/shallow" ] || ! vcpkg_has_baseline "$VCPKG_ROOT"; then
    if ! vcpkg_has_baseline "$SEED_VCPKG_ROOT"; then
        echo "error: vcpkg seed cannot resolve builtin-baseline '$VCPKG_BASELINE'; rebuild the SteamRT image" >&2
        exit 1
    fi
    mkdir -p "$VCPKG_ROOT"
    find "$VCPKG_ROOT" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
    cp -a "$SEED_VCPKG_ROOT/." "$VCPKG_ROOT/"
fi

export PATH="$VCPKG_ROOT:$PATH"

cd "$REPO_DIR"

read -r -a TARGETS <<< "$TARGETS_STRING"

if [ "$#" -gt 0 ]; then
    exec "$@"
fi

if [ -f "$BUILD_DIR/CMakeCache.txt" ] && ! grep -Fq "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" "$BUILD_DIR/CMakeCache.txt"; then
    rm -rf "$BUILD_DIR"
fi

if command -v ccache >/dev/null 2>&1 && [ -f "$BUILD_DIR/CMakeCache.txt" ] && ! grep -Fq "CMAKE_C_COMPILER_LAUNCHER:UNINITIALIZED=ccache" "$BUILD_DIR/CMakeCache.txt" && ! grep -Fq "CMAKE_C_COMPILER_LAUNCHER:STRING=ccache" "$BUILD_DIR/CMakeCache.txt"; then
    rm -rf "$BUILD_DIR"
fi

configure_args=(--preset "$PRESET")
# BUILD_VERSION_TAG is a CMake cache value. Always pass it so omitting --tag
# clears any tag left by a previous tagged build.
configure_args+=("-DBUILD_VERSION_TAG=$VERSION_TAG")
if command -v ccache >/dev/null 2>&1; then
    configure_args+=(
        -D CMAKE_C_COMPILER_LAUNCHER=ccache
        -D CMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
fi
cmake "${configure_args[@]}"

build_args=(
    --build "$BUILD_DIR"
    --parallel "$PARALLEL_LEVEL"
)

if [ "${#TARGETS[@]}" -gt 0 ]; then
    build_args+=(--target "${TARGETS[@]}")
fi

export OFPR_BUILD_VERSION_TAG="$VERSION_TAG"
cmake "${build_args[@]}"

if [ "$SKIP_CARGO" != "1" ] && [ -f "$TRIDENT_DIR/Cargo.toml" ]; then
    if ! command -v cargo >/dev/null 2>&1; then
        echo "error: cargo requested but not available in container" >&2
        exit 1
    fi
    if [ -f "$REPO_DIR/Cargo.toml" ]; then
        cargo build --manifest-path "$REPO_DIR/Cargo.toml"
        tri_binary="$REPO_DIR/target/debug/tri"
    else
        cargo build --manifest-path "$TRIDENT_DIR/Cargo.toml"
        tri_binary="$TRIDENT_DIR/target/debug/tri"
    fi
    if [ -f "$tri_binary" ]; then
        mkdir -p "$DIST_DIR"
        cp "$tri_binary" "$DIST_DIR/tri"
    fi
fi
