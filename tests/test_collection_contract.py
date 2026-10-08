from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


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
        from PIL import Image

        expected = {
            "sce_sys/icon0.png": (128, 128),
            "sce_sys/livearea/contents/bg.png": (840, 500),
            "sce_sys/livearea/contents/startup.png": (280, 158),
        }
        for filename, size in expected.items():
            with self.subTest(filename=filename):
                with Image.open(ROOT / filename) as image:
                    self.assertEqual(image.size, size)
                    self.assertEqual(image.mode, "P")

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
        self.assertIn('"framerate=%d\\n"', source)
        self.assertIn("settings_load();", source)
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("## Framerate", readme)
        self.assertIn("35 FPS (classic)", readme)
        self.assertIn("monsters", readme)
        self.assertIn("doors, lifts and moving floors", readme)

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


if __name__ == "__main__":
    unittest.main()
