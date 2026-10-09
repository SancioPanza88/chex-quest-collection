# Co-op multiplayer: what it took, and what is left

Two to four PS Vitas on one Wi-Fi playing the same game. This started as a plan
(what the engine was missing, in the order it had to be added) and is now mostly
a record: the code is on `v1.2-coop` and builds in CI. What is left needs two
consoles on one router, and this file says exactly which questions that has to
answer.

## What was already in the tree

The engine is a Chocolate Doom descendant and its multiplayer *logic* was present
and untouched; only the code that moves tic commands between machines was
missing, and it was switched off on purpose (`#undef FEATURE_MULTIPLAYER`):

- `d_loop.c` — the netgame loop: tic command exchange, `D_InitNetGame`,
  `TryRunTics`, the launch handshake.
- `d_net.c` — `D_ConnectNetGame`, the per-player tic command buffer, quit
  handling.
- `doomstat.h` — `netgame`, `playergame[]`, `consoleplayer`: the game already
  knows how to run with up to four players.
- `w_checksum.c`/`sha1.c` — the WAD and DeHacked digests the handshake uses to
  refuse a game two consoles do not agree on.
- The headers of the network layer (`net_defs.h`, `net_io.h`, `net_packet.h`,
  `net_loop.h`, `net_server.h`, `net_client.h`, `net_structrw.h`) were all still
  there, which is what fixed the version to match: Chocolate Doom **2.3.0**.

## What was added

1. **The netgame layer**, the seven sources the headers describe, taken from
   Chocolate Doom 2.3.0 (GPLv2, like the rest of the engine):
   `net_common.c`, `net_io.c`, `net_packet.c`, `net_loop.c`, `net_server.c`,
   `net_client.c`, `net_structrw.c`. `FEATURE_MULTIPLAYER` is on in
   `doomfeatures.h` and in `CMakeLists.txt`.
2. **The Vita transport**, `net_vita.c`, replacing `net_sdl.c`: the same
   `net_module_t` interface over SceNet UDP sockets (`sceNetInit`,
   `sceNetSocket`, `sceNetSendto`/`sceNetRecvfrom`, non-blocking). Both roles
   share one socket: a client lets the kernel pick a port, a server binds 2342.
   The network stack is started on first use, so a single player game never
   touches it.
3. **The three files the layer still called and this tree does not carry**,
   `net_vita_glue.c`: the master-server search answers "nothing found" (a
   console joins by address, and a LAN game is not advertised to the internet),
   the dedicated server is refused, and the waiting loop is rewritten — see
   below.
4. **A launcher screen for it** (`R` on the launcher): game, **HOST THIS GAME**
   or **JOIN ANOTHER CONSOLE**, how many consoles, and the address to join. The
   role, the game, the console count and the address are stored in
   `settings.cfg`, so the address is typed once.
5. **The netgame arguments**, in `doomgeneric_vita.c`: hosting passes
   `-server -nodes N -port 2342`, joining passes
   `-connect <address> -port 2342`, and both pass the same `-iwad`/`-file`/
   `-collection-game` the single player path uses, so the two paths cannot
   drift apart. The host's launch is the consoles' `-nodes` count, which only
   the controller acts on.
6. **The refusal during a co-op game** for quick save and quick load. The port
   saves and loads straight from the pad rather than through the save menus,
   which are where the engine refuses both in a netgame (`m_menu.c`): saving
   mid-level while the other console plays the same level is a desync waiting to
   happen, so the toast now says `NO SAVES IN A CO-OP GAME`.

### The waiting screen, and why it is not the desktop one

Chocolate Doom's `net_gui.c` waits in a textscreen window with a **"press space
to start"** action for the controller. The Vita has no keyboard and no
textscreen, so the wait is two things:

- `net_vita_glue.c` launches by itself as soon as
  `num_players + num_drones >= expected_nodes` **and** the wait data says this
  console is the controller (the oldest connected client, which is the host,
  since it connects to itself first). `-nodes` is what the launcher passes.
- `VITA_NetWaitScreen()` in the port draws the count (`2 OF 4 CONSOLES IN`, one
  square per console), says whether this console hosts or has connected, and
  offers **hold L + R + SELECT for a second** to give up and reload the
  launcher.

### The decisions that differ from the first plan

- **The address is typed, not picked from a list.** The original plan proposed
  announcing addresses on the LAN and choosing from a short list of them. Typing
  is what the transport can already do (`ResolveAddress` on a dotted quad), it
  needs no new packet, and the shoulders plus the repeating left/right make it
  usable with a D-pad. The address is remembered, so it is typed once.
- **No co-op frame-rate preset.** The plan proposed forcing 30 fps in a netgame.
  Lockstep runs at the speed of the slowest console either way, and the automatic
  speed guard already drops 60 to 30 when the frames do not fit, so a hard preset
  would have taken the choice away for nothing.

## How it can be verified without two consoles

- **Contract tests** (`python -m unittest discover -s tests`): the co-op screen,
  the arguments it builds, the settings round trip, the waiting loop's launch
  condition and the transport's shape are all checked against the sources, along
  with the link-time trap that the stub file `dummy.c` must not define the two
  globals the real client now owns.
- **The screenshots** of the co-op screen are rendered by
  `scripts/prepare_screenshots.py` from the port's own font and coordinates, and
  the script refuses to write an image whose texts overlap: that is the only
  check on the layout that does not need a console.
- **CI** builds the whole thing into a VPK with VitaSDK, which is what proves
  the SceNet and netgame code compiles and links for the console.

## First test on two consoles: what it found

A PSTV hosting and a PS Vita 2000 joining, Chex Quest 3, build `43ed205`. What
it proved and what it broke:

- **The handshake works.** Both consoles got past the wait, the host launched by
  itself, and the game came up on both — one console hosting and one joined is no
  longer a theory. Pressing START reached the game's own menu, so the input and
  the tic loop were running.
- **The picture was wrong, and both causes were in the port, not in the netgame
  layer.** They are both about the palette, and the second test found the deeper
  one. The waiting screen draws with the launcher's own table and left
  `launcher_frame` set, so every frame of the game after it was translated
  through the launcher table instead of the game's. Black stayed black (the
  screen the player saw) and everything else became the RGB332 expansion of a
  palette index that means nothing to the game (the mess seen after START). One
  flag, cleared in `VITA_NetWaitScreen()` now, and a contract test that keeps it
  cleared. With that flag cleared the game came up **in greys**: a netgame skips
  the title screen and goes straight into a level (`if (autostart || netgame)`),
  while the engine installs PLAYPAL only when the screen changes *away* from a
  level (`D_Display`), so in co-op that line is never reached and the whole game
  ran on the port's placeholder ramp. The port installs the game's palette
  itself once the netgame is up; a contract test holds it there.
- **The message "you can't start a new game in a network game" is the engine
  behaving.** In co-op the level is already running - the host started it - so
  NEW GAME is refused by design; nobody has to press anything to enter the game.
- **Typing the address was the clunky part.** The first three parts of it are now
  taken from this console's own address before anything was ever typed, and
  choosing to join leaves the selection on the address row.

## Second test on two consoles: the black screen

A PSTV and a PS Vita 2000, build `03a29c4`, Chex Quest 3. The two consoles
connected, the wait ended and the game came up — on a **black screen**, on both.
The menu opened over it (START), **NEW GAME** answered *you can't start a new
game while you're in a network game*, and nothing else the pad did made any
difference. When the host left, the other console stayed on the same black
screen; when the joining console left, the host could play normally. That last
observation is the one that identified the fault.

**The cause was not in the netgame layer at all: the handshake that starts a
netgame had been compiled out.** `D_StartNetGame()` in `d_loop.c` — the function
that sends the client's `GAMESTART`, waits for the server's answer and reads the
real player numbers — was wrapped in `#if ORIGCODE`. Chocolate Doom defines
`ORIGCODE`, but this tree's `config.h` (doomgeneric's) has it **undefined**, so
the whole handshake fell into the `#else` branch. What that branch does is start
a one-player game without telling the server anything:

- the server never received the controller's `GAMESTART`, so it never entered
  its in-game state and never sent a single tic (`SERVER_IN_GAME` is what
  `NET_SV_PumpSendQueue` is reached from);
- with no tic data, `recvtic` stayed 0, `GetLowTic()` stayed 0 and `TryRunTics()`
  ran **no tics at all**: `D_Display()` skips `R_RenderPlayerView()` while
  `gametic == 0`, which is exactly a black level, while the menu is drawn on top
  of it by the engine's own menu code;
- `netgame` was still true, so the engine refused NEW GAME — correctly, but the
  refusal was the only thing the player could reach;
- when the joining console left, the server's client-disconnect path called
  `NET_SV_GameEnded()`, which disconnected the host's own loopback client too:
  the host's client stopped being connected, `GetLowTic()` stopped being limited
  by `recvtic`, and the tics the host had been holding back ran. That is why the
  host could play alone the moment the other console was gone.

The fix is one word wide: the two blocks now belong to
`FEATURE_MULTIPLAYER` — the feature they actually depend on — instead of
`ORIGCODE`, so a co-op build compiles them in. A contract test now fails if
`#if ORIGCODE` comes back into `d_loop.c`, and normalises the line endings to
find the block.

Three smaller faults were found around it and fixed in the same pass:

- **A console whose wait ended because the other console vanished started a
  game on its own.** `net_waiting_for_launch` goes false on a lost connection
  exactly as it does on a real launch, and the port's waiting loop fell straight
  through into the game. It now says so (`LOST THE OTHER CONSOLE`) and hands the
  launcher back.
- **The game-data check the README promised was not implemented.** The waiting
data carries the controller's WAD SHA1 — Chocolate Doom's `net_gui.c` compares
it in a textscreen window, which the Vita has no equivalent of. The port now
compares it itself and refuses to start with `DIFFERENT GAME DATA`. The DEH
half of that comparison cannot be done in this tree (`DEH_Checksum()` is one of
the functions doomgeneric left out), so `d_net.c` sends a zeroed DEH digest
deliberately instead of uninitialised stack.
- **A co-op game that failed while it was starting closed the application.**
`I_Error` on this port logs, waits two seconds and exits, which on a console
looks like a crash. When the session was started from the co-op screen it now
shows `CO-OP COULD NOT START` with a way back to the launcher instead.

## What is left, and it needs hardware

Build `03a29c4` proves the sockets, the handshake, the automatic launch and the
data checksums; the build after this fix is the first one that can actually run
a level, and the next test on hardware has to answer:

- **whether the two consoles stay in step inside the level.** This is the first
  thing to look at: nothing before this build ever exchanged a tic.
- **whether the lockstep holds at all.** The port's frame loop drives
  `TryRunTics()` from its own 35 Hz wall clock. With the engine's default old sync
  that is the same clock the engine uses, but `-newsync` (not passed by the
  launcher) would need the adjusted clock instead.
- input delay on the 60 fps path: lockstep is one tic (about 28 ms) plus the
  round trip, and 35 fps classic is the mode to compare against;
- what a console leaving mid-level does to the others. The server times a silent
  client out (`CONNECTION_TIMEOUT_LEN`, 30 s) and broadcasts that it
  disconnected, so the remaining players should play on, but the port's "return
  to launcher" reloads the app without running the engine's exit handlers, so
  the other consoles only learn about it from that timeout — and a console whose
  host left is stuck waiting for tics for those same 30 seconds before it
  carries on alone. A shorter timeout is the obvious knob if that feels long on
  hardware, but it has to leave room for a level load on both sides at once.
