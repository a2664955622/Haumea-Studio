# SPDX-License-Identifier: GPL-2.0-or-later
"""The zones' start-up population comes from the game's own main.scm.

Haumea reads the SET_ZONE_* commands of main.scm's INITIAL mission (nothing
of Rockstar's is compiled in); --dump-zone-settings writes what it read. On
the synthetic game (fixture/make_game.py) the small main.scm sets three
things for GAN1; without main.scm every zone keeps the defaults. On the
real game (HAUMEA_GAME_DIR) INITIAL sets 476.

Needs a built editor and a display (run_all.sh starts its own Xvfb).
"""
from pathlib import Path
import glob
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from haumea_test_env import editor_env, is_real_game, run_editor  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
HAVE_DISPLAY = bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))

# SET_ZONE_POPULATION_TYPE 7 (gangland), SET_ZONE_POPULATION_RACE 1 (black),
# SET_ZONE_GANG_STRENGTH gang 1 (Grove) 40; the rest stays as it was.
# label popType raceMask noCops dealer gang0..gang9
DEFAULT_GAN1 = 'GAN1 5 15 0 0 0 0 0 0 0 0 0 0 0 0'
FIXTURE_GAN1 = 'GAN1 7 1 0 0 0 40 0 0 0 0 0 0 0 0'


@unittest.skipUnless(BINARY and HAVE_DISPLAY, 'build the editor to run')
class ZoneSettingsTests(unittest.TestCase):
    def dump(self, tmp, game):
        out = Path(tmp, 'zones.txt')
        result = run_editor([BINARY, '--game', game, '--dump-zone-settings', out],
                            cwd=tmp, env=editor_env(tmp))
        self.assertIn('dump-zone-settings: ' + str(out), result.stdout, result.stdout[-2000:])
        return out.read_text().splitlines()

    def synthetic_game(self, tmp):
        game = Path(tmp, 'game')
        subprocess.run([sys.executable, HERE / 'fixture' / 'make_game.py', game],
                       check=True, capture_output=True)
        return game

    def test_synthetic_main_scm(self):
        with tempfile.TemporaryDirectory() as tmp:
            lines = self.dump(tmp, self.synthetic_game(tmp))
            self.assertEqual(lines[0], 'settings 3 (main.scm INITIAL)')
            self.assertIn(FIXTURE_GAN1, lines)
            self.assertIn('MARKST 5 15 0 0 0 0 0 0 0 0 0 0 0 0', lines)

    def test_without_main_scm_zones_keep_the_defaults(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = self.synthetic_game(tmp)
            (game / 'data' / 'script' / 'main.scm').unlink()
            lines = self.dump(tmp, game)
            self.assertEqual(lines[0], 'settings none (no main.scm)')
            self.assertIn(DEFAULT_GAN1, lines)

    @unittest.skipUnless(is_real_game(GAME_DIR), 'needs the real game (HAUMEA_GAME_DIR)')
    def test_real_main_scm(self):
        with tempfile.TemporaryDirectory() as tmp:
            lines = self.dump(tmp, GAME_DIR)
            self.assertEqual(lines[0], 'settings 476 (main.scm INITIAL)')
            gan1 = next(line for line in lines if line.startswith('GAN1 '))
            self.assertEqual(gan1.split()[1], '7', gan1)	# Grove Street is gangland


if __name__ == '__main__':
    unittest.main()
