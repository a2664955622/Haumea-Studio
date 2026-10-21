"""Texture Studio maps number their textures by Texture Studio's alltextures.pwn.

The editor uses the workspace's own copy of that list when the server has
Texture Studio installed, and its bundled copy otherwise. ui/fixtures/ui_tstudio.db
gives a crate texture 3 (2notherbuildsfe ferry_build14 in the bundled list);
a workspace list where entry 3 is matcolours red must turn it red instead.

Needs a built editor, a game folder (HAUMEA_GAME_DIR; run_all.sh sets one) and
a display, like the other tests that open the editor.
"""
from pathlib import Path
import glob
import os
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from haumea_test_env import editor_env  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
DB = HERE / 'ui' / 'fixtures' / 'ui_tstudio.db'
BUNDLED = HERE.parent / 'tstudio_textures.h'
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
HAVE_DISPLAY = bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))


def alltextures(rows):
    """An alltextures.pwn with Texture Studio's table layout."""
    lines = ['new ObjectTextures[][TEXTUREDEF] = {']
    lines += [f'\t{{ {model}, "{txd}", "{tex}" }},' for model, txd, tex in rows]
    return '\n'.join(lines + ['};', ''])


@unittest.skipUnless(GAME_DIR and BINARY and HAVE_DISPLAY,
                     'set HAUMEA_GAME_DIR and build the editor to run')
class TStudioListTests(unittest.TestCase):
    def open_db(self, workspace=None):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp, 'out.pwn')
            args = [BINARY, '--game', GAME_DIR]
            if workspace:
                args += ['--workspace', str(workspace)]
            result = subprocess.run(args + ['--pawn-roundtrip', str(DB), str(target)],
                                    capture_output=True, text=True, timeout=600, env=editor_env(tmp))
            self.assertTrue(target.exists(), result.stdout[-2000:] + result.stderr[-2000:])
            return target.read_text(errors='replace')

    def test_bundled_list(self):
        self.assertIn('"2notherbuildsfe", "ferry_build14"', self.open_db())

    def test_the_workspace_list_wins(self):
        rows = re.findall(r'\{ (-?\d+), "([^"]*)", "([^"]*)" \}', BUNDLED.read_text())
        self.assertGreater(len(rows), 1000)
        rows[3] = ('18646', 'matcolours', 'red')
        with tempfile.TemporaryDirectory() as ws:
            folder = Path(ws, 'filterscripts', 'tstudio')
            folder.mkdir(parents=True)
            (folder / 'alltextures.pwn').write_text(alltextures(rows))
            out = self.open_db(ws)
        self.assertIn('"matcolours", "red"', out)
        self.assertNotIn('ferry_build14', out)


if __name__ == '__main__':
    unittest.main()
