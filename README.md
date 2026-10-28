# On Your Own - Immersive Map and Compass Markers

An F4SE plugin for Fallout 4 that lets you choose which markers the game shows you: on the
Pip-Boy world and local maps, on the HUD compass, and floating over targets in the world.
Made for exploration play. Turn off quest markers and undiscovered locations, keep the places you
have found yourself.

Everything is set in the Mod Configuration Menu (MCM). The mod changes only what the UI displays.
It never edits saves, discovery, quests or fast travel, and it replaces no game or interface files.

## Features

**Pip-Boy map**
- Undiscovered locations, discovered locations and discovered settlements, each on or off.
- Quest objectives, miscellaneous objectives and quest door markers, each with
  *Always / World map only / Local map only / Never*.
- Player position, custom waypoint and power armor with the same four choices.
- With the player marker hidden, the world map opens zoomed out and centered.

**HUD**
- Compass: hide the whole compass, or only quest markers, enemies, undiscovered or discovered
  locations, the custom waypoint or the N/E/S/W letters.
- Floating quest markers over targets in the world can be turned off on their own.
- With every HUD option on, the HUD is left completely alone.

## Requirements

- Fallout 4 **1.11.240** (current Steam version) with F4SE 0.7.x, **or**
  Fallout 4 **1.10.163** (old-gen) with F4SE 0.6.23
- [F4SE](https://f4se.silverlock.org/) for that runtime
- [Address Library for F4SE Plugins](https://www.nexusmods.com/fallout4/mods/47327) for that runtime
- [Mod Configuration Menu](https://www.nexusmods.com/fallout4/mods/21497) (needed to change settings)

## Installation

Each release has one ZIP per game version:

| Game version | File |
|---|---|
| 1.11.240 (current Steam version) | `OnYourOwn-<version>.zip` |
| 1.10.163 (old-gen) | `OnYourOwn-<version>-OldGen.zip` |

Install the one for your game with your mod manager, or copy its contents into the game's `Data` folder:

```
Data/F4SE/Plugins/OnYourOwn.dll
Data/MCM/Config/OnYourOwn/config.json
Data/MCM/Config/OnYourOwn/settings.ini
```

Settings are under *Mod Config → On Your Own* and apply when you close the menu.

## Compatibility

- **Immersive HUD (iHUD) / HUDFramework:** compatible. Let iHUD decide when the compass is shown and
  use On Your Own to hide specific markers. "Show compass" off overrides iHUD.
- **Mods that replace `Pipboy_MapPage.swf` or `HUDMenu.swf`:** usually fine. Clip classes are read at
  runtime, but heavily rebuilt interfaces may not be recognised. Unrecognised markers stay visible.
- Other runtimes (earlier next-gen builds, VR) are not supported. Each DLL loads only on its own
  game version, checks the game's code before hooking and stays inactive if it does not match.

## Building

The project uses [xmake](https://xmake.io) and [CommonLibF4](https://github.com/libxse/CommonLibF4)
(a git submodule). Clone with submodules:

```sh
git clone --recursive https://github.com/vito-tivadar/OnYourOwn-FO4-Mod.git
cd OnYourOwn-FO4-Mod
```

### Windows

Requirements: xmake 3.x, Visual Studio 2022 or its Build Tools with the C++ workload, Python 3.

```powershell
scripts/build-windows.ps1
```

This runs the tests, builds both DLLs and writes `dist/OnYourOwn-<version>.zip` and
`dist/OnYourOwn-<version>-OldGen.zip`. Manually:

```powershell
xmake f -p windows -a x64 -m release --oldgen=n -o build
xmake build OnYourOwn

python scripts/oldgen.py headers build/oldgen/ids
xmake f -p windows -a x64 -m release --oldgen=y -o build/oldgen
xmake build OnYourOwn
```

### Old-gen build

The old-gen DLL is built from the same source. CommonLibF4 targets next-gen, so `--oldgen=y`
uses copies of its Address Library ID headers in which the next-gen-only IDs are replaced by the
1.10.163 IDs listed in [oldgen/ids.tsv](oldgen/ids.tsv) (`scripts/oldgen.py headers`), declares
game 1.10.163 in `F4SEPlugin_Version` (read by mod managers), and adds the `F4SEPlugin_Query`
entry point F4SE 0.6 needs. `scripts/oldgen.py check` fails the build
if the DLL uses a next-gen-only ID that is not in that list; find the 1.10.163 ID and add it.

### Linux (cross-build)

Builds a normal MSVC-ABI Windows DLL with `clang-cl` and `lld-link`, using Microsoft's CRT and
Windows SDK downloaded by [xwin](https://github.com/Jake-Shadle/xwin). Requirements: xmake 3.x,
Clang/LLVM (`clang-cl`, `lld-link`, `llvm-lib`, `llvm-ml`, `llvm-rc`), g++, Python 3, curl.

```sh
scripts/setup-toolchain.sh --accept-license   # once; review Microsoft's license terms first
scripts/build-arch.sh                         # both DLLs and ZIPs, like the Windows script
```

### Tests

The game-independent logic has host tests that build on any platform:

```sh
xmake f -m debug --oldgen=n -o build/host-tests
xmake build -g tests
xmake run -g tests
```

GitHub Actions builds and tests every push on Windows and attaches both ZIPs to a draft release
for `v*` tags.

## How it works

See [docs/how-it-works.md](docs/how-it-works.md). In short: the plugin hooks the Pip-Boy map's
data update and filters the markers before the interface draws them, and hides HUD marker clips
every frame. Contributions are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Copyright (C) 2026 Vito Tivadar.

GPL-3.0 with a modding exception, see [LICENSE](LICENSE) and [EXCEPTIONS](EXCEPTIONS).
Third-party code: CommonLibF4 (GPL-3.0 with exceptions), spdlog (MIT), and an adapted
xmake-repo package recipe in `packages/` (Apache-2.0, see `packages/LICENSE-APACHE-2.0.md`).
Release ZIPs contain only this mod's DLL and MCM files.
