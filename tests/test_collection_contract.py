from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
PNG_PALETTE = 3  # the colour type of an indexed PNG, which PIL calls "P"


def png_header(path: Path) -> tuple[int, int, int]:
    """Width, height and colour type straight out of the PNG header.

    Read here instead of through Pillow: these tests run in the build
    container, which has Python and nothing else installed.
    """
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise AssertionError(f"{path} is not a PNG with a header")
    width, height, _depth, colour, _comp, _filter, _interlace = struct.unpack(
        ">IIBBBBB", data[16:29])
    return width, height, colour


class CollectionContractTests(unittest.TestCase):
    def test_vita_dpad_contract_is_shared(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        self.assertIn("if (up && !up_was", source)
        self.assertIn("G_SaveGame(0, \"VITA SAVE\")", source)
        self.assertIn("if (down && !down_was", source)
        self.assertIn("G_LoadGame(path)", source)
        self.assertIn("int l = (pad.buttons & SCE_CTRL_LEFT) != 0", source)
        self.assertIn("int r = (pad.buttons & SCE_CTRL_RIGHT) != 0", source)
        self.assertIn("pulse_weapon_digit();", source)
        self.assertNotIn("both_triggers", source)

    def test_launcher_requires_each_game_data_file(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        self.assertIn("CHEX.WAD", source)
        self.assertIn("CHEX2.WAD", source)
        self.assertIn("chex3v.wad", source)
        self.assertIn("chex3.deh", source)
        self.assertIn("-collection-game", source)

    def test_game_profiles_separate_save_paths(self):
        source = (ROOT / "doomgeneric/doomgeneric/m_config.c").read_text(encoding="utf-8")
        for game in ("cq1", "cq2", "cq3"):
            self.assertIn(f"saves/{game}/", source)
            self.assertIn(f"cfg/{game}/", source)

    def test_required_vita_artwork_is_indexed_png(self):
        expected = {
            "sce_sys/icon0.png": (128, 128),
            "sce_sys/livearea/contents/bg.png": (840, 500),
            "sce_sys/livearea/contents/startup.png": (280, 158),
        }
        for filename, size in expected.items():
            with self.subTest(filename=filename):
                width, height, colour = png_header(ROOT / filename)
                self.assertEqual((width, height), size)
                self.assertEqual(colour, PNG_PALETTE)

    def test_launcher_uses_supplied_background_and_game_logos(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        generated = (ROOT / "launcher_art.h").read_text(encoding="ascii")
        for marker in ("launcher_bg", "launcher_logo_cq1", "launcher_logo_cq2", "launcher_logo_cq3"):
            self.assertIn(marker, generated)
        self.assertIn("memcpy(I_VideoBuffer, launcher_bg", source)
        self.assertNotIn("selected ? 255 : logo[pos]", source)
        self.assertIn("I_VideoBuffer[(y + py) * SCREENWIDTH + x + px] = logo[pos]", source)
        self.assertIn("menu_music_rate_phase += 22050", source)
        self.assertIn("#define OUTPUT_RATE 48000", source)
        self.assertNotIn("editions[]", source)
        self.assertIn('sceIoOpen("app0:/menu_music.pcm", SCE_O_RDONLY, 0)', source)
        self.assertIn("menu_music_active = 0", source)
        self.assertTrue((ROOT / "assets/menu_music.pcm").is_file())
        self.assertIn('vpk_add_asset("${CMAKE_SOURCE_DIR}/assets/menu_music.pcm" "menu_music.pcm")', (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        for filename in ("assets/logo_chexquest1.png", "assets/logo_chexquest2.png", "assets/logo_chexquest3.png", "launcher.jfif"):
            self.assertTrue((ROOT / filename).is_file(), filename)

    def test_frame_loop_interpolates_the_view_at_sixty(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        # The game is drawn once per vsync, from an interpolated camera ...
        self.assertIn("static void game_loop_smooth(void)", source)
        self.assertIn("uint32_t frac = subtic_fraction();", source)
        self.assertIn("view_apply(frac);", source)
        self.assertIn("view_restore();", source)
        self.assertIn("D_Display();", source)
        # ... while the original tic paced loop stays available as an option.
        self.assertIn("static void game_loop_classic(void)", source)
        self.assertIn("FPS_CLASSIC 35", source)
        self.assertIn("FPS_SMOOTH 60", source)
        self.assertNotIn("while (1) doomgeneric_Tick();", source)

    def test_upscaler_expands_each_source_row_once(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        self.assertIn("static void blit_expand_row", source)
        self.assertIn("memcpy(dst + y * VITA_W, blit_line, sizeof(blit_line));", source)
        self.assertIn("int sy = blit_row_src[y];", source)
        # the per pixel palette branch and the fixed point walk are gone
        self.assertNotIn("if (launcher_frame) {", source)
        self.assertNotIn("sx_f += step_x;", source)

    def test_automatic_speed_guard_drops_to_thirty(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        # 30 frames per second: every other vertical blank is left empty, so
        # the heaviest maps have half the frames to draw.
        self.assertIn("#define FPS_PERFORMANCE 30", source)
        self.assertIn("fps_skip_next = 1 - fps_skip_next;", source)
        self.assertIn("sceDisplayWaitVblankStart();", source)
        # The guard watches the frames really presented: two seconds in a row
        # below the floor are a scene that does not fit, so the port steps down
        # and says so, without touching the chosen framerate.
        self.assertIn("#define AUTO_SPEED_FLOOR 55", source)
        self.assertIn("#define AUTO_SPEED_WINDOWS 2", source)
        self.assertIn("auto_speed_check(frames);", source)
        # A window that barely ran was a level load or a wipe, not a slow
        # scene: 30 frames would not have helped it, so it is not counted.
        self.assertIn("#define AUTO_SPEED_MIN_FRAMES 20", source)
        self.assertIn("if (frames < AUTO_SPEED_MIN_FRAMES) {", source)
        self.assertIn("auto_dropped = 1;", source)
        self.assertIn("fps_active = FPS_PERFORMANCE;", source)
        self.assertIn('show_toast("AUTO SPEED: 30 FPS");', source)
        self.assertIn("fps_target != FPS_SMOOTH", source)
        # The measurement no longer depends on the counter being shown.
        self.assertNotIn("if (!fps_counter_on) {", source)
        # A framerate picked by hand clears an automatic drop ...
        self.assertIn("static void fps_choose(int fps)", source)
        self.assertIn("auto_dropped = 0;", source)
        self.assertIn("fps_active = fps_target;", source)
        # ... and the saved file keeps the choice, not the drop.
        self.assertIn('"framerate=%d\\nauto=%d\\ncounter=%d\\n"', source)
        self.assertIn("fps_target, auto_speed, fps_counter_on,", source)
        self.assertNotIn("fps_active, auto_speed", source)

    def test_things_weapon_and_sectors_are_interpolated(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        self.assertIn("static void things_snapshot(void)", source)
        self.assertIn("static void things_swap(int interpolate, uint32_t frac)", source)
        self.assertIn("things_swap(1, frac);", source)
        self.assertIn("things_swap(0, 0);", source)
        # every thing carries the position of the previous tic ...
        mobj = (ROOT / "doomgeneric/doomgeneric/p_mobj.h").read_text(encoding="utf-8")
        for field in ("prev_x;", "prev_y;", "prev_z;"):
            self.assertIn(field, mobj)
        # ... the weapon sprite carries its offset ...
        pspr = (ROOT / "doomgeneric/doomgeneric/p_pspr.h").read_text(encoding="utf-8")
        self.assertIn("prev_sx;", pspr)
        self.assertIn("prev_sy;", pspr)
        # ... and so do the sectors, so doors, lifts and moving floors slide.
        rdefs = (ROOT / "doomgeneric/doomgeneric/r_defs.h").read_text(encoding="utf-8")
        sector = rdefs.split("} sector_t;")[0]
        self.assertIn("prev_floorheight;", sector)
        self.assertIn("prev_ceilingheight;", sector)
        setup = (ROOT / "doomgeneric/doomgeneric/p_setup.c").read_text(encoding="utf-8")
        self.assertIn("ss->prev_floorheight = ss->floorheight;", setup)
        # The sector layout is engine side, so the port calls into p_setup.c.
        self.assertIn("void P_InterpSnapshotSectors(void);", source)
        self.assertIn("void P_InterpSwapSectors(int interpolate, int frac);", source)
        self.assertIn("P_InterpSnapshotSectors();", source)
        self.assertIn("P_InterpSwapSectors(interpolate, (int)frac);", source)
        self.assertIn("sectors[i].prev_floorheight = sectors[i].floorheight;", setup)
        self.assertIn("FixedMul(sec->prev_ceilingheight - sec->ceilingheight, frac)", setup)
        self.assertIn(
            "void P_InterpSwapSectors (int interpolate, int frac);",
            (ROOT / "doomgeneric/doomgeneric/p_setup.h").read_text(encoding="utf-8"),
        )
        # r_defs.h cannot be included from the port: it pulls in i_video.h,
        # whose globals clash with the ones the port defines.
        self.assertNotIn('#include "r_defs.h"', source)
        # Spawns and loaded games start from a defined value instead of from
        # whatever the memory happened to contain.
        spawn = (ROOT / "doomgeneric/doomgeneric/p_mobj.c").read_text(encoding="utf-8")
        self.assertIn("mobj->prev_x = mobj->x;", spawn)
        saveg = (ROOT / "doomgeneric/doomgeneric/p_saveg.c").read_text(encoding="utf-8")
        self.assertIn("mobj->prev_x = mobj->x;", saveg)
        self.assertIn("str->psprites[i].prev_sx = str->psprites[i].sx;", saveg)
        # The savegame format itself must not change.
        self.assertNotIn("prev_x", saveg.split("saveg_write_mobj_t")[1].split("}")[0])
        self.assertNotIn("prev_floorheight", saveg)
        self.assertNotIn("prev_ceilingheight", saveg)

    def test_interpolation_never_walks_an_unbuilt_thinker_list(self):
        # The engine builds thinkercap only from P_SetupLevel and from the
        # savegame unarchive. On the title screen it is still the zeroed global
        # it starts as, so a blind walk dereferenced address zero and crashed
        # the console as soon as a game ran at 60 fps.
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        setup = (ROOT / "doomgeneric/doomgeneric/p_setup.c").read_text(encoding="utf-8")
        self.assertIn("static thinker_t *thing_list(void)", source)
        self.assertIn("if (th == NULL || th == &thinkercap)", source)
        self.assertIn("for (th = thing_list(); th != NULL && th != &thinkercap;", source)
        self.assertNotIn("for (th = thinkercap.next; th != &thinkercap", source)
        # Sector heights are guarded in the engine, where the sectors live.
        self.assertIn("static boolean vita_sectors_ready (void)", setup)
        self.assertIn("return gamestate == GS_LEVEL && numsectors > 0 && sectors != NULL;", setup)
        self.assertIn("#define VITA_INTERP_MAX_STEP", setup)
        # A jump longer than one tic of movement is a teleport or a spawn: it is
        # drawn where it really is instead of being smeared across the map.
        self.assertIn("#define INTERP_MAX_STEP", source)
        self.assertIn("static int interp_near(fixed_t a, fixed_t b)", source)
        # The invariant the guard relies on, straight from the engine.
        tick = (ROOT / "doomgeneric/doomgeneric/p_tick.c").read_text(encoding="utf-8")
        self.assertIn("thinkercap.prev = thinkercap.next  = &thinkercap;", tick)
        # P_Init() does not build the list, only the level setup does, which is
        # exactly why the walk must be guarded before the first level.
        setup = (ROOT / "doomgeneric/doomgeneric/p_setup.c").read_text(encoding="utf-8")
        self.assertIn("P_InitThinkers ();", setup)
        self.assertNotIn("P_InitThinkers", setup.split("void P_Init (void)")[1].split("\n}")[0])

    def test_framerate_choice_is_saved_and_documented(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        self.assertIn('VITA_GAME_DATA_DIR "settings.cfg"', source)
        self.assertIn('"framerate=%d\\nauto=%d\\ncounter=%d\\n"', source)
        self.assertIn('strstr(buf, "framerate=")', source)
        self.assertIn('strstr(buf, "counter=")', source)
        self.assertIn("settings_load();", source)
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("## Framerate", readme)
        self.assertIn("35 FPS (classic)", readme)
        self.assertIn("monsters", readme)
        self.assertIn("doors, lifts and moving floors", readme)
        self.assertIn("## Automatic speed guard", readme)

    def test_frames_go_out_double_buffered(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        # Two framebuffers exchanged at the vertical blank: the picture being
        # scanned out is never written to, which is what stopped the torn
        # scanlines that showed up while turning the view.
        self.assertIn("#define FB_COUNT 2", source)
        self.assertIn("static void *fb_buffers[FB_COUNT]", source)
        self.assertIn("fb_allocated = i + 1;", source)
        self.assertIn("if (fb_allocated > 1) {", source)
        self.assertIn("fb_draw_index ^= 1;", source)
        self.assertIn("fb_base = fb_buffers[fb_draw_index];", source)
        self.assertIn("if (fb_allocated == 0)\n    return;", source)
        # Both the startup path and ui_present() leave the displayed buffer
        # alone: the buffer drawn into is always the other one.
        self.assertEqual(source.count("fb_draw_index ^= 1;"), 2)
        self.assertEqual(source.count("fb_base = fb_buffers[fb_draw_index];"), 2)
        # ... and with a single buffer (allocation failed) nothing flips.
        self.assertEqual(source.count("if (fb_allocated > 1) {"), 2)
        # Every screen draws through fb_base, so they all follow the exchange:
        # the two native helpers and the game blit each read it per call.
        self.assertEqual(source.count("uint32_t *dst = (uint32_t *)fb_base;"), 2)
        self.assertIn("dst = (uint32_t *)fb_base;", source)

    def test_options_screen_selects_and_stores_the_settings(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        # A real menu: three boxed rows, UP/DOWN to choose, X or left/right to
        # change the value of the selected row.
        self.assertIn("#define OPT_ROWS 3", source)
        for label in ("FRAMERATE", "AUTO SPEED", "FRAME COUNTER"):
            self.assertIn(f'"{label}"', source)
        self.assertIn('"PERFORMANCE"', source)
        self.assertIn('strstr(buf, "auto=")', source)
        self.assertIn("static int options_selected = 0;", source)
        self.assertIn("options_selected = (options_selected + OPT_ROWS - 1) % OPT_ROWS;", source)
        self.assertIn("options_selected = (options_selected + 1) % OPT_ROWS;", source)
        # Two buttons in the same frame accumulate, so neither change is lost.
        self.assertIn("changed |= options_toggle(options_selected);", source)
        self.assertIn("changed |= options_set(options_selected, 1);", source)
        self.assertIn("changed |= options_set(options_selected, 0);", source)
        self.assertIn("changed = 0;", source)
        self.assertIn("static void options_draw_row(int row)", source)
        self.assertIn('"FRAME COUNTER"', source)
        self.assertIn('"X: CHANGE"', source)
        self.assertIn('"UP/DOWN: CHOOSE   X OR LEFT/RIGHT: CHANGE"', source)
        # The frame counter is a setting as well, and it is measured.
        self.assertIn("static void fps_counter_tick(void)", source)
        self.assertIn('"%d FPS  %d.%d MS"', source)
        self.assertIn("fps_counter_tick();", source)
        self.assertIn("counter=%d", source)

    def test_netgame_layer_is_wired_to_the_vita_transport(self):
        engine = ROOT / "doomgeneric/doomgeneric"
        features = (engine / "doomfeatures.h").read_text(encoding="utf-8")
        # The engine's netgame code is compiled in, and the transport it uses
        # is the console's own UDP sockets rather than the SDL one.
        self.assertIn("#define FEATURE_MULTIPLAYER 1", features)
        self.assertNotIn("#undef FEATURE_MULTIPLAYER", features)
        loop = (engine / "d_loop.c").read_text(encoding="utf-8")
        self.assertIn('#include "net_vita.h"', loop)
        self.assertIn("NET_SV_AddModule(&net_vita_module);", loop)
        self.assertIn("net_vita_module.InitClient();", loop)
        self.assertIn("net_vita_module.ResolveAddress(myargv[i+1]);", loop)
        self.assertNotIn("net_sdl_module", loop)
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("FEATURE_MULTIPLAYER=1", cmake)
        self.assertIn("SceNet_stub SceNetCtl_stub", cmake)
        # The transport is a net_module_t like the one it replaces, and the
        # master-server/GUI half of the query code is not dragged in: without
        # it there is no textscreen and no internet advertisement.
        transport = (engine / "net_vita.c").read_text(encoding="utf-8")
        for hook in ("NET_VITA_InitClient", "NET_VITA_InitServer",
                     "NET_VITA_SendPacket", "NET_VITA_RecvPacket",
                     "NET_VITA_AddrToString", "NET_VITA_FreeAddress",
                     "NET_VITA_ResolveAddress"):
            self.assertIn(hook, transport)
        self.assertIn("net_module_t net_vita_module =", transport)
        self.assertIn("SCE_NET_SO_NBIO", transport)
        self.assertTrue((engine / "net_vita_glue.c").is_file())
        self.assertFalse((engine / "net_query.c").exists())
        self.assertFalse((engine / "net_gui.c").exists())
        self.assertFalse((engine / "net_query_stub.c").exists())
        self.assertFalse((engine / "net_sdl.c").exists())
        # The vendored netgame layer is the version whose headers this tree
        # already carries (Chocolate Doom 2.3.0), not a newer one.
        for name in ("net_common.c", "net_structrw.c", "net_packet.c",
                     "net_io.c", "net_loop.c", "net_server.c", "net_client.c",
                     "net_common.h", "net_structrw.h"):
            self.assertTrue((engine / name).is_file(), name)
        # Joining is by address and hosting by -server: a plain start must
        # still be a plain single player game.
        self.assertIn('M_CheckParm("-server")', loop)
        self.assertIn('M_CheckParmWithArgs("-connect", 1)', loop)
        self.assertIn("if (addr != NULL)", loop)
        # dummy.c used to stand in for the network layer that this build did
        # not carry, so it defined two of its globals as well. Now that the
        # real client is compiled in, both definitions would collide at link
        # time unless the stub only fills the gap when the layer is absent.
        dummy = (engine / "dummy.c").read_text(encoding="utf-8")
        self.assertIn('#include "doomfeatures.h"', dummy)
        stubs = dummy.split("#ifndef FEATURE_MULTIPLAYER")[1].split("#endif")[0]
        self.assertIn("net_client_connected", stubs)
        self.assertIn("boolean drone = false;", stubs)
        self.assertIn("net_client_connected;", (engine / "net_client.c").read_text(encoding="utf-8"))

    def test_coop_screen_starts_a_netgame_from_the_launcher(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        engine = ROOT / "doomgeneric/doomgeneric"
        # A console has no keyboard, so the desktop waiting window's "press
        # space to start" cannot exist: the controller launches the game by
        # itself once the consoles it was told to wait for are in.
        glue = (engine / "net_vita_glue.c").read_text(encoding="utf-8")
        self.assertIn("static int ExpectedNodes(void)", glue)
        self.assertIn('M_CheckParmWithArgs("-nodes", 1)', glue)
        self.assertIn("NET_CL_LaunchGame();", glue)
        self.assertIn("net_client_wait_data.is_controller", glue)
        self.assertIn("connected >= wanted", glue)
        # ... and the port draws that wait, since the engine has no text mode.
        self.assertIn("extern void VITA_NetWaitScreen(", glue)
        self.assertIn("VITA_NetWaitScreen(connected, wanted,", glue)
        self.assertIn("void VITA_NetWaitScreen(int connected, int expected", source)
        self.assertIn("%d OF %d CONSOLES IN", source)
        # The launcher opens it with R, and it says so.
        self.assertIn('"SELECT: OPTIONS   R: CO-OP"', source)
        self.assertIn("if ((pad.buttons & SCE_CTRL_RTRIGGER) && !(previous.buttons & SCE_CTRL_RTRIGGER)) {", source)
        self.assertIn("show_coop_screen();", source)
        self.assertIn("if (coop_ready)", source)
        # Four rows: which game, host or join, how many consoles, and the
        # address to join.
        self.assertIn("#define COOP_ROWS 4", source)
        for label in ('"GAME"', '"MODE"', '"CONSOLES"', '"JOIN"'):
            self.assertIn(label, source)
        for value in ("HOST THIS GAME", "JOIN ANOTHER CONSOLE"):
            self.assertIn(value, source)
        # The address is typed with the pad, so the direction repeats while it
        # is held and the shoulders pick which part is being changed.
        self.assertIn("#define COOP_REPEAT_FRAMES 4", source)
        self.assertIn("coop_repeat = (coop_repeat + 1) % COOP_REPEAT_FRAMES;", source)
        self.assertIn("coop_part = (coop_part + 1) % COOP_ADDRESS_PARTS;", source)
        self.assertIn('"L/R: PART"', source)
        # This console's own address is the one the others have to type, so the
        # screen shows it before anything is started.
        self.assertIn("NET_VITA_GetLocalAddress(coop_local_address, sizeof(coop_local_address));", source)
        self.assertIn("boolean NET_VITA_EnsureStack(void)", (engine / "net_vita.c").read_text(encoding="utf-8"))
        self.assertIn("NO WI-FI ADDRESS", source)
        # A console that cannot play the chosen game is told which one, instead
        # of being sent into a fatal error by the engine.
        self.assertIn("if (launcher_game_ready(coop_game)) {", source)

    def test_the_wait_screen_gives_the_game_its_palette_back(self):
        # Found on two consoles: after the wait the game was presented through
        # the launcher's colour table, so the title screen came out black and
        # the menu a mess of wrong colours. The wait draws in that table and
        # has to hand the game back the game's own one.
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        self.assertIn("lut = launcher_frame ? launcher_lut : cmap;", source)
        wait = source.split("void VITA_NetWaitScreen(")[1].split("\n}\n")[0]
        self.assertIn("launcher_frame = 1;", wait)
        self.assertIn("I_FinishUpdate();", wait)
        self.assertIn("launcher_frame = 0;", wait.split("I_FinishUpdate();")[1])
        # The launcher itself clears it in the same way before a game starts.
        self.assertIn("menu_music_active = 0;\n    launcher_frame = 0;", source)

    def test_a_netgame_installs_the_game_palette(self):
        # Found on two consoles: in co-op the level, the menu and the status bar
        # all came out in greys. A netgame skips the title screen and goes
        # straight into a level, and the engine installs PLAYPAL only when the
        # screen changes away from a level, so it never reaches that line: the
        # port has to hand the game its palette itself, once the game is up.
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        create = source.index("doomgeneric_Create(nargc, nargv);")
        start = source.index("if (netgame) {", create)
        installed = source.index(
            'I_SetPalette(W_CacheLumpName(DEH_String("PLAYPAL"), PU_CACHE));', start)
        self.assertIn("debug_log(\"Co-op: installed the game palette for the netgame\");",
                      source[start:installed + 200])
        # ... and the engine's own conditions are still the ones this works
        # around: if they ever change, this line is the one to revisit.
        engine = (ROOT / "doomgeneric/doomgeneric/d_main.c").read_text(encoding="utf-8")
        self.assertIn("if (gamestate != oldgamestate && gamestate != GS_LEVEL)", engine)
        self.assertIn("if (autostart || netgame)", engine)

    def test_the_coop_start_handshake_is_compiled_in(self):
        # Found on two consoles: co-op connected, the wait ended, the level came
        # up black and never ran a single tic. The handshake that starts a
        # netgame (the GAMESTART exchange in d_loop.c) was guarded with
        # "#if ORIGCODE", and this tree's config.h has ORIGCODE undefined -
        # doomgeneric turned it off - so the whole handshake was compiled out:
        # the server never entered its in-game state, so it never sent a tic,
        # and both consoles sat on a frozen level. The handshake belongs to
        # FEATURE_MULTIPLAYER, which this build does define.
        engine = ROOT / "doomgeneric/doomgeneric"
        # Normalised so the blocks can be split out by hand: these sources are
        # checked in with CRLF endings.
        loop = (engine / "d_loop.c").read_text(encoding="utf-8").replace("\r\n", "\n")
        self.assertIn("#undef ORIGCODE", (engine / "config.h").read_text(encoding="utf-8"))
        self.assertNotIn("#if ORIGCODE", loop)
        wait = loop.split("void D_StartGameLoop(void)")[1].split("void D_StartNetGame(")[0]
        self.assertIn("static void BlockUntilStart(net_gamesettings_t *settings,", wait)
        self.assertIn("if (!net_client_connected)", wait)
        guard = wait.find("#ifdef FEATURE_MULTIPLAYER")
        block = wait.find("static void BlockUntilStart(")
        closed = wait.find("#endif", block)
        self.assertTrue(0 <= guard < block < closed,
                        "BlockUntilStart must live inside the FEATURE_MULTIPLAYER block")
        start = loop.split("void D_StartNetGame(net_gamesettings_t *settings,")[1]
        live = start.split("#ifdef FEATURE_MULTIPLAYER")[1].split("#else")[0]
        self.assertIn("NET_CL_StartGame(settings);", live)
        self.assertIn("BlockUntilStart(settings, callback);", live)
        self.assertIn("localplayer = settings->consoleplayer;", live)
        # A build with multiplayer switched off keeps its short path.
        self.assertIn("settings->num_players = 1;", start.split("#else")[1].split("#endif")[0])

    def test_a_coop_game_that_never_started_says_so(self):
        # Two ways a co-op session can end before it starts: the other console
        # leaves while everyone is waiting, or the two consoles disagree about
        # their game data. Neither may fall through into a game - the first
        # used to start a network game on the remaining console alone, with no
        # other player in it - and neither may close the application without a
        # word, which is what a plain I_Error does on this port.
        engine = ROOT / "doomgeneric/doomgeneric"
        glue = (engine / "net_vita_glue.c").read_text(encoding="utf-8").replace("\r\n", "\n")
        self.assertIn("extern void VITA_NetFailScreen(const char *line1, const char *line2);", glue)
        wait = glue.split("void NET_WaitForLaunch(void)")[1].split("\n}\n")[0]
        self.assertIn('VITA_NetFailScreen("LOST THE OTHER CONSOLE",', wait)
        self.assertIn('"THE GAME DID NOT START");', wait)
        # The data every console has to be running is compared before the game.
        self.assertIn("net_local_wad_sha1sum", wait)
        self.assertIn("net_client_wait_data.wad_sha1sum", wait)
        self.assertIn('VITA_NetFailScreen("DIFFERENT GAME DATA",', wait)
        self.assertIn('"SAME FILES ON BOTH");', wait)
        self.assertIn("if (!net_client_connected)", wait)
        # The digests that comparison reads are the ones the two consoles send.
        net = (engine / "d_net.c").read_text(encoding="utf-8").replace("\r\n", "\n")
        self.assertIn("W_Checksum(connect_data->wad_sha1sum);", net)
        self.assertIn("memset(connect_data->deh_sha1sum, 0", net)
        self.assertNotIn("#if ORIGCODE", net)
        # The port draws the failure and hands the launcher back, and a co-op
        # that fails while starting goes through the same screen.
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8").replace("\r\n", "\n")
        fail = source.split("void VITA_NetFailScreen(const char *line1, const char *line2)")[1].split("\n}\n")[0]
        self.assertIn("return_to_launcher();", fail)
        self.assertIn("SCE_CTRL_CROSS", fail)
        self.assertIn("if (coop_ready) {", source)
        error = source.split("void I_Error(const char *error, ...)")[1].split("\n}")[0]
        self.assertIn('VITA_NetFailScreen("CO-OP COULD NOT START"', error)

    def test_the_join_address_only_needs_its_last_part_typed(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        # A home router hands out addresses that share the first three parts,
        # so what this console already has fills those in and the other console
        # is typed two digits at a time.
        self.assertIn("static void coop_adopt_local_network(const char *local)", source)
        self.assertIn("coop_adopt_local_network(coop_local_address);", source)
        adopt = source.split("static void coop_adopt_local_network(const char *local)")[1].split("\n}\n")[0]
        # ... and an address that was typed and stored is never touched.
        self.assertIn("if (coop_host[0] != 192 || coop_host[1] != 168", adopt)
        self.assertIn('sscanf(local, "%u.%u.%u.%u", &a, &b, &c, &d) != 4', adopt)
        self.assertIn("coop_host[2] = (unsigned char)c;", adopt)
        # Choosing to join leaves the selection on the address row, which is
        # the next thing to do.
        mode = source.split("case COOP_ROW_MODE:")[1].split("break;")[0]
        self.assertIn("coop_selected = COOP_ROW_JOIN;", mode)
        self.assertIn("coop_selected = coop_mode == COOP_JOINING ? COOP_ROW_JOIN : COOP_ROW_GAME;", source)

    def test_coop_arguments_carry_the_role_the_game_and_the_port(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        # The game is chosen the same way for one console and for several, so
        # the two paths cannot drift apart.
        self.assertIn("static int game_arguments(int game, char *out[], int max)", source)
        self.assertIn("static int coop_arguments(char *out[], int max)", source)
        run = source.split("int game = coop_ready ? coop_game : selected;")[1]
        self.assertIn("nargc = game_arguments(game, nargv, GAME_ARGV_MAX);", run)
        self.assertIn("coop_arguments(nargv + nargc, GAME_ARGV_MAX - nargc)", run)
        # The engine keeps the pointers for the whole game, so the strings it
        # is handed cannot live on a stack frame that returns.
        arguments = source.split("static int game_arguments")[1].split("static int coop_arguments")[0]
        self.assertIn("static char patch_path[]", arguments)
        self.assertNotIn("  char patch_path[]", arguments)
        # Hosting waits for the consoles it was told about; joining connects to
        # the typed address. Both sides open the same port.
        coop = source.split("static int coop_arguments")[1].split("/* MAIN */")[0]
        self.assertIn('#define COOP_PORT 2342', source)
        self.assertIn('"-server"', coop)
        self.assertIn('"-connect"', coop)
        self.assertIn('"-nodes"', coop)
        self.assertIn('"-port"', coop)
        self.assertIn('snprintf(port_value, sizeof(port_value), "%d", COOP_PORT);', coop)
        self.assertIn("nodes_value[0] = (char)('0' + coop_consoles);", coop)
        self.assertIn("if (coop_mode == COOP_HOSTING) {", coop)
        self.assertIn("coop_address_plain(host_value, sizeof(host_value));", coop)

    def test_coop_settings_are_remembered_between_sessions(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        saved = source.split("static void settings_save(void) {")[1].split("\n}")[0]
        loaded = source.split("static void settings_load(void) {")[1].split("\n}")[0]
        # An address is typed once: the role, the game, how many consoles and
        # the address itself all go into the settings file and come back.
        for key in ("coop=%s", "coopgame=%d", "coopnodes=%d", "coophost=%d.%d.%d.%d"):
            self.assertIn(key, saved)
        for key in ('strstr(buf, "coop=")', 'strstr(buf, "coopgame=")',
                    'strstr(buf, "coopnodes=")', 'strstr(buf, "coophost=")'):
            self.assertIn(key, loaded)
        # A file from an older build, or one that was edited by hand, cannot
        # put a value in that the game cannot use.
        self.assertIn("if (game >= 0 && game <= 2)", loaded)
        self.assertIn("if (consoles >= COOP_MIN_CONSOLES && consoles <= COOP_MAX_CONSOLES)", loaded)
        self.assertIn('sscanf(value + 9, "%u.%u.%u.%u", &a, &b, &c, &d) == 4', loaded)
        self.assertIn("&& a < 256 && b < 256 && c < 256 && d < 256", loaded)
        self.assertIn("#define COOP_MIN_CONSOLES 2", source)
        self.assertIn("#define COOP_MAX_CONSOLES 4", source)

    def test_readme_options_screenshot_is_generated_from_the_port(self):
        # The README shows the OPTIONS screen, and that image is not a
        # hand-made mock-up: scripts/prepare_screenshots.py redraws it from the
        # port's own font and coordinates, and refuses to write a screen whose
        # texts would overlap.
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("docs/screenshots/options.png", readme)
        self.assertNotIn("docs/screenshots/controls.png", readme)
        self.assertTrue((ROOT / "docs/screenshots/options.png").is_file())
        # The replaced screen must not linger as a second, stale image.
        self.assertFalse((ROOT / "docs/screenshots/controls.png").exists())
        generator = (ROOT / "scripts/prepare_screenshots.py").read_text(encoding="utf-8")
        self.assertIn("menu_font", generator)
        self.assertIn("def check_layout", generator)
        self.assertIn("overlaps", generator)
        self.assertIn('"OPTIONS"', generator)
        self.assertIn('"FRAME COUNTER"', generator)
        self.assertIn("OPTION_ROWS", generator)
        # Every selector row of those screens is checked for overlap, and the
        # co-op screen is rendered the same way from the port's own layout.
        self.assertIn("in OPTION_ROWS + COOP_HOSTING_ROWS + COOP_JOINING_ROWS:", generator)
        self.assertIn("def coop_screen(", generator)
        self.assertIn("COOP_HOSTING_ROWS", generator)
        self.assertIn("COOP_JOINING_ROWS", generator)
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        for name in ("coop-host.png", "coop-join.png"):
            self.assertIn(f"docs/screenshots/{name}", readme)
            self.assertTrue((ROOT / "docs/screenshots" / name).is_file(), name)

    def test_readme_is_english(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("## PS Vita controls", readme)
        self.assertIn("## Installation and game data", readme)
        self.assertNotIn("Comandi PS Vita", readme)
        self.assertNotIn("Le frecce del D-pad", readme)

    def test_wad_and_deh_assets_are_not_staged(self):
        for path in ROOT.rglob("*"):
            if ".git" in path.parts or "build" in path.parts:
                continue
            self.assertNotIn(path.suffix.lower(), {".wad", ".deh", ".ipk3"})

    def test_a_coop_player_without_a_start_still_gets_a_body(self):
        # Found on two consoles: Chex Quest 2 co-op started the level and its
        # music, then crashed with a black screen on both. The rushed Chex
        # Quest 2 maps carry only player 1's start - Chex Quest 1's maps carry
        # four cooperative starts, Chex Quest 2's carry none - so with a second
        # console player 2 was put in the game with no body at all: players[1]
        # .mo stayed NULL and P_PlayerThink dereferenced it on the first tic.
        setup = (ROOT / "doomgeneric/doomgeneric/p_setup.c").read_text(encoding="utf-8")
        load = setup.split("P_LoadThings (lumpnum+ML_THINGS);")[1].split("// if deathmatch")[0]
        self.assertIn("if (!playeringame[i] || players[i].mo != NULL)", load)
        self.assertIn("if (!deathmatch)", load)
        self.assertIn("spawnhere = playerstarts[i];", load)
        # A body of its own, not a copy of the first start: the second player
        # used to stand inside the first one, invisible, in the way of every
        # step and in front of every shot. A rushed single-player map has no
        # cooperative starts left, but it does mark player-sized holes for
        # deathmatch.
        self.assertIn("if (spawnhere.type == 0 && i < dmstarts)", load)
        self.assertIn("spawnhere = deathmatchstarts[i];", load)
        self.assertIn("spawnhere = playerstarts[0];", load)
        self.assertIn("spawnhere.type = i + 1;", load)
        # ... recorded where the respawn code reads it, so a player the map
        # gave no start can be respawned instead of left without a body.
        self.assertIn("playerstarts[i] = spawnhere;", load)
        self.assertIn("P_SpawnPlayer(&spawnhere);", load)
        # ... and the engine is untouched otherwise: the body the missing
        # start would have left NULL is what the player think dereferences
        # before it does anything else.
        user = (ROOT / "doomgeneric/doomgeneric/p_user.c").read_text(encoding="utf-8")
        self.assertIn("player->mo->flags", user.split("void P_PlayerThink")[1])
        # p_setup.c spawns the player without including p_mobj.h, so the
        # prototype has to be here, next to the one it already had.
        self.assertIn("void P_SpawnPlayer (mapthing_t* mthing);", setup)
        # The other half of the same problem: G_DoReborn takes the corpse away
        # and then respawns at playerstarts[], so both the body and the start
        # have to be there. A map without them left a NULL dereference and a
        # zeroed start (x, y, angle, type) in the respawn path.
        game = (ROOT / "doomgeneric/doomgeneric/g_game.c").read_text(encoding="utf-8")
        reborn = game.split("void G_DoReborn (int playernum)")[1].split("void G_ScreenShot")[0]
        self.assertIn("if (players[playernum].mo != NULL)", reborn)
        check = game.split("G_CheckSpot\n(")[1].split("G_DeathMatchSpawnPlayer")[0]
        self.assertIn("players[i].mo != NULL", check)


if __name__ == "__main__":
    unittest.main()
