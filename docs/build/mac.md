# Installing on macOS

You can install CWR-CE on macOS by [downloading the development builds](#development-builds) or by [building it yourself](#building-manually), then [moving the binaries into a directory with the game data](#install-binaries-alongside-game-data).

## Development builds

The quickest way to play the game is to download pre-built binaries. Changes to CWR-CE are automatically compiled and tested, and the resulting CI builds are published on the following webpage: <https://ofpisnotdead-com.github.io/CWR-CE-builds/#main>

On this page, the download links for the latest build are sorted to the top of the list. Builds are published for Apple Silicon (`arm64`) and Intel (`x64`) Macs. Among the download options, you'll find:

- `macOS-arm64-rwdi-Game` - The full game. Intended to be run with the [Steam](https://store.steampowered.com/app/65790/Arma_Cold_War_Assault_Remastered/) or [GOG](https://www.gog.com/en/game/arma_cold_war_assault) game data, but you may use it with the [Steam demo data](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/) to unlock usage of additional features (like the editor).
- `macOS-arm64-rwdi-GameDemo` - The game demo, with fewer features. Intended to be run with the [Steam demo data](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/).
- `macOS-arm64-rwdi-Server` - The multiplayer server.

Use the `macOS-x64-rwdi-*` downloads instead on an Intel Mac.

After downloading the binaries you are interested in, you must [install them](#install-binaries-alongside-game-data).

## Building manually

You may want to compile the code yourself, such as when testing changes made in local development or when you need binaries that aren't published elsewhere. In this case, [CMake](https://cmake.org/) presets can be used to quickly configure and build the project.

### Installing the dependencies

Before anything can be built, you must ensure that the dependencies are installed.

First, install the Xcode Command Line Tools, which provide Clang and Git. Run the following command in Terminal and confirm the dialog that appears:

```shell
xcode-select --install
```

Next, install [Homebrew](https://brew.sh/) by following the instructions on its website, including the commands it prints at the end to add `brew` to your shell environment.

Once Homebrew is available, run the following command to install the remaining dependencies:

```shell
brew install ccache clang-format cmake ninja pkg-config vcpkg
```

After this has completed successfully, you will need to download the vcpkg recipes:

```shell
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
```

Once vcpkg is set up, the environment variable needs to be set. The default macOS shell is zsh; use `~/.bash_profile` instead of `~/.zprofile` if you use bash:

```shell
echo 'export VCPKG_ROOT="$HOME/vcpkg"' >> ~/.zprofile
source ~/.zprofile
```

### Downloading the repository

Clone the CWR-CE [source code repository](https://github.com/ofpisnotdead-com/CWR-CE) using the following command, run in Terminal:

```shell
cd ~; git clone https://github.com/ofpisnotdead-com/CWR-CE.git
```

Before moving on to the [compiling steps](#compiling), we should make sure we are working in the new directory with the CWR-CE source code:

```shell
cd CWR-CE
```

### Compiling

The `macos-arm64-clang-rwdi` preset provides a simpler path for building most of the executable targets with debug symbols. On an Intel Mac, use `macos-x64-clang-rwdi` instead. To use the preset, run the following commands in Terminal:

```shell
cmake --preset macos-arm64-clang-rwdi
cmake --build build/macos-arm64-clang-rwdi
```

Note that these may take a while, especially the first time they are run. Afterwards, the binaries will be available in the `dist/arm64-macos-rwdi` directory (`dist/x64-macos-rwdi` on Intel). For example, `dist/arm64-macos-rwdi/PoseidonGame` is the full game binary.

## Install binaries alongside game data

The game expects to be run alongside remastered game data, including the [Steam game](https://store.steampowered.com/app/65790/Arma_Cold_War_Assault_Remastered/), [GOG game](https://www.gog.com/en/game/arma_cold_war_assault), and [Steam demo](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/). To install the binaries, you must:

1. Get the game data from the [game](https://store.steampowered.com/app/65790/Arma_Cold_War_Assault_Remastered/) or [demo](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/) on Steam, or the [game from GOG](https://www.gog.com/en/game/arma_cold_war_assault). The game is not published for macOS, so you may need to install it on a Windows or Linux computer and copy the `Remastered` folder (or the top level of the demo's files) to your Mac.
2. Copy the binaries, together with `libopenal.dylib`, into that folder.

After the binaries are in place, run them from Terminal inside that folder, or point the game at the data with `-C`:

```shell
./PoseidonGame -C /path/to/Remastered
```
