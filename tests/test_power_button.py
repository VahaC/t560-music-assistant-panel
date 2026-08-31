import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "t560-power-button.py"
SPEC = importlib.util.spec_from_file_location("power_button", SCRIPT)
POWER_BUTTON = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(POWER_BUTTON)


class ScreenOffSecondsTest(unittest.TestCase):
    def read(self, contents):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.ini"
            path.write_text(contents, encoding="utf-8")
            return POWER_BUTTON.screen_off_seconds(str(path))

    def test_reads_the_configured_timeout(self):
        self.assertEqual(self.read("[panel]\nscreen_off_seconds=45\n"), 45)

    def test_zero_disables_automatic_screen_off(self):
        self.assertEqual(self.read("[panel]\nscreen_off_seconds=0\n"), 0)
        self.assertEqual(self.read("[panel]\nscreen_off_seconds=-10\n"), 0)

    def test_clamps_out_of_range_values(self):
        self.assertEqual(
            self.read("[panel]\nscreen_off_seconds=1\n"),
            POWER_BUTTON.MIN_SCREEN_OFF_SECONDS,
        )
        self.assertEqual(
            self.read("[panel]\nscreen_off_seconds=99999\n"),
            POWER_BUTTON.MAX_SCREEN_OFF_SECONDS,
        )

    def test_falls_back_to_the_default(self):
        self.assertEqual(
            self.read("[panel]\npoll_interval_ms=1000\n"),
            POWER_BUTTON.DEFAULT_SCREEN_OFF_SECONDS,
        )
        self.assertEqual(
            self.read("[panel]\nscreen_off_seconds=soon\n"),
            POWER_BUTTON.DEFAULT_SCREEN_OFF_SECONDS,
        )
        self.assertEqual(
            POWER_BUTTON.screen_off_seconds("/nonexistent/config.ini"),
            POWER_BUTTON.DEFAULT_SCREEN_OFF_SECONDS,
        )


if __name__ == "__main__":
    unittest.main()
