# Installing on Windows

You can install CWR-CE on Windows by [downloading the development builds](#development-builds) or by [building it yourself](#building-manually), then [moving the binaries into a directory with the game data](#install-binaries-alongside-game-data).

## Development builds

The quickest way to play the game is to download pre-built binaries. Changes to CWR-CE are automatically compiled and tested, and the resulting CI builds are published on the following webpage: <https://ofpisnotdead-com.github.io/CWR-CE-builds/#main>

On this page, the download links for the latest build are sorted to the top of the list. Among the download options, you'll find:

- `Linux-x64-rwdi-Game` - The full game. Intended to be run with the [Steam game data](https://store.steampowered.com/app/65790/Arma_Cold_War_Assault_Remastered/), but you may use it with the [Steam demo data](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/) to unlock usage of additional features (like the editor).
- `Windows-x64-rwdi-GameDemo` - The game demo, with fewer features. Intended to be run with the [Steam demo data](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/).
- `Windows-x64-rwdi-Server` - The multiplayer server.

After downloading the binaries you are interested in, you must [install them](#install-binaries-alongside-game-data).

## Building manually

You may want to compile the code yourself, such as when testing changes made in local development or when you need binaries that aren't published elsewhere. In this case, [CMake](https://cmake.org/) presets can be used to quickly configure and build the project.

### Installing the dependencies

Before anything can be built, you must ensure that the dependencies are installed.

First, run the following commands in PowerShell to install most of the dependencies:

```powershell
winget install Ccache.Ccache Git.Git Kitware.CMake LLVM.LLVM Ninja-build.Ninja PolarGoose.ClangFormat
winget install Microsoft.VisualStudio.BuildTools --custom '"--add Microsoft.VisualStudio.Workload.VCTools;includeRecommended"'
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
```

After this has completed successfully, you will need to follow the instructions for [setting up vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-powershell#1---set-up-vcpkg):

```powershell
git clone https://github.com/microsoft/vcpkg.git $env:USERPROFILE\vcpkg
cd $env:USERPROFILE\vcpkg; .\bootstrap-vcpkg.bat
```

Once vcpkg is set up, the environment variables need to be set:

```powershell
$env:VCPKG_ROOT = "$env:USERPROFILE\vcpkg"
$env:PATH = "$env:VCPKG_ROOT;$env:PATH"
```

Note that these commands will only set the vcpkg environment variables for the current session. For the environment variables to persist, they must be set in the Windows System Environment Variables panel.

### Downloading the repository

The CWR-CE [source code repository](https://github.com/ofpisnotdead-com/CWR-CE) can be cloned with [Git](https://git-scm.com/) using the following command, run in PowerShell:

```powershell
cd $env:USERPROFILE; git clone https://github.com/ofpisnotdead-com/CWR-CE.git
```

Before moving on to the [compiling steps](#compiling), we should make sure we are working in the new directory with the CWR-CE source code:

```powershell
cd CWR-CE
```

### Compiling

The `win-x64-clang-rwdi` preset provides a simpler path for building most of the executable targets with debug symbols. To use the preset, run the following commands in PowerShell:

```powershell
cmake --preset win-x64-clang-rwdi
cmake --build build/win-x64-clang-rwdi
```

Note that these may take a while, especially the first time they are run. Afterwards, the binaries will be available in the `dist\win-x64-clang-rwdi` directory. For example, `dist\win-x64-clang-rwdi\PoseidonGame.exe` is the full game binary.

## Install binaries alongside game data

The game expects to be run alongside remastered game data, including the [Steam game](https://store.steampowered.com/app/65790/Arma_Cold_War_Assault_Remastered/) and [Steam demo](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/). To install the binaries, you must:

1. Install the [game](https://store.steampowered.com/app/65790/Arma_Cold_War_Assault_Remastered/) or [demo](https://store.steampowered.com/app/4819000/Arma_Cold_War_Assault_Remastered_Demo/) within Steam.
2. Copy the binaries into the `Remastered` folder in the game's files, or the top level of the demo's downloaded files. If you aren't sure how to find these, follow the instructions in the [Steam Support article for finding bonus content](https://help.steampowered.com/en/faqs/view/720F-F7D3-0EB2-059F).

After the binaries are in place, they may be run directly, or you may press play in Steam.
