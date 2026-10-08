# Chex Quest Collection — v1.2 development plan

This is the **private staging repo**. Features for the next release are built and
tested here before they go to the public repo `SancioPanza88/chex-quest-collection`.

## Branches

| Branch | Purpose |
| --- | --- |
| `main` | mirror of the current public release (v1.1.0, commit `21316f1`). Keep it identical to the public repo so diffs stay readable. |
| `v1.2-dev` | next-release work. |

Release flow: develop on `v1.2-dev` → grab the VPK from the CI pre-release →
test on hardware → merge into `main` → push to `origin` (public) and tag the
release there.

The GitHub Actions workflow builds a VPK on every push. In this private repo the
VPK to test is in the rolling pre-release:

```
gh release download ci-latest -R SancioPanza88/chex-quest-collection-next -p "*.vpk"
```

Artifacts are not usable here: GitHub meters artifact storage for private
repositories, the account quota is currently full, and an upload attempt fails
the step with "Artifact storage quota has been hit". That is why the workflow
publishes the VPK as a release asset too (release assets do not count against
the artifact quota) and why the artifact upload is allowed to fail without
failing the build.

There is no local VitaSDK, so every build comes from CI. The contract tests in
`tests/` run locally with `python -m unittest discover -s tests`.

## Status

| Item | State |
| --- | --- |
| 60 fps frame loop, interpolated view, cheaper upscaler | implemented on `v1.2-dev`, **needs a test on hardware** |
| Interpolation of things (monsters, projectiles, items) and of the weapon | implemented, **needs a test on hardware** |
| Framerate option (SELECT → OPTIONS, saved in `settings.cfg`) | implemented |
| Cheats menu | not started |
| Arena mode | not started |
| Resolution experiment | not started |
| Co-op prototype | not started |

## Requested features and verdicts

| Feature | Verdict | Why |
| --- | --- | --- |
| Cheats menu | **Feasible, low risk** | the engine already has the full classic cheat set, it only needs an input path |
| 60 fps | **Feasible, biggest engine job** | rendering is tic-paced at ~35 fps and the upscaler is software, both must change |
| Higher resolution | **Feasible, compile-time only** | `SCREENWIDTH`/`SCREENHEIGHT` are compile-time; needs a performance measurement on hardware |
| CapUnlocker | **Marginal** | it is not a clock unlocker (see below) |
| Co-op multiplayer | **Risky, prototype only** | the whole networking layer is missing from this tree |
| Arena mode, single player | **Feasible** | new game mode on the installed maps, no new data files |
| Arena co-op | **Depends on co-op** | only after the network prototype holds |
| Split-screen co-op on one Vita | **Not practical** | two views and two inputs on a 960x544 screen, and it doubles the render cost |

## 1. Cheats — feasible, low risk

The engine is a Chocolate Doom derivative and still contains the whole cheat
machinery:

- `doomgeneric/doomgeneric/m_cheat.c` — `cht_CheckCheat()`, matches typed ASCII
  characters against a sequence string.
- `doomgeneric/doomgeneric/st_stuff.c:388-408` — the classic sequences:
  `iddqd` (god), `idkfa` / `idfa` (weapons), `idspispopd` / `idclip` (no clip),
  `idbehold*` (powerups), `idchoppers`, `idclev` (level warp), `idmus`,
  `idmypos`, and `iddt` (full automap) in `am_map.c:267`.
- Every cheat is read in `ST_Responder` from `ev_keydown` events whose
  `data2` is the ASCII character, so the Vita port can trigger them by posting
  synthetic key events — no keyboard required.

Constraints found in the code, to respect in the design:

- `st_stuff.c:465` guards the whole block with
  `if (!netgame && gameskill != sk_nightmare)` — cheats are off in network games
  and on Nightmare difficulty. That is vanilla behaviour, keep it.
- `idclev` (level warp) and the automap cheat (`!deathmatch`) have their own
  guards.
- A cheat changes the player state that saves contain, so a cheat menu should be
  explicit about it.

Plan: a `CHEATS` entry in the launcher plus an in-game cheat screen (toggle list:
god mode, no clip, all weapons, keys, level warp, automap), implemented by
posting the corresponding key sequences into the engine, plus a config flag to
remember the choice. Everything stays inside the vanilla rules above.

## 2. 60 fps — feasible, and it is the headline job

Current state:

- `doomgeneric/doomgeneric/d_main.c:409` `doomgeneric_Tick()` runs
  `TryRunTics()` and then `D_Display()`, so a frame is drawn per game tic.
- Tic pacing comes from `d_loop.c` (`new_sync` buffering), i.e. the sim runs at
  35 tics/s and the renderer follows it.
- `doomgeneric_vita.c:1758` `I_FinishUpdate()` does a **software** nearest
  neighbour blit of 320x200 → 960x544: 522,240 palette lookups and writes per
  frame, with a per-pixel branch inside the inner loop.
- Presentation is vsync-locked (`sceDisplaySetFrameBuf` +
  `sceDisplayWaitVblankStart()`, 60 Hz panel), so 60 is the ceiling.

Work in three stages, each one testable on its own:

1. **Cheaper frame.** Integer scaling (3x horizontally, 2.72x vertically becomes
   a clean 3x + letterbox or a proper 4:3 fit), a palette→RGBA lookup table
   instead of a per-pixel branch, and row-wise writes. This alone buys a lot of
   headroom and is low risk.
2. **Decouple display from tics.** Draw on every vblank (60 Hz) while the
   simulation keeps running at 35 tics/s. This removes the 35 Hz frame pacing
   but does not make motion smoother by itself.
3. **Interpolation.** Store the previous tic state for the camera (and then for
   mobile objects) and draw the interpolated position. This is what "60 fps
   smooth" actually means in modern Doom ports (DSDA-Doom, Doom Retro do exactly
   this) and what the player will feel.

Things and the weapon are interpolated through a `prev_x/prev_y/prev_z` triple
on `mobj_t` and a `prev_sx/prev_sy` pair on `pspdef_t`: the values are swapped
with the live ones before the frame is drawn and swapped back right after, so
the simulation only ever sees whole tics. The new fields are deliberately kept
out of `saveg_*`, which means old savegames keep loading. Sector heights (doors,
lifts, moving floors) are still drawn in 35 Hz steps — a possible next step.

Ship it as an option (`Framerate: 35 / 60`) with the 35 fps path kept as a
fallback, so a slow scene or a card without the option can fall back safely.

Implemented on `v1.2-dev` as the three stages above: each source row is expanded
once and copied into the screen rows that map onto it, the port owns the frame
loop (`game_loop_smooth`), and the camera is interpolated between the previous
and the current tic (`view_apply` / `view_restore`) with the loop drawing once
per vsync. The framerate can be switched between 60 and 35 in the options
screen. What is left is the measurement on a real console: if the software
renderer cannot keep a frame inside the 16.6 ms vsync window in a busy scene,
the 35 fps option is the fallback and the resolution stage gets postponed.

Honest note for the release notes: game logic stays 35 Hz because that is the
Doom fixed timestep; interpolation is what makes the image move smoothly.

## 3. Higher resolution — feasible, requires a measurement

- `doomgeneric_vita.c:44-45` defines `SCREENWIDTH 320` / `SCREENHEIGHT 200`;
  the CMake `DOOMGENERIC_RESX/RESY` values must stay in sync with it.
- There is no `MAX_SCREENWIDTH`/`MAX_SCREENHEIGHT` cap in this tree; the renderer
  sizes its tables from the constants (`xtoviewangle` in `r_main.c:98`, the
  column tables in `R_Init`), so a resolution change is a compile-time change.
- Cost scales with pixels drawn. Candidates:
  - 320x200 — today (64,000 pixels)
  - 400x240 — +50%
  - 480x272 — 2.04x, exactly half of the 960x544 panel (crisp 2x upscale)
  - 640x400 — 4x, likely too heavy for a 444 MHz ARM
- Because it is compile-time, it cannot be a menu switch. Plan: after stage 1 of
  the 60 fps work, build a 480x272 test VPK and measure. Ship the higher
  resolution only if the frame budget still holds; otherwise keep 320x200 for
  gameplay (the launcher UI already draws at the full 960x544).

## 4. CapUnlocker and overclocking — marginal for this project

[CapUnlocker](https://github.com/GrapheneCt/CapUnlocker) is a **kernel plugin**
that unlocks: mounting virtual drives such as `cache0:`, the **4th core** for
game applications, virtual-machine threadmgr functions, and all thread
attributes. It does **not** change clock speeds.

What it could give us: the 4th CPU core, but only inside a multi-threaded
renderer, and only for users who already installed a kernel plugin. Depending on
it in a public release is not an option. Verdict: not needed for v1.2; worth
revisiting only if the 60 fps work ends up needing a second render thread.

Clock speed is a different plugin (PSVshell / PSVshell Plus): ARM up to 500 MHz
instead of 444, GPU 222, bus 222, XBAR 166. `doomgeneric_vita.c:2195-2198`
already requests 444 / 222 / 222 / 166, i.e. the maximum the official API
allows, so a user-side overclock adds at most +13% CPU. It can be mentioned in
the README as an optional tip, never as a requirement.

## 5. Co-op multiplayer — the risky one

This tree has **no networking at all**:

- `d_loop.c` contains the Chocolate Doom net calls, but they are inside
  `#ifdef FEATURE_MULTIPLAYER`, which is never defined here.
- `d_net.c` and the `net_client_connected` stub in `dummy.c:27` are present, and
  the headers `net_client.h`, `net_server.h`, `net_loop.h`, `net_io.h`,
  `net_packet.h`, `net_query.h`, `net_defs.h`, `net_structrw.h`… are still in the
  tree, but the matching sources (`net_client.c`, `net_server.c`, `net_loop.c`,
  `net_io.c`, `net_packet.c`, `net_structrw.c`, `net_query.c`, `i_net.c`,
  `net_sdl.c`) are **absent**.

To ship LAN co-op we would have to:

1. Bring back the Chocolate Doom network layer (client, server, loop, io, packet
   serialisation) and compile it with `FEATURE_MULTIPLAYER`.
2. Write a UDP backend for it with SceNet (the SDL_net backend does not exist on
   the Vita) — host and client on the same local network.
3. Add a host/join UI, with "join by IP" typed through the Vita on-screen
   keyboard dialog.
4. Synchronise settings and WAD checksums between both consoles, and debug
   desyncs — the part that eats the time, and it needs two Vitas on the desk.

Estimate: the prototype alone is a multi-week job with a real chance of not
being stable in time. Recommendation: keep it as an experimental branch, plan it
for v1.3 if it is not ready, and do not promise it in v1.2.

Interaction to remember: cheats and network games exclude each other in vanilla
(`!netgame` guard), so a co-op release would need a separate decision about
cheats.

## 6. Arena mode — feasible in single player

Design: one more entry in the launcher next to the three games (`ARENA`), which
starts one of the installed maps in arena mode:

- waves of monsters spawned by the engine itself (`P_SpawnMobj` with the mobj
  types from `info.c`), increasing count and mix per wave;
- level exit disabled so the player cannot accidentally end the run — the run
  ends when the player dies, with a wave/score summary;
- HUD block with wave number, monsters left and score, drawn through the
  existing `draw_game_overlays()` path in the Vita port;
- pickups and a short breather between waves;
- spawn spots validated with the engine's own position checks.

Maps: the Chex Quest 1/2 levels already installed (E1M1…E1M5) can serve as
arenas, so **no extra data files** are needed and nothing has to be
redistributed.

Risks: wave composition must respect the installed game (Chex 1 has the smallest
monster set), and the balance needs play testing. There is no existing arena code
to reuse; it is new gameplay code, but it sits on top of the engine's own
functions rather than fighting them.

Arena co-op is only possible after the network work above.

## 7. Split-screen co-op on one Vita — not practical

Two viewpoints at once means rendering the world twice per frame (the renderer is
software, so the cost doubles), a second input mapping, and a duplicated HUD
inside 960x544. Each half would be 480x272 — the same budget we may not even
reach at full screen for 60 fps. No.

## 60 fps: what is interpolated, and the crash that was fixed

The 60 fps mode draws once per vsync and interpolates between the last two game
tics. Everything that moves is interpolated: the camera, every thing (monsters,
projectiles, items), the weapon sway, and the sector floor/ceiling heights - so
doors, lifts and moving floors slide instead of stepping 35 times per second.

The first build that interpolated the things crashed the console as soon as a
game ran at 60 fps. The cause was the walk over the engine's thinker list: it
assumed the list was built, but `thinkercap` is a zeroed global and
`P_InitThinkers()` is called only from `P_SetupLevel` and from the savegame
unarchive (`P_Init()` does not call it). On the title screen - before the first
level - `thinkercap.next` is NULL, so `th->function.acp1` read address zero.
The port now starts the walk from `thing_list()`, which returns nothing unless a
level is up (`gamestate == GS_LEVEL`) and the head is a real pointer. The sector
heights go through the `sectors[]` array, which needs no list walk at all.

Three more guards keep the interpolation harmless:

- values that did not change are not exchanged at all (most things and nearly
  all sectors stand still, so this also saves the work);
- a jump larger than one tic of movement - a teleport, a spawn, a rebuilt level
  - is never interpolated, so a stale value can only ever be drawn a tic away
  from the truth;
- after a wipe or a hiccup `TryRunTics()` catches up several tics at once; the
  frames that follow are drawn from the state those tics produced, with no
  interpolation in between.

## Proposed v1.2 scope

1. Cheats menu (launcher + in-game) — small, self-contained.
2. 60 fps option and a cheaper upscaler — the headline feature.
3. Arena mode, single player — new gameplay content.
4. Resolution bump — ship it only if it holds the frame budget.
5. Co-op — prototype branch, released only if it is stable, otherwise v1.3.

## Test plan

- Every push builds a VPK in CI; the VPK is tested on real hardware.
- `python -m unittest discover -s tests` must stay green (it checks the data
  file contract, the launcher artwork, the save paths and the README).
- Before a public release: VPK validated as a zip, installed with VitaShell, the
  three games started, launcher, data screen and return-to-launcher checked.
