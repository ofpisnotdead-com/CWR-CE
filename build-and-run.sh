#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PRESET="${PRESET:-macos-arm64-clang-rwdi}"
TARGET="${TARGET:-PoseidonGame}"
JOBS="${JOBS:-8}"
CONTENT_DIR="${CONTENT_DIR:-packages/Combined}"
RENDER="${RENDER:-mtl}"
CONFIGURE="${CONFIGURE:-1}"
XCODE_CONFIG="${XCODE_CONFIG:-Debug}"
ICLOUD=0
DEFAULT_TEST_MISSION="${HOME}/.local/share/Cold War Assault/missions/benchmark.abel"
TEST_MISSION_SET=0
TEST_MISSION=""
GAME_ARGS=()

usage() {
    cat <<EOF
Usage: $(basename "$0") [options] [--] [game args...]

Builds the default macOS debug game target and launches it with useful
development flags.

Environment overrides:
  PRESET       CMake preset to configure/build (default: $PRESET)
  TARGET       CMake target to build (default: $TARGET)
  JOBS         Parallel build jobs (default: $JOBS)
  CONTENT_DIR  Game content directory passed with -C (default: $CONTENT_DIR)
  RENDER       Renderer passed with --render (default: $RENDER)
  CONFIGURE    Run cmake --preset first, 1 or 0 (default: $CONFIGURE)
  XCODE_CONFIG Build configuration for --icloud's Xcode preset (default: $XCODE_CONFIG)

Options:
  --test-mission[=PATH]  Launch a test mission. Without PATH, uses:
                         $DEFAULT_TEST_MISSION
  --icloud               Build the signed, entitled macOS .app via the
                         macos-arm64-xcode preset instead of the regular
                         bare-executable dev build, so CloudSync/iCloud
                         actually works (see PoseidonGame.entitlements).
                         Requires POSEIDON_MACOS_SIGN_BUNDLE + local Xcode
                         signing setup -- this is NOT the everyday build.

Examples:
  ./build-and-run.sh
  ./build-and-run.sh --test-mission
  ./build-and-run.sh --test-mission="\$HOME/.local/share/Cold War Assault/missions/benchmark.abel"
  RENDER=gl33 ./build-and-run.sh --no-sound
  CONFIGURE=0 JOBS=12 ./build-and-run.sh
  ./build-and-run.sh --icloud
EOF
}

while (($# > 0)); do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --test-mission=*)
            TEST_MISSION_SET=1
            TEST_MISSION="${1#*=}"
            shift
            ;;
        --test-mission)
            TEST_MISSION_SET=1
            if [[ $# -ge 2 && "${2:0:1}" != "-" ]]; then
                TEST_MISSION="$2"
                shift 2
            else
                TEST_MISSION="$DEFAULT_TEST_MISSION"
                shift
            fi
            ;;
        --icloud)
            ICLOUD=1
            shift
            ;;
        --)
            shift
            GAME_ARGS+=("$@")
            break
            ;;
        *)
            GAME_ARGS+=("$1")
            shift
            ;;
    esac
done

if [[ "$TEST_MISSION_SET" == "1" ]]; then
    if ((${#GAME_ARGS[@]} > 0)); then
        GAME_ARGS=(--test-mission "$TEST_MISSION" "${GAME_ARGS[@]}")
    else
        GAME_ARGS=(--test-mission "$TEST_MISSION")
    fi
fi

BUILD_ARGS=()
if [[ "$ICLOUD" == "1" ]]; then
    # macos-arm64-xcode bakes in POSEIDON_MACOS_SIGN_BUNDLE=ON + Automatic
    # signing (see cmake/presets/macos.json) -- a real signed .app, not the
    # bare unsigned executable the other presets produce, which iCloud can't
    # touch at all. Multi-config Xcode generator: needs --config, and the
    # binary lands inside a per-config .app bundle, not flat in Game/.
    PRESET="macos-arm64-xcode"
    BUILD_ARGS+=(--config "$XCODE_CONFIG")
fi

BUILD_DIR="$ROOT_DIR/build/$PRESET"
if [[ "$ICLOUD" == "1" ]]; then
    GAME_BIN="$BUILD_DIR/apps/cwr/Game/$XCODE_CONFIG/PoseidonGame.app/Contents/MacOS/PoseidonGame"
else
    GAME_BIN="$BUILD_DIR/apps/cwr/Game/PoseidonGame"
fi

if [[ "$CONFIGURE" != "0" ]]; then
    cmake --preset "$PRESET"
fi

cmake --build "$BUILD_DIR" --target "$TARGET" -j "$JOBS" ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"}

exec "$GAME_BIN" \
    --no-splash \
    -C "$CONTENT_DIR" \
    --render "$RENDER" \
    --dev \
    --show-fps \
    ${GAME_ARGS[@]+"${GAME_ARGS[@]}"}
