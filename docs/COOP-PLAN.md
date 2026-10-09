# Co-op multiplayer: what it takes

Playing the whole game in co-op with two or more PS Vitas is possible with this
engine, but it is the largest change the project has seen so far. This is the
plan, in the order it has to be built, with the checks that can be run without
holding a console in hand.

## What is already in the tree

The game engine is a Chocolate Doom descendant, and its multiplayer logic is
present and untouched:

- `doomgeneric/doomgeneric/d_loop.c` — the netgame loop: tic command exchange,
  `D_InitNetGame`, `TryRunTics`, late-join and the launch handshake.
- `doomgeneric/doomgeneric/d_net.c` — `D_CheckNetGame`, `netcmds[]`, the
  per-player tic command buffer, quit handling.
- `doomgeneric/doomgeneric/doomstat.h` — `netgame`, `playeringame[]`,
  `consoleplayer`, `displayplayer`: the game already knows how to run with up
  to four players.
- `w_checksum.c`/`sha1.c` — the WAD and DEH digests the launch handshake uses
  to refuse to start a game two players do not agree on.

What is missing is everything that moves the tic commands between consoles, and
it is switched off on purpose: `doomfeatures.h` has `#undef FEATURE_MULTIPLAYER`,
so `D_InitNetGame` is compiled out and nothing in the port has to provide a
transport.

## What has to be added

1. **The Chocolate Doom net stack** (~4.5k lines, GPLv2 like the rest of the
   engine), none of which is in the tree today:
   `net_common.c`, `net_io.c`, `net_packet.c`, `net_loop.c`, `net_server.c`,
   `net_client.c`, `net_structrw.c`, plus `i_net.h` and the matching headers
   that are already present (`net_defs.h`, `net_io.h`, `net_packet.h`,
   `net_loop.h`, `net_server.h`, `net_client.h`, …).
2. **A Vita transport** replacing `net_sdl.c`: `net_vita.c` implements the same
   `net_module_t` interface with `SceNet` UDP sockets
   (`sceNetInit`, `sceNetSocket`, `sceNetSendto`/`sceNetRecvfrom`, non
   blocking, `SCE_NET_CTL_IP` for the local address), so hosting and joining
   works over the Wi-Fi both consoles are already connected to. Console to
   console `SceNetAdhoc` is a second module behind the same interface and can
   come later.
3. **Turning `FEATURE_MULTIPLAYER` on** and giving `D_InitNetGame` the two
   modules it expects, in `d_loop.c` and `CMakeLists.txt`.
4. **A launcher screen for it**: a fourth row per game — *PLAY ALONE*,
   *HOST CO-OP*, *JOIN CO-OP* — with the player count for the host and the
   host address for the joiner. The Vita has no keyboard, so the address is
   picked from a short list of addresses seen on the local network (the
   transport's own announcement packet) instead of being typed.
5. **The details the game gets wrong outside single player**:
   - quick save and quick load must be refused during a netgame (the port calls
     `G_SaveGame`/`G_LoadGame` directly today; in a netgame that desyncs);
   - the launcher's "return to launcher" reload has to leave the netgame
     cleanly instead of dropping the other players;
   - the co-op preset: lockstep runs at the speed of the slowest console, so a
     netgame starts at 30 fps with the automatic speed guard on, as already
     implemented for single player.

## How it can be verified without two consoles

Compiling is not evidence that a handshake works, and testing lockstep needs
two machines, so the plan is to make the protocol testable where CI can reach
it:

- **Host side unit tests**: the packet and transport layer is written against a
  small socket interface, so CI (ubuntu, gcc) can build it with a fake socket
  and drive two endpoints against each other — connection, WAD checksum
  exchange, launch, tic command delivery, a dropped packet, a late joiner and a
  player leaving. That covers exactly the logic that is otherwise only
  reachable with hardware.
- **Workerd/VitaSDK build**: the existing CI job keeps proving the console
  build links and produces a VPK.
- **Two consoles, finally**: only at that point can the remaining questions be
  answered — does the Wi-Fi hold, is the input delay acceptable, does a
  three-player game stay in sync.

## Risks, stated plainly

- The net stack is 4.5k lines of code from another project: it will not compile
  first try, and the CI round trip is about a minute and a half per attempt.
- Lockstep on a 60 Hz display with a 35 Hz simulation means the input delay is
  at least one tic (about 28 ms) plus the network round trip. On a home Wi-Fi
  that is playable but not free; the 30 fps mode is the safer default.
- Without two consoles this plan cannot be called finished, only built and
  made plausible.
