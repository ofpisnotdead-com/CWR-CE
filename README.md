[![Build](https://github.com/McArdle-Systems/CWR-arm64/actions/workflows/build.yml/badge.svg)](https://github.com/McArdle-Systems/CWR-arm64/actions/workflows/build.yml)

# CWR-arm64 — a community fork with Apple Silicon, Metal, and iOS support

> **This is a modified, unofficial community fork**, not the original program
> and not affiliated with or endorsed by Bohemia Interactive. This fork makes
> the engine build and run natively on Apple Silicon macOS and iOS, with the
> original-style GL33 renderer still available on desktop and a native Metal
> renderer (`--render mtl`, `engine/PoseidonMTL/`) used for Apple platforms.
> iOS builds use Metal only; the Metal backend has reached full parity with
> the GL33 renderer.
>
> No rights to "ARMA", "Operation Flashpoint", or any other Bohemia
> Interactive trademark are claimed or implied — see the Additional Terms in
> [`LICENSE`](LICENSE). Upstream: <https://github.com/BohemiaInteractive/CWR>.

---

<img width="2868" height="1320" alt="CWR running on iOS" src=".github/images/CWR%20iOS.png" />

---

# Arma: Cold War Assault - Remastered

This repository holds the engine and game source code (codename *Poseidon*) behind *Arma: Cold War Assault* — the game first released in 2001 as *Operation Flashpoint: Cold War Crisis*. That release launched Bohemia Interactive and began the technology lineage that later grew into Real Virtuality, Arma, and Enfusion. The code has been modernized to C++20, built with CMake and Clang, with cross-platform support for Windows x64, Linux x64, Apple Silicon macOS, and iOS.
Bohemia Interactive is releasing it to the community that has kept this game alive for more than two decades — to study it, build on it, fix it, and create from it. Three things are worth keeping separate:

**Source code (this repository)**

The engine and game executables, licensed under GPL-3.0-or-later with additional terms under Section 7. You may use, study, modify, and redistribute it, provided it stays GPL and you follow those terms.

**The name and brand**

"ARMA", "Operation Flashpoint", and the logos are *not* granted. The trademarks stay with their owners ("ARMA" is Bohemia Interactive's). A fork must be renamed and must not present itself as "Arma" or as an official Bohemia Interactive product.

**Game data (separate)**

Models, textures, sounds, missions, and voices. These are not in this repository and are not GPL; they ship separately under the APL-SA license. A free Demo is available on Steam.

In short: the code is free software, the name is not, and the game data comes separately. This license covers the source code only and grants no rights to the trademarks.


## Quick Start

```sh
cmake --preset win-x64-clang-rwdi
cmake --build build/win-x64-clang-rwdi
```

On GNU/Linux, use the matching `linux-x64-clang-rwdi` preset.

## How to Build and Run (macOS / Apple Silicon)

One-time setup — vcpkg, if you don't already have it:

```sh
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg   # add to your shell profile to persist
```

Configure and build (RelWithDebInfo; swap in `macos-arm64-clang` for a Debug
build or `macos-arm64-clang-rel` for Release):

```sh
cmake --preset macos-arm64-clang-rwdi
cmake --build build/macos-arm64-clang-rwdi --target PoseidonGame -j8
```

### Clean rebuild

CMake bakes the absolute source/build path into the generated build tree
(`CMakeCache.txt`, `compile_commands.json`, Ninja files). If you move or
rename the repo's checkout directory, reconfigure from scratch rather than
reusing the old build dir:

```sh
rm -rf build/macos-arm64-clang-rwdi
cmake --preset macos-arm64-clang-rwdi
cmake --build build/macos-arm64-clang-rwdi --target PoseidonGame -j8
```

vcpkg's binary cache (`~/.cache/vcpkg/archives` by default) is keyed by
package content/ABI hash, not by path, so this won't trigger a from-source
rebuild of dependencies — they're restored from cache.

Game data: drop your Demo or retail game data into `packages/Remaster/`
(git-ignored local staging dir — see [Getting game data](#getting-game-data-to-run-what-you-build)).
Point the game at that directory with `-C`/`--work-dir` rather than `cd`-ing
into it:

```sh
# Native Metal backend on macOS
build/macos-arm64-clang-rwdi/apps/cwr/Game/PoseidonGame -C packages/Remaster --render mtl --window

# GL33 baseline (cross-platform default renderer, for comparison)
build/macos-arm64-clang-rwdi/apps/cwr/Game/PoseidonGame -C packages/Remaster --render gl33 --window
```

`--window` runs windowed instead of fullscreen — useful for debugging, since
fullscreen puts the game on its own macOS Space. Other useful flags:
`--width`/`-w` and `--height`/`-h` to set resolution, `--no-splash` to skip
the splash screen.

To debug under lldb, wrap the same invocation:

```sh
lldb -o "run -C packages/Remaster --render mtl --window" -- build/macos-arm64-clang-rwdi/apps/cwr/Game/PoseidonGame
```

## How to Build and Run (iOS / arm64)

iOS uses Metal only. `PoseidonGame` builds the app bundle and its `PoseidonMTL`
dependency.

Game data can't be redistributed (see the [Licensing](#license) section below),
so **by default the iOS app ships with no game data at all**. On first
launch, a native pre-boot screen prompts the user to either paste a download
link or import a local `.zip` archive (via the Files app) — the app unpacks it
into its own sandboxed storage and boots from there. A Settings.app toggle
("Reset Game Data on Next Launch") clears the imported data and re-triggers
this screen.

For local development, you can instead bundle `packages/Combined/` straight
into the `.app` (skipping the on-device import screen entirely) by passing
`-DPOSEIDON_IOS_BUNDLE_GAME_DATA=ON` at configure time:

Simulator:

```sh
cmake --preset ios-arm64-simulator-xcode -DPOSEIDON_IOS_BUNDLE_GAME_DATA=ON
cmake --build build/ios-arm64-simulator-xcode --target PoseidonGame -j8
open build/ios-arm64-simulator-xcode/CWR.xcodeproj
```

Device:

```sh
cmake --preset ios-arm64-device -DPOSEIDON_IOS_BUNDLE_GAME_DATA=ON
cmake --build build/ios-arm64-device --target PoseidonGame -j8
open build/ios-arm64-device/CWR.xcodeproj
```

The device preset uses automatic Xcode signing; override
`CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM` locally if needed.

For a distributable build (App Store / TestFlight — no bundled game data,
manual release signing), use the `ios-arm64-device-appstore` preset instead;
see `.github/workflows/testflight.yml` for the full archive/sign/upload
pipeline used for automated TestFlight releases.

### iOS touch controls status

Touch controls are implemented in-engine through SDL3 finger events, not UIKit.
The touch module lives in `engine/Poseidon/Input/TouchInput.*`, is enabled by
default only for iOS builds, and feeds the existing input/controller UI paths.

Current behavior:

- Left virtual stick drives movement in gameplay and cursor movement in views
  that still use cursor-style navigation.
- Vehicle forward/back is bridged from the left stick so touch up/down behaves
  like keyboard throttle/brake in driver contexts.
- Right-side drag controls aim/look in gameplay.
- Right-side tap, when not on another touch button, performs a fire/primary
  click action.
- Touch buttons currently cover Fire, Action, Reload, Optics/Zoom, Map, and
  Pause/Escape.
- Menu-like screens, pause/options views, editor dialogs, and modal dialogs
  support direct tap/drag on existing UI controls.
- Map controls support one-finger primary interaction for taps, selection, and
  drag boxes; two-finger drag pans the map; pinch zooms the map.
- The Action button commits on clean tap release only. Holding the Action button
  never commits by itself; dragging up/down while held scrolls the action menu,
  and release after scrolling or dragging does not execute the highlighted
  action.
- `Options > Controls > Touch Controls` exposes aim sensitivity and cursor
  movement sensitivity; values are saved to `touch.cfg`. Cursor sensitivity uses
  a curved slider so low speeds have more usable adjustment range.

Known touch-control issues:

- Main Menu and Pause Menu still draw only the escape icon plus part of reload
  until entering a submenu or returning from gameplay. The touch hit zones are
  active and near the screen edge, but the overlay visuals are incomplete there.
- Action-menu flick velocity is not tuned yet; current scrolling is based on
  hold-and-drag distance.
- Command bar unit selection and command-menu options are not tappable yet.
- Overlay polish, full vehicle/aircraft parity, and advanced editor gestures are
  still work in progress.

For device debugging, the iOS app currently injects `--no-splash --show-fps`
when those options are not supplied, and writes launch/stdout/stderr logs into
the app preferences directory. To launch from Xcode without the FPS overlay, add
`--no-fps` to the scheme arguments.

## Layout

- [Apps](apps/README.md) - executable targets
- [Engine](engine/README.md) - engine libraries and Rust Trident tooling
- [Master server tools](mserver/README.md) - Rust service and CLI crates
- [Tests](tests/README.md) - test source trees; CI builds and runs them via `ctest` on every push/PR (Linux, macOS, Windows — the iOS device build is compile-only, no simulator/device run in CI)
- `cmake/` - presets, toolchains, vcpkg triplets, and overlay ports
- `docker/` - container support for service and runtime environments
- `fastlane/` - build/upload lane for automated iOS TestFlight releases (`.github/workflows/testflight.yml`)
- `packages/` - ignored local game data staging area
- `resources/` - application icon resources
- `thirdparty/` - vendored third-party headers and sources

## Project Notes

- [Contributing](CONTRIBUTING.md)
- [Credits](CREDITS.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Vendored dependencies](thirdparty/README.md)

## License

The source in this repository is licensed under the **GNU General Public License
v3.0 *or later***, with additional terms under **Section 7** of the GPL. See [`LICENSE`](LICENSE) for the
full text.
This license does not grant you any right to use "ARMA" or any other Bohemia Interactive trademark.

The [`thirdparty/`](thirdparty) directory is **excluded** from the project's GPL
license: it contains vendored third-party code (glad, the RenderDoc API header)
under their own respective licenses — see [`thirdparty/README.md`](thirdparty/README.md).
Dependencies pulled in via vcpkg ([`vcpkg.json`](vcpkg.json)) likewise remain under
their own licenses.

*"ARMA" is a registered trademark of BOHEMIA INTERACTIVE a.s. "OPERATION FLASHPOINT" is a registered trademark of Electronic Arts Inc.
See [`LICENSE`](LICENSE) for information concerning trademarks. This credits file is
informational and does not constitute any grant and/or waiver of rights.*

### Game data / assets — Arma Public License Share Alike (APL-SA)

Game data and assets (models, textures, sounds, missions, etc.) are **not part of
this repository** and are **not** covered by the GPL. They are released separately
by Bohemia Interactive under the **Arma Public License Share Alike (APL-SA)**:

- APL-SA license text: <https://www.bohemia.net/community/licenses/arma-public-license-share-alike>

### Getting game data to run what you build

The compiled binaries need game data to run. You can obtain the **free Demo game
data** on Steam:

- *Arma: Cold War Assault Remastered* Demo on Steam: <https://store.steampowered.com/app/4819000>

The full game data ships with the retail game. Whatever you do with assets is
governed by the APL-SA linked above; whatever you do with this source is governed by
the GPL with additional terms per Section 7 in [`LICENSE`](LICENSE).


## Contributing

Unlike the original upstream release, **this fork is open to contributions**:
fork the repo, branch, and open a pull request. This is a small community
project rather than a formal one, so response times and review turnaround
aren't guaranteed. See [`CONTRIBUTING.md`](CONTRIBUTING.md) for more information.
