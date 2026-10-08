# Chex Quest Collection — PS Vita

One Vita app for **Chex Quest 1**, **Chex Quest 2** and **Chex Quest 3: Vanilla Edition**. Pick a game in the launcher and press **Cross** or **START** to play it.

The launcher is a proper menu now: it lists the three games plus a data-files row, shows at a glance which games are ready, plays menu music and blips, and can hand you the game data with a QR code if something is missing.

## Screenshots

| Launcher | Game data missing |
|---|---|
| ![Launcher with all three games ready](docs/screenshots/launcher-ready.png) | ![Launcher showing two games as missing](docs/screenshots/launcher-missing.png) |

| Data files and QR code | Data screen, nothing installed yet |
|---|---|
| ![Data files screen with QR code](docs/screenshots/data-qr.png) | ![Data screen reporting the missing files](docs/screenshots/data-missing.png) |

| Controls | Return to launcher | Quick save |
|---|---|---|
| ![Controls screen](docs/screenshots/controls.png) | ![Hold L, R and Select to return to the launcher](docs/screenshots/return-to-launcher.png) | ![Quick saved confirmation](docs/screenshots/quick-save.png) |

## Download and install

Get [`ChexQuestCollection.vpk`](https://github.com/SancioPanza88/chex-quest-collection/releases/latest/download/ChexQuestCollection.vpk) from Releases and install it on a homebrew-enabled PS Vita/PSTV (VitaShell, or any VPK installer).

## Installation and game data

The VPK contains the launcher, **not** the game data. Copy your legally obtained files directly into `ux0:/data/chexquestcollection/`:

- `CHEX.WAD`
- `CHEX2.WAD`
- `chex3v.wad` and `chex3.deh`

**Important:** put all the files together in the `chexquestcollection` folder itself. Do **not** create subfolders (for example `chexquestcollection/chexquest/` or `chexquestcollection/chex3/`): the launcher will not find files placed inside subfolders.

Expected layout:

```
ux0:/data/chexquestcollection/
├── CHEX.WAD
├── CHEX2.WAD
├── chex3v.wad
└── chex3.deh
```

If files are missing, the launcher says so instead of failing:

- every game row shows **READY** or **MISSING**, and the header counts the installed games (`READY 2/3`);
- selecting the **DATA FILES** row (or pressing **Square**, or pressing **Cross** on a game that has no data yet) opens a screen with a **QR code** to download the data archive with your phone, a printable link, the folder to copy the files into, and a live checklist of the four files;
- when the launcher starts with no game data at all, it opens that data screen first;
- the status list is re-checked once per second, so files copied over FTP show up without restarting the app.

## PS Vita controls

The D-pad works in the Doom menus too, and every action is the same in all three games.

| In game | |
|---|---|
| Left stick | move |
| Right stick | turn |
| Cross | use |
| Square or R | fire |
| L | run |
| Triangle | automap |
| START | menu |
| D-pad Up | quick save |
| D-pad Down | quick load |
| D-pad Left/Right | cycle weapons (hold to repeat) |

| In menus | |
|---|---|
| D-pad Up/Down | move |
| D-pad Left/Right | change value |
| Cross | select |
| START | close |

Quick saves and loads confirm themselves with a short on-screen message ("QUICK SAVED", "QUICK LOADED", or "NO QUICK SAVE YET"). Saves and settings are kept separately for each game, under `ux0:/data/chexquestcollection/saves/<game>/` and `cfg/<game>/`.

To leave a game and go back to the launcher, **hold L + R + SELECT for one second**: a progress bar fills up and the app reloads into the launcher.

## Build

Install VitaSDK, then run:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build --parallel 1
```

The contract tests check the launcher and data contracts against the sources:

```sh
python -m unittest tests.test_collection_contract
```

Game data and third-party trademarks/artwork remain the property of their respective owners; this project does not distribute WAD or DEH files.
