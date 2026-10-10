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
| 30 fps rate and automatic speed guard | implemented, **needs a test on hardware** |
| Picture modes (sharp / smoothed rows / boxed 2x) | **tried and removed**: they add no real detail and look worse than the plain fill |
| Cheats menu | not started |
| Arena mode | not started |
| Resolution experiment (renderer at 640x400) | not started — see the note below |
| Co-op over the LAN (host/join screen, Vita UDP transport, netgame layer) | implemented on `v1.2-coop`; two consoles found the start handshake compiled out (black level), then Chex Quest 2 crashing on its missing cooperative starts and then only its host crashing on the body that first fix stacked on player 1's start, all fixed — **needs a re-test on hardware** |

## Requested features and verdicts

| Feature | Verdict | Why |
| --- | --- | --- |
| Cheats menu | **Feasible, low risk** | the engine already has the full classic cheat set, it only needs an input path |
| 60 fps | **Feasible, biggest engine job** | rendering is tic-paced at ~35 fps and the upscaler is software, both must change |
| Higher resolution | **Feasible, compile-time only** | `SCREENWIDTH`/`SCREENHEIGHT` are compile-time; needs a performance measurement on hardware |
| CapUnlocker | **Marginal** | it is not a clock unlocker (see below) |
| Co-op multiplayer | **Built, unverified on hardware** | the layer and the transport are in; two Vitas still have to prove it |
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

## 5. Co-op multiplayer — built, still to be proved on two consoles

The tree had **no networking at all** when this was scoped: `d_loop.c` kept the
Chocolate Doom net calls behind `FEATURE_MULTIPLAYER`, and the sources they call
(`net_client.c`, `net_server.c`, `net_loop.c`, `net_io.c`, `net_packet.c`,
`net_structrw.c`, `net_query.c`, `net_sdl.c`) were absent. What is on
`v1.2-coop` now:

1. The Chocolate Doom 2.3.0 netgame layer is vendored back — the version whose
   headers this tree already carried — and compiled with `FEATURE_MULTIPLAYER`.
2. `net_vita.c` is the UDP backend it talks to instead of `net_sdl.c`: SceNet
   sockets, the same `net_module_t` interface, no SDL_net and no textscreen.
3. `net_vita_glue.c` fills the three files the layer still calls and this tree
   does not carry: the master-server search answers "nothing found" (a console
   joins by address and advertises nothing), the dedicated server is refused,
   and the waiting loop launches the game **by itself** once `-nodes` consoles
   are connected, because a console has no key to press.
4. The launcher has a co-op screen (**R**): game, host or join, how many
   consoles, and the address to join, typed with the pad and remembered in
   `settings.cfg`. It draws the wait screen while the others connect.
5. WAD and DeHacked checksums are compared by the server, which refuses a
   console whose data does not match — the desync there is the one that would
   be hardest to debug later.

The first two tests on hardware are recorded in `docs/COOP-PLAN.md` ("First test"
and "Second test"): the sockets bind and resolve, two consoles connect, the host
launches by itself, and the game data is checked — but the level came up black
and never ran a tic. The netgame **start handshake** in `d_loop.c` was wrapped in
`#if ORIGCODE`, and this tree's `config.h` has `ORIGCODE` undefined, so it was
compiled out and the server never entered its in-game state. That is fixed: the
handshake now belongs to `FEATURE_MULTIPLAYER`, a console whose wait ends because
the other console left says so instead of starting a game alone, mismatched game
data is refused before the game, and a co-op start that fails gives the launcher
back instead of closing the application.

What is left is the part that needs hardware again:

- whether the lockstep now holds — the port's frame loop drives `TryRunTics()`
  from its own 35 Hz wall clock, and with the default old sync the engine's clock
  is the same wall clock, but `-newsync` would not be;
- whether an actual level plays, both players moving, a level change, a
  disconnect;
- input delay on the 60 fps path (one tic plus the round trip);
- how long a console waits after the other one leaves (the connection timeout is
  30 s).

Interaction to remember: cheats and network games exclude each other in vanilla
(`!netgame` guard), so a co-op release would need a separate decision about
cheats. The engine also carries no input for a second player on the same
console; co-op means one console per player.

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

## Presentation: no more torn scanlines

The port used a single framebuffer: the CPU wrote the next frame into the very
buffer the display was scanning out, so turning the view produced jagged
horizontal tears (the write and the scanout raced, which is also why they came
and went). There are two framebuffers now, exchanged at the vertical blank in
`ui_present()`, and every screen draws through the `fb_base` pointer, so the
buffer on the display is never touched. If the second allocation ever fails the
port keeps working with one buffer, exactly as before.

The options screen is a menu instead of a line of text: three boxed rows
(FRAMERATE, AUTO SPEED, FRAME COUNTER), UP/DOWN to choose, X or left/right to
change, and the selected row is highlighted with a gold border and a value box.
The frame counter is measured once per second from the frames the port really
presented, and it is stored in `settings.cfg` next to the framerate and the
guard switch.

The 30 fps rate and the guard make the 60 fps mode honest: a frame that misses
its vertical blank is paced to the next one, so a scene that does not fit turns
into an uneven 30 anyway. The guard measures the presented frames and, after
two whole seconds below 55 fps, drops to a clean 30 with a toast on screen,
leaving the saved choice alone. A second in which the loop barely ran — a level
load, a wipe — is not counted. A netgame will start with it on, since lockstep
runs at the speed of the slowest console.

### The picture modes, and why they were removed

Three ways of pushing the 320x200 picture onto the 960x544 panel were built and
then dropped: nearest fill (the original), a vertical smoothing of the rows and
a centred 2x box. They do not add any detail — the renderer still draws 320x200
— so the best they can do is trade sharpness for smoothness. The plain fill is
what the game was designed for.

### Why a real resolution bump is a renderer refactor

`SCREENWIDTH`/`SCREENHEIGHT` are compile-time constants (320x200) and the
renderer strides by them everywhere. Raising them compiles, but every 2D
drawing coordinate in the engine is in 320x200 space, so the status bar, the
menus, the intermission, the finale and the automap all land in the wrong place
until the 2D layer is scaled. Doing it properly means a maximum-size array and
a runtime screen width plus a scaling choke point in `v_video.c`: a real
refactor, to be done with the CI compiler in the loop, not blind.

## Proposed v1.2 scope

1. Cheats menu (launcher + in-game) — small, self-contained.
2. 60 fps option and a cheaper upscaler — the headline feature.
3. Arena mode, single player — new gameplay content.
4. Resolution bump — a renderer refactor, not a constant change (see above).
   The picture modes were an attempt to fake it and did not survive review.
5. Co-op — built on `v1.2-coop`; released only if two consoles play a level
   together, otherwise it stays an experimental branch and v1.3 picks it up.

## Test plan

- Every push builds a VPK in CI; the VPK is tested on real hardware.
- Co-op needs a second pass before it can be called done: two consoles on the
  same router, host and join, one level played to the exit, a level change, and
  a console leaving mid-level. Until that happens, co-op is *built*, not
  *working*.
- `python -m unittest discover -s tests` must stay green (it checks the data
  file contract, the launcher artwork, the save paths and the README).
- Before a public release: VPK validated as a zip, installed with VitaShell, the
  three games started, launcher, data screen and return-to-launcher checked.
