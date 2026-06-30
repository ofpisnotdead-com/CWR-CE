# Installing on Linux

You can install CWR-CE on GNU/Linux by [downloading the development builds](#development-builds) or by [building it yourself](#building-manually), then [moving the binaries into a directory with the game data](#install-binaries-alongside-game-data).

## Development builds

The quickest way to play the game is to download pre-built binaries. Changes to CWR-CE are automatically compiled and tested, and the resulting CI builds are published on the following webpage: <https://ofpisnotdead-com.github.io/CWR-CE-builds/#main>

On this page, the download links for the latest build are sorted to the top of the list. Among the download options, you'll find:

- `Linux-x64-rwdi-Game` - The full game. Intended to be run with the [Steam game data](https://store.steampowered.com/app/65790/Arma_Cold_War_Assault_Remastered/), but you may use it with the [Steam demo data](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/) to unlock usage of additional features (like the editor).
- `Linux-x64-rwdi-GameDemo` - The game demo, with fewer features. Intended to be run with the [Steam demo data](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/).
- `Linux-x64-rwdi-Server` - The multiplayer server.

After downloading the binaries you are interested in, you must [install them](#install-binaries-alongside-game-data).

## Building manually

You may want to compile the code yourself, such as when testing changes made in local development or when you need binaries that aren't published elsewhere. In this case, [CMake](https://cmake.org/) presets can be used to quickly configure and build the project.

### Installing the dependencies

Before anything can be built, you must ensure that the dependencies are installed. See the section below for steps that correspond to your Linux distribution.

#### Arch Linux

Run the following command in a terminal (with elevated privildges, such as with `su` or `sudo`) to install the dependencies:

```shell
pacman -S autoconf-archive automake ccache clang git libtool libxcursor libxinerama libxkbcommon libxrandr libxtst make opengl-driver pkgconf python vcpkg wayland wayland-protocols
```

After this has completed successfully, you will need to follow the instructions in the post-install script to complete the setup for vcpkg:

```shell
# Set up the environment variables without needing to load a new environment
source /etc/profile.d/vcpkg.sh
# Install vcpkg recipes
git clone https://github.com/microsoft/vcpkg $VCPKG_ROOT
```

#### Debian/Ubuntu

Run the following command in a terminal to install most of the dependencies:

```shell
sudo apt-get install autoconf-archive automake ccache clang clang-format cmake curl git libtool libltdl-dev libgl1-mesa-dev libwayland-dev libx11-dev libxcursor-dev libxext-dev libxi-dev libxinerama-dev libxkbcommon-dev libxrandr-dev libxtst-dev make ninja-build pkg-config python3 python3-venv tar unzip wayland-protocols zip
```

After this has completed successfully, you will need to run the following commands to setup vcpkg:

```shell
git clone https://github.com/microsoft/vcpkg ~/.local/share/vcpkg
cd ~/.local/share/vcpkg; ./bootstrap-vcpkg.sh
```

Once vcpkg is set up, the environment variables need to be set:

```
echo "# Configure vcpkg\nexport VCPKG_ROOT=$HOME/.local/share/vcpkg\nexport PATH=$PATH:$VCPKG_ROOT" >> ~/.bash_profile
source ~/.bash_profile
```

#### Fedora

Run the following command in a terminal to install the dependencies:

```shell
sudo dnf install autoconf-archive ccache clang clang-tools-extra git libGL-devel libtool libtool-ltdl-devel libX11-devel libXcursor-devel libXext-devel libXi-devel libXinerama-devel libxkbcommon-devel libXrandr-devel libXtst-devel perl-FindBin perl-IPC-Cmd perl-Time-Piece pkgconf python3 vcpkg wayland-devel wayland-protocols-devel
```

After this has completed successfully, you will need to follow the instructions in the `README.fedora` file to complete the setup for vcpkg:

```shell
# Set up the environment variables without needing to load a new environment
source /etc/profile.d/vcpkg.sh 
# Install vcpkg recipes
git clone https://github.com/microsoft/vcpkg $VCPKG_ROOT
```

### Downloading the repository

The CWR-CE [source code repository](https://github.com/ofpisnotdead-com/CWR-CE) can be cloned with [Git](https://git-scm.com/) using the following command, run in a terminal:

```shell
cd ~; git clone https://github.com/ofpisnotdead-com/CWR-CE.git
```

Before moving on to the [compiling steps](#compiling), we should make sure we are working in the new directory with the CWR-CE source code:

```shell
cd CWR-CE
```

### Compiling

The `linux-x64-clang-rwdi` preset provides a simpler path for building most of the executable targets with debug symbols. To use the preset, run the following commands in a terminal:

```shell
cmake --preset linux-x64-clang-rwdi
cmake --build build/linux-x64-clang-rwdi
```

Note that these may take a while, especially the first time they are run. Afterwards, the binaries will be available in the `dist/linux-x64-clang-rwdi` directory. For example, `dist/linux-x64-clang-rwdi/PoseidonGame` is the full game binary.

## Install binaries alongside game data

The game expects to be run alongside remastered game data, including the [Steam game](https://store.steampowered.com/app/65790/Arma_Cold_War_Assault_Remastered/) and [Steam demo](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/). To install the binaries, you must:

1. Install the [game](https://store.steampowered.com/app/65790/Arma_Cold_War_Assault_Remastered/) or [demo](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/) within Steam.
2. Copy the binaries into the `Remastered` folder in the game's files, or the top level of the demo's downloaded files. If you aren't sure how to find these, follow the instructions in the [Steam Support article for finding bonus content](https://help.steampowered.com/en/faqs/view/720F-F7D3-0EB2-059F).

After the binaries are in place, they may be run directly, or you may press play in Steam.
