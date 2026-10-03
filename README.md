# Chex Quest Collection — PS Vita

Launcher and native PS Vita homebrew port for **Chex Quest 1**, **Chex Quest 2**, and **Chex Quest 3: Vanilla Edition**. The games share one Vita app, a common set of controls, OPL3 audio, and the Doom/doomgeneric engine. Choose a game in the launcher with the D-pad and press **X** or **Start**. To play another title, close the app and open the Collection again.

> This repository and VPK contain code and artwork only. Obtain game data from a legitimate copy; the WAD and DEH files are copyrighted and are neither included nor distributed here.

## Requirements and game files

Install the VPK on a PS Vita/PSTV with a compatible homebrew setup. Copy your legally obtained files to the following paths on the Vita:

| Game | Required files |
| --- | --- |
| Chex Quest 1 | `ux0:/data/chexquestcollection/CHEX.WAD` |
| Chex Quest 2 | `ux0:/data/chexquestcollection/CHEX.WAD` and `ux0:/data/chexquestcollection/CHEX2.WAD` |
| Chex Quest 3: Vanilla Edition | `ux0:/data/chexquestcollection/chex3v.wad` and `ux0:/data/chexquestcollection/chex3.deh` |

CQ2 is loaded as a PWAD over the original Chex Quest IWAD. The CQ3 DEH patch is required by the Vanilla Edition port. File names and paths above are case-sensitive on some storage setups; keep the spelling shown.

## Controls (all three games)

| Action | Input |
| --- | --- |
| Move | Left stick; D-pad directions are reserved for the actions below |
| Turn | Right stick |
| Quick save (slot 0) | D-pad **Up** |
| Quick load (slot 0) | D-pad **Down** |
| Previous / next weapon | D-pad **Left / Right**; hold to repeat |
| Select weapon 1–7 | Tap the matching zone on the upper front touchscreen |
| Fire | Square or R trigger |
| Use / open | Cross |
| Run | L trigger |
| Strafe modifier | Circle |
| Automap | Triangle |
| In-game menu | Start |
| Menu confirm | Select |

Quick save/load are available during gameplay; Down loads the most recent slot-0 save when one exists. Save files and per-game settings are kept separate under `ux0:/data/chexquestcollection/saves/cq1`, `cq2`, `cq3` and `cfg/cq1`, `cq2`, `cq3`. Diagnostic log: `ux0:/data/chexquestcollection/debug.log`.

## Build

Build with VitaSDK and CMake:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build --parallel 1
```

The GitHub Actions workflow builds and uploads `ChexQuestCollection.vpk` as an artifact.

## Credits and legal notes

Engine: [doomgeneric](https://github.com/ozkl/doomgeneric) and its Chocolate Doom-derived components. Vita SDK: [VitaSDK](https://vitasdk.org/). The Collection artwork was supplied in the workspace's `artwork da usare` folder. Chex Quest names, game data, and third-party artwork remain the property of their respective owners; inclusion of a file here does not grant rights to redistribute copyrighted game data.
