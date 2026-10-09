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

## What is left, and it needs hardware

None of the following has been observed; it has only been compiled and checked by
contract tests. Until these are answered, co-op is *built*, not *working*:

- that `sceNetInit` and the sockets work with Wi-Fi on, and what happens when the
  console is offline (the co-op screen should read `NO WI-FI ADDRESS` rather than
  hang);
- that the handshake completes: connect, checksum exchange, launch;
- **that the lockstep holds.** The port's frame loop drives `TryRunTics()` from
  its own 35 Hz wall clock. With the engine's default old sync that is the same
  clock the engine uses, but `-newsync` (not passed by the launcher) would need
  the adjusted clock instead;
- that the automatic launch fires on a real network, and that a refused console
  (wrong game, or the wrong address) says so instead of hanging;
- input delay on the 60 fps path: lockstep is one tic (about 28 ms) plus the
  round trip, and 35 fps classic is the mode to compare against;
- what a console leaving mid-level does to the others. The server times a silent
  client out and broadcasts that it disconnected, so the remaining players should
  play on, but the port's "return to launcher" reloads the app without running
  the engine's exit handlers, so the other consoles only learn about it from that
  timeout.
