# SPDX-License-Identifier: GPL-2.0-or-later
"""Text reaches the game as the bytes the SA-MP client draws (Windows-1252).

pawn/maps/hv_text.pwn is a pawno-style Windows-1252 map with "Ação, Coração,
€ and ñ" in a label, material text, a global and a per-player textdraw and a
note. Opened and saved it must keep those bytes (not UTF-8) and say so in its
header, and compiled, the .amx must hold each string as Windows-1252 cells.
Needs a built editor and a game folder (HAUMEA_GAME_DIR); the compile step
needs the Pawn toolchain (PAWN_TOOLCHAIN, fetched by run_all.sh).
"""
from pathlib import Path
import glob
import os
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from haumea_test_env import editor_env, run_editor  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MAP = HERE / 'pawn' / 'maps' / 'hv_text.pwn'
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
HAVE_DISPLAY = bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))
TEXT = 'Ação, Coração, € and ñ'
PLAYER_TEXT = 'Olá ñandú'


@unittest.skipUnless(GAME_DIR and BINARY and HAVE_DISPLAY,
                     'set HAUMEA_GAME_DIR and build the editor to run')
class TextEncodingTests(unittest.TestCase):
    def test_windows_1252_text_reaches_the_amx(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp, 'hv_text.pwn')
            run_editor([BINARY, '--game', GAME_DIR, '--pawn-roundtrip', str(MAP), str(out)],
                           cwd=tmp, env=editor_env(tmp), capture_output=True)
            data = out.read_bytes()
            self.assertIn(b'// Text encoding: Windows-1252', data)
            # Three times: the label, the material text and the textdraw.
            self.assertEqual(data.count(TEXT.encode('cp1252')), 3)
            self.assertNotIn(TEXT.encode('utf-8'), data)
            self.assertIn(PLAYER_TEXT.encode('cp1252'), data)
            self.assertIn('@note("Coração da praça")'.encode('cp1252'), data)

            toolchain = Path(os.environ.get('PAWN_TOOLCHAIN') or HERE / 'pawn' / '.toolchain')
            pawncc = toolchain / 'bin' / 'pawncc'
            if not pawncc.exists():
                self.skipTest('no Pawn toolchain (run_all.sh fetches it)')
            gm = Path(tmp, 'gm.pwn')
            gm.write_text('#include <open.mp>\n#include "hv_text.pwn"\nmain() {}\n'
                          'public OnGameModeInit()\n{\n\tHaumea_hv_text_CreateObjects();\n'
                          '\tHaumea_hv_text_CreateTextDraws();\n\treturn 1;\n}\n'
                          'public OnPlayerConnect(playerid)\n{\n\tHaumea_hv_text_ShowTextDraws(playerid);\n'
                          '\treturn 1;\n}\n')
            amx = Path(tmp, 'gm.amx')
            result = subprocess.run([str(pawncc), str(gm), f'-i{toolchain / "omp" / "Server" / "qawno" / "include"}',
                                     f'-o{amx}', '-C-', '-;+', '-(+'], cwd=tmp, capture_output=True, text=True)
            self.assertTrue(amx.exists(), result.stdout + result.stderr)
            code = amx.read_bytes()
            cells = lambda s: b''.join(struct.pack('<I', b) for b in s.encode('cp1252'))
            self.assertEqual(code.count(cells(TEXT)), 3, 'the strings are not Windows-1252 in the .amx')
            self.assertEqual(code.count(cells(PLAYER_TEXT)), 1)


if __name__ == '__main__':
    unittest.main()
