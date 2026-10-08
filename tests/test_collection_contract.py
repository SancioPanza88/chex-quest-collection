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
        self.assertIn("view_apply(subtic_fraction());", source)
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

    def test_framerate_choice_is_saved_and_documented(self):
        source = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
        self.assertIn('VITA_GAME_DATA_DIR "settings.cfg"', source)
        self.assertIn('"framerate=%d\\n"', source)
        self.assertIn("settings_load();", source)
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("## Framerate", readme)
        self.assertIn("35 FPS (classic)", readme)

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
