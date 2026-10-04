# Chex Quest Collection — PS Vita

One Vita app for **Chex Quest 1**, **Chex Quest 2**, and **Chex Quest 3: Vanilla Edition**. Pick a game with the D-pad and press **Cross** or **START**.

## Download and install

Get [`ChexQuestCollection.vpk`](https://github.com/SancioPanza88/chex-quest-collection/releases/latest/download/ChexQuestCollection.vpk) from Releases and install it on a homebrew-enabled PS Vita/PSTV.

The VPK contains the launcher and soundtrack, not game data. Copy legally obtained files to `ux0:/data/chexquestcollection/`:

- **Chex Quest 1:** `CHEX.WAD`
- **Chex Quest 2:** `CHEX.WAD` and `CHEX2.WAD`
- **Chex Quest 3 Vanilla:** `chex3v.wad` and `chex3.deh`

## Controls

- Left stick: move; right stick: turn
- D-pad Up: quick-save; Down: quick-load
- D-pad Left/Right: cycle weapons (hold to repeat)
- Cross: use; Square or R: fire; L: run; Triangle: automap; Start: menu

The D-pad controls are the same in all three games. Saves and settings are kept separately for each game.

## Build

Install VitaSDK, then run:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build --parallel 1
```

The GitHub Actions workflow builds the VPK. Game data and third-party trademarks/artwork remain the property of their respective owners; this project does not distribute WAD or DEH files.
