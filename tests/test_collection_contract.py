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

    def test_wad_and_deh_assets_are_not_staged(self):
        for path in ROOT.rglob("*"):
            if ".git" in path.parts or "build" in path.parts:
                continue
            self.assertNotIn(path.suffix.lower(), {".wad", ".deh", ".ipk3"})


if __name__ == "__main__":
    unittest.main()
