# Arma: Cold War Assault - Remastered - Community Edition

_based on https://github.com/bohemiainteractive/CWR_

This community repository continues the official engine and game source code (codename *Poseidon*) behind *Arma: Cold War Assault* — the game first released in 2001 as *Operation Flashpoint: Cold War Crisis*. That release launched Bohemia Interactive and began the technology lineage that later grew into Real Virtuality, Arma, and Enfusion. The code has been modernized to C++20, built with CMake and Clang, with cross-platform support for Windows x64 and Linux x64.
Bohemia Interactive released it to the community that has kept this game alive for more than two decades — to study it, build on it, fix it, and create from it. Three things are worth keeping separate:

**Source code (this repository)**

The engine and game executables, licensed under GPL-3.0-or-later with additional terms under Section 7. You may use, study, modify, and redistribute it, provided it stays GPL and you follow those terms.

**The name and brand**

"ARMA", "Operation Flashpoint", and the logos are *not* granted. The trademarks stay with their owners ("ARMA" is Bohemia Interactive's). A fork must be renamed and must not present itself as "Arma" or as an official Bohemia Interactive product.

**Game data (separate)**

Models, textures, sounds, missions, and voices. These are not in this repository and are not GPL; they ship separately under the APL-SA license. A free Demo is available on Steam.

In short: the code is free software, the name is not, and the game data comes separately. This license covers the source code only and grants no rights to the trademarks.


## Quick Start

### Step-by-step Guides

There are installation/build guides for [Linux](docs/build/linux.md) and
[Windows](docs/build/windows.md).

### Development Builds

The quickest way to get the game or server executables is to download the CI builds: <https://ofpisnotdead-com.github.io/CWR-CE-builds/>

### Build Requirements

- [Clang](https://clang.llvm.org/)
- [CMake](https://cmake.org/)
- [Ninja](https://ninja-build.org/)
- [vcpkg](https://vcpkg.io/)

On Windows, run the following commands:

```
winget install Ccache.Ccache Kitware.CMake LLVM.LLVM Ninja-build.Ninja PolarGoose.ClangFormat
winget install Microsoft.VisualStudio.BuildTools --custom '"--add Microsoft.VisualStudio.Workload.VCTools;includeRecommended"'
```

Then follow the instructions for [setting up
vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-powershell)
to get the required software.

### Compiling

```sh
cmake --preset win-x64-clang-rwdi
cmake --build build/win-x64-clang-rwdi
```

On GNU/Linux, use the matching `linux-x64-clang-rwdi` preset.

### Testing

Copy game binaries from `dist/` into the [Steam game's Remastered folder](https://store.steampowered.com/app/65790/Arma_Cold_War_Assault_Remastered/)
or the [Steam demo folder](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/) and start.

> [!WARNING]
> Compiled binaries are not drop-in compatible with original game folder. See https://github.com/ofpisnotdead-com/CWR-CE/issues/8#issuecomment-4772323490 and https://github.com/ofpisnotdead-com/CWR-CE/issues/29#issuecomment-4803747960 for more info.

## Layout

- [Apps](apps/README.md) - executable targets
- [Engine](engine/README.md) - engine libraries and Rust Trident tooling
- [Master server tools](mserver/README.md) - Rust service and CLI crates
- [Tests](tests/README.md) - test source trees; CI currently compiles them only
- `cmake/` - presets, toolchains, vcpkg triplets, and overlay ports
- `docs/` - auxiliary documentation
- `docker/` - container support for service and runtime environments
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

The compiled binaries need game data to run. You can obtain the **free demo game
data** or the paid full game on Steam:

- *Arma: Cold War Assault Remastered* demo on Steam: <https://store.steampowered.com/app/4819000>
- *Arma: Cold War Assault Remastered* full game on Steam: <https://store.steampowered.com/app/65790>

The full game data ships with the retail game. Whatever you do with assets is
governed by the APL-SA linked above; whatever you do with this source is governed by
the GPL with additional terms per Section 7 in [`LICENSE`](LICENSE).


## Contributing

This is the community continuation of the official source release. Pull requests,
bug reports, ports, tooling improvements, documentation updates, and development
ideas are welcome here.

Please use issues for bugs and proposals, and open pull requests for source,
build, test, or documentation changes. See [`CONTRIBUTING.md`](CONTRIBUTING.md)
for contribution guidelines.
