# Chex Quest Collection — PS Vita

Launcher and native PS Vita homebrew port for **Chex Quest 1**, **Chex Quest 2**, and **Chex Quest 3: Vanilla Edition**. The games share one Vita app, a common set of controls, OPL3 audio, and the Doom/doomgeneric engine. Choose a game in the launcher with the D-pad and press **✕** or **START**. To play another title, close the app and open the Collection again.

<p align="center">
  <img src="sce_sys/livearea/contents/bg.png" alt="Chex Quest Collection artwork" width="840">
</p>

<p align="center"><em>Artwork della Chex Quest Collection per PS Vita.</em></p>

<p align="center">
  <img src="https://raw.githubusercontent.com/SancioPanza88/chexquest-vita/main/assets/1.webp" alt="Screenshot di gioco di Chex Quest su PS Vita" width="800">
</p>

<p align="center"><em>Screenshot di Chex Quest su PS Vita — cattura del port originale.</em></p>

> This repository and VPK contain code and artwork only. Obtain game data from a legitimate copy; the WAD and DEH files are copyrighted and are neither included nor distributed here.

## Requirements and game files

Install the VPK on a PS Vita/PSTV with a compatible homebrew setup. Copy your legally obtained files to the following paths on the Vita:

| Game | Required files |
| --- | --- |
| Chex Quest 1 | `ux0:/data/chexquestcollection/CHEX.WAD` |
| Chex Quest 2 | `ux0:/data/chexquestcollection/CHEX.WAD` and `ux0:/data/chexquestcollection/CHEX2.WAD` |
| Chex Quest 3: Vanilla Edition | `ux0:/data/chexquestcollection/chex3v.wad` and `ux0:/data/chexquestcollection/chex3.deh` |

CQ2 is loaded as a PWAD over the original Chex Quest IWAD. The CQ3 DEH patch is required by the Vanilla Edition port. File names and paths above are case-sensitive on some storage setups; keep the spelling shown.

## Comandi PS Vita (validi per tutti e tre i giochi)

| Azione | Tasto PS Vita |
| --- | --- |
| Muovi / cammina | Levetta sinistra |
| Gira | Levetta destra |
| Salvataggio rapido (slot 0) | <img src="https://commons.wikimedia.org/wiki/Special:Redirect/file/PlayStation_Up_button.svg" alt="D-pad su" height="24"> D-pad su |
| Caricamento rapido (slot 0) | <img src="https://commons.wikimedia.org/wiki/Special:Redirect/file/PlayStation_Down_button.svg" alt="D-pad giù" height="24"> D-pad giù |
| Arma precedente / successiva | <img src="https://commons.wikimedia.org/wiki/Special:Redirect/file/PlayStation_Left_button.svg" alt="D-pad sinistra" height="24"> / <img src="https://commons.wikimedia.org/wiki/Special:Redirect/file/PlayStation_Right_button.svg" alt="D-pad destra" height="24">; tieni premuto per scorrere |
| Seleziona arma 1–7 | Tocca la zona corrispondente nella fascia alta del touchscreen anteriore |
| Spara | <img src="https://commons.wikimedia.org/wiki/Special:Redirect/file/PlayStationSquare.svg" alt="Quadrato" height="24"> **Quadrato** oppure **R** |
| Usa / apri porte | <img src="https://commons.wikimedia.org/wiki/Special:Redirect/file/PlayStationCross.svg" alt="Croce" height="24"> **Croce** |
| Corri | **L** |
| Movimento laterale (modificatore) | <img src="https://commons.wikimedia.org/wiki/Special:Redirect/file/PlayStationCircle.svg" alt="Cerchio" height="24"> **Cerchio** + levetta sinistra |
| Mappa (automap) | <img src="https://commons.wikimedia.org/wiki/Special:Redirect/file/PlayStationTriangle.svg" alt="Triangolo" height="24"> **Triangolo** |
| Menu di gioco | **START** |
| Conferma nei menu | **SELECT** |

Le frecce del D-pad hanno la stessa funzione in tutti e tre i giochi: **↑ salva**, **↓ carica**, **← / → cambia arma**. Non sono usate per muoversi; per il movimento usa la levetta sinistra.

Salvataggio e caricamento rapidi sono disponibili durante una partita; **D-pad ↓** carica il salvataggio dello slot 0 se presente. Salvataggi e impostazioni sono separati per gioco in `ux0:/data/chexquestcollection/saves/cq1`, `cq2`, `cq3` e `cfg/cq1`, `cq2`, `cq3`. Log diagnostico: `ux0:/data/chexquestcollection/debug.log`.

## Build

Build with VitaSDK and CMake:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build --parallel 1
```

The GitHub Actions workflow builds and uploads `ChexQuestCollection.vpk` as an artifact.

## Credits and legal notes

Engine: [doomgeneric](https://github.com/ozkl/doomgeneric) and its Chocolate Doom-derived components. Vita SDK: [VitaSDK](https://vitasdk.org/). The Collection artwork was supplied in the workspace's `artwork da usare` folder. PlayStation button SVGs are linked from Wikimedia Commons: [D-pad up](https://commons.wikimedia.org/wiki/File:PlayStation_Up_button.svg), [down](https://commons.wikimedia.org/wiki/File:PlayStation_Down_button.svg), [left](https://commons.wikimedia.org/wiki/File:PlayStation_Left_button.svg), [right](https://commons.wikimedia.org/wiki/File:PlayStation_Right_button.svg), [Square](https://commons.wikimedia.org/wiki/File:PlayStationSquare.svg), [Cross](https://commons.wikimedia.org/wiki/File:PlayStationCross.svg), [Circle](https://commons.wikimedia.org/wiki/File:PlayStationCircle.svg), and [Triangle](https://commons.wikimedia.org/wiki/File:PlayStationTriangle.svg). Chex Quest names, game data, and third-party artwork remain the property of their respective owners; inclusion of a file here does not grant rights to redistribute copyrighted game data.
