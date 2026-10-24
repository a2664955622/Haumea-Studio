# SPDX-License-Identifier: GPL-2.0-or-later
"""The showcase map: one of every element, built through the agent.

tests/showcase/showcase.pwn is a Haumea-saved streamer map (Pershing Square)
with a removed world building, retextured objects and material text, a fence
laid along a line, a mirrored structure, tuned vehicles, a pickup, a label, a
gang zone, a map icon, a game sound, an actor with an animation, NPCs on a
path, on the path nodes and playing a .rec, a checkpoint race, a textdraw and
a custom model. The custom model's files are cut from the game's own gta3.img
at test time, so no game files are kept in the repository; the recording
(TRAIN_LINDEN_TO_UNITY) ships with the open.mp server.

The legacy encoding/race tags migrate into XML on save. The runtime code must
stay identical and its second save must be stable. The result must
compile with 0 errors and 0 warnings against open.mp's includes. Needs a built
editor, a GTA San Andreas directory and a display: set HAUMEA_GAME_DIR (and
HAUMEA_BINARY if it is not in bin/). The compile step needs the Pawn toolchain
(run_all.sh fetches it into PAWN_TOOLCHAIN).
"""
from pathlib import Path
import glob
import os
import re
import xml.etree.ElementTree as ET
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from haumea_test_env import editor_env, run_editor  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SHOWCASE = HERE / 'showcase'
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
HAVE_DISPLAY = bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))
# The custom model (-2100 in artconfig.txt): a barrel from gta3.img.
MODEL_FILES = {'barrel4.dff': 'showbarrel.dff', 'dynbarrels.txd': 'showbarrel.txd'}


def img_entries(path):
    with open(path, 'rb') as f:
        if f.read(4) != b'VER2':
            return {}
        count, = struct.unpack('<I', f.read(4))
        entries = {}
        for _ in range(count):
            offset, size, size2, name = struct.unpack('<IHH24s', f.read(32))
            entries[name.split(b'\0')[0].decode().lower()] = (offset * 2048, (size or size2) * 2048)
        return entries


def make_server(tmp):
    """A workspace server with the showcase's custom model, or None."""
    img = Path(GAME_DIR, 'models', 'gta3.img')
    entries = img_entries(img) if img.is_file() else {}
    if not all(name in entries for name in MODEL_FILES):
        return None
    server = Path(tmp, 'server')
    (server / 'models').mkdir(parents=True)
    (server / 'scriptfiles').mkdir()
    with open(img, 'rb') as f:
        for source, target in MODEL_FILES.items():
            offset, size = entries[source]
            f.seek(offset)
            (server / 'models' / target).write_bytes(f.read(size))
    shutil.copy(SHOWCASE / 'artconfig.txt', server / 'models' / 'artconfig.txt')
    return server


@unittest.skipUnless(GAME_DIR and BINARY and HAVE_DISPLAY,
                     'set HAUMEA_GAME_DIR and build the editor to run')
class ShowcaseTests(unittest.TestCase):
    def roundtrip(self, tmp, server, source):
        """Opens and saves the showcase, then saves that save again, in one editor."""
        first, second = Path(tmp, 'first', 'showcase.pwn'), Path(tmp, 'second', 'showcase.pwn')	# the file name sets the prefix
        first.parent.mkdir()
        second.parent.mkdir()
        result = run_editor([BINARY, '--game', GAME_DIR, '--workspace', str(server),
                             '--pawn-roundtrip', str(source), str(first), str(first), str(second)],
                            cwd=tmp, env=editor_env(tmp))
        self.assertEqual(result.stdout.count('0 skipped on save'), 2, result.stdout[-2000:])
        self.assertIn('skipped 0 from', result.stdout, result.stdout[-2000:])
        return first, second

    def test_showcase_round_trip_and_compile(self):
        with tempfile.TemporaryDirectory() as tmp:
            server = make_server(tmp)
            if server is None:
                self.skipTest('the game folder has no gta3.img with barrel4 (synthetic fixture)')
            first, second = self.roundtrip(tmp, server, SHOWCASE / 'showcase.pwn')
            expected = (SHOWCASE / 'showcase.pwn').read_text()
            # Only these editor tags leave the legacy showcase's Pawn.
            expected = re.sub(r'^// Text encoding:.*\n', '', expected, flags=re.M)
            expected = re.sub(r' @race\("[^"\n]*"\)', '', expected)
            xml = ET.parse(str(first) + '.xml')
            self.assertEqual(xml.getroot().get('encoding'), 'Windows-1252')
            self.assertEqual(sum(n.get('cpRace') == 'plazarace' for n in xml.findall('./scene/object/properties')), 3)
            self.assertEqual(ET.tostring(xml.getroot().find('scene')),
                             ET.tostring(ET.parse(str(second)+'.xml').getroot().find('scene')))
            self.assertEqual(first.read_text(), expected,
                             'opening and saving the showcase changed it')
            self.assertEqual(second.read_text(), expected, 'saving it again changed it')

            toolchain = os.environ.get('PAWN_TOOLCHAIN') or str(HERE / 'pawn' / '.toolchain')
            if not Path(toolchain, 'bin', 'pawncc').exists():
                self.skipTest('no Pawn toolchain (run_all.sh fetches it)')
            result = subprocess.run([str(HERE / 'pawn' / 'check_pawn.sh'), str(first.parent)],
                                    env={**os.environ, 'PAWN_TOOLCHAIN': toolchain},
                                    capture_output=True, text=True, timeout=600)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('ok   showcase (openmp)', result.stdout)


if __name__ == '__main__':
    unittest.main()
