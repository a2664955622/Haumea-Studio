# SPDX-License-Identifier: GPL-2.0-or-later
"""Haumea refuses to write into the game folder, however the path is spelled.

Saves a map to the game folder directly, through a symlink to it and through
"x/../": each must be refused and leave nothing behind. Needs a built editor
and a game folder (HAUMEA_GAME_DIR, the real one or run_all.sh's synthetic one).
"""
from pathlib import Path
import glob
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from haumea_test_env import editor_env  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
HAVE_DISPLAY = bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))
MAP = '#include <a_samp>\nstock W_CreateObjects()\n{\n\tCreateObject(1271, 1600.0, -1890.0, 13.5, 0.0, 0.0, 0.0);\n}\n'


@unittest.skipUnless(GAME_DIR and BINARY and HAVE_DISPLAY and os.name == 'posix',
                     'set HAUMEA_GAME_DIR and build the editor to run (POSIX symlinks)')
class GameDirWriteTests(unittest.TestCase):
    def test_saving_into_the_game_folder_is_refused(self):
        game = Path(GAME_DIR).resolve()
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp, 'map.pwn')
            source.write_text(MAP)
            link = Path(tmp, 'gamelink')
            link.symlink_to(game, target_is_directory=True)
            name = 'haumea_write_test_%d.pwn' % os.getpid()
            targets = {
                'direct': game / name,
                'symlink': link / name,
                'dotdot': Path(str(game.parent), '..', game.parent.name, game.name, name),
            }
            for how, target in targets.items():
                result = subprocess.run([BINARY, '--game', str(game), '--pawn-roundtrip',
                                         str(source), str(target)], cwd=tmp, env=editor_env(tmp),
                                        capture_output=True, text=True, timeout=600)
                written = (game / name).exists()
                if written:
                    (game / name).unlink()
                self.assertFalse(written, f'{how}: wrote {target} into the game folder')
                self.assertIn('Refusing to write', result.stdout, how)


if __name__ == '__main__':
    unittest.main()
