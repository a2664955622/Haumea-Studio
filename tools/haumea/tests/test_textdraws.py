# SPDX-License-Identifier: GPL-2.0-or-later
"""Textdraws in the real editor: an old map's are saved back with it, and a
textdraw file of their own is written and compiles beside that map.

Needs a built editor, a GTA San Andreas directory with SA-MP installed and a
display: set HAUMEA_GAME_DIR (and HAUMEA_BINARY if it is not in bin/).
With PAWNCC (the compiler) and OMP_INCLUDE / SAMP037_INCLUDE (include
folders) set, the result is also compiled and must have no warnings.
"""
from pathlib import Path
import glob
import os
import re
import subprocess
import tempfile
import unittest

import sys

# The helper sits next to the tests: found as a script and with python -m unittest.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from haumea_test_env import editor_env, run_editor  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
HAVE_DISPLAY = bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))
PAWNCC = os.environ.get('PAWNCC')

MAP = r'''#include <a_samp>

new Text:Title;
new Text:Box;
new PlayerText:PlayerTD[MAX_PLAYERS][2];

stock Make()
{
	Title = TextDrawCreate(320.0, 10.0, "~r~Haumea ~w~Server~n~\"friend\"");
	TextDrawAlignment(Title, 2);
	TextDrawFont(Title, TEXT_DRAW_FONT_PRICEDOWN);
	TextDrawLetterSize(Title, 0.6, 2.0);
	TextDrawColor(Title, -1);
	TextDrawSetOutline(Title, 1);
	Box = TextDrawCreate(500.0, 100.0, "_");
	TextDrawUseBox(Box, 1);
	TextDrawBoxColor(Box, 0x00000088);
	TextDrawTextSize(Box, 630.0, 0.0);
	TextDrawLetterSize(Box, 0.0, 10.0);
}

public OnPlayerConnect(playerid)
{
	PlayerTD[playerid][0] = CreatePlayerTextDraw(playerid, 30.0, 300.0, "Money");
	PlayerTextDrawSetSelectable(playerid, PlayerTD[playerid][0], true);
	PlayerTD[playerid][1] = CreatePlayerTextDraw(playerid, 540.0, 300.0, "_");
	PlayerTextDrawFont(playerid, PlayerTD[playerid][1], 5);
	PlayerTextDrawTextSize(playerid, PlayerTD[playerid][1], 80.0, 80.0);
	PlayerTextDrawSetPreviewModel(playerid, PlayerTD[playerid][1], 411);
	PlayerTextDrawSetPreviewVehCol(playerid, PlayerTD[playerid][1], 3, 1);
	return 1;
}
'''

# The map with its old read-only textdraws and a textdraw file, together.
GAMEMODE_WITH_FILE = '''#include "map.pwn"
#include "hud.pwn"
main(){}
public OnGameModeInit()
{
	Haumea_map_CreateTextDraws();
	Haumea_hud_CreateTextDraws();
	return 1;
}
public OnPlayerConnect(playerid)
{
	Haumea_map_ShowTextDraws(playerid);
	Haumea_hud_ShowTextDraws(playerid);
	return 1;
}
'''

GAMEMODE = '''#include "map.pwn"
main(){}
public OnGameModeInit()
{
	Haumea_map_CreateTextDraws();
	return 1;
}
public OnPlayerConnect(playerid)
{
	Haumea_map_ShowTextDraws(playerid);
	return 1;
}
'''


@unittest.skipUnless(GAME_DIR and BINARY and HAVE_DISPLAY,
                     'set HAUMEA_GAME_DIR and build the editor to run')
class TextDrawTests(unittest.TestCase):
    def compile(self, tmp, gamemode):
        """0 errors and 0 warnings on every include set given."""
        if not PAWNCC:
            return
        Path(tmp, 'gm.pwn').write_text(gamemode)
        for variable in ('OMP_INCLUDE', 'SAMP037_INCLUDE'):
            include = os.environ.get(variable)
            if not include:
                continue
            compiled = subprocess.run([PAWNCC, 'gm.pwn', '-i' + include, '-ogm.amx', '-d0'],
                                      cwd=tmp, capture_output=True, text=True,
                                      env=dict(os.environ, LD_LIBRARY_PATH=str(Path(PAWNCC).parent)))
            messages = [line for line in (compiled.stdout + compiled.stderr).splitlines()
                        if 'warning' in line or 'error' in line]
            self.assertEqual(messages, [], variable)

    def test_textdraw_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp, 'in.pwn')
            source.write_text(MAP)
            # The file's own textdraws, named apart from the map's.
            hud_source = Path(tmp, 'hud_in.pwn')
            hud_source.write_text(MAP.replace('Title', 'HudTitle').replace('Box', 'HudBox')
                                  .replace('PlayerTD', 'HudTD'))
            env = editor_env(tmp)
            result = run_editor([BINARY, '--game', GAME_DIR, '--textdraw-roundtrip', str(hud_source),
                                     str(Path(tmp, 'hud.pwn'))],
                                    cwd=tmp, env=env, capture_output=True, text=True)
            self.assertIn(': 4 textdraw(s)', result.stdout, result.stdout[-2000:])
            hud = Path(tmp, 'hud.pwn').read_text()
            self.assertIn('stock Haumea_hud_CreateTextDraws()', hud)
            self.assertIn('stock Haumea_hud_ShowTextDraws(playerid)', hud)
            self.assertIn('HudTitle = TextDrawCreate(320.000, 10.000,', hud)
            self.assertNotIn('CreateObject', hud)
            # Opened again, the file gives the same file back.
            result = run_editor([BINARY, '--game', GAME_DIR, '--textdraw-roundtrip', str(Path(tmp, 'hud.pwn')),
                                     str(Path(tmp, 'hud2.pwn'))],
                                    cwd=tmp, env=env, capture_output=True, text=True)
            self.assertIn(': 4 textdraw(s)', result.stdout, result.stdout[-2000:])
            self.assertEqual(Path(tmp, 'hud2.pwn').read_text().replace('hud2', 'hud'), hud)

            result = run_editor([BINARY, '--game', GAME_DIR, '--pawn-roundtrip', str(source),
                                     str(Path(tmp, 'map.pwn'))],
                                    cwd=tmp, env=env, capture_output=True, text=True)
            self.assertIn('pawn-roundtrip:', result.stdout, result.stdout[-2000:])
            self.compile(tmp, GAMEMODE_WITH_FILE)

    def test_textdraws_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp, 'in.pwn')
            source.write_text(MAP)
            target = Path(tmp, 'map.pwn')
            result = run_editor([BINARY, '--game', GAME_DIR, '--pawn-roundtrip', str(source), str(target)],
                                    cwd=tmp, env=editor_env(tmp), capture_output=True, text=True)
            self.assertIn('pawn-roundtrip:', result.stdout, result.stdout[-2000:])
            out = target.read_text()

            self.assertIn('Haumea_map_CreateTextDraws() from OnGameModeInit', out)
            self.assertIn('new Text:Title;', out)
            self.assertIn('Title = TextDrawCreate(320.000, 10.000, "~r~Haumea ~w~Server~n~\\"friend\\"");', out)
            self.assertIn('TextDrawAlignment(Title, TEXT_DRAW_ALIGN:2);', out)
            self.assertIn('TextDrawFont(Title, TEXT_DRAW_FONT:3);', out)
            self.assertIn('TextDrawColour(Title, 0xFFFFFFFF);', out)
            self.assertIn('TextDrawUseBox(Box, true);', out)
            # The two per-player textdraws of one array stay apart.
            self.assertEqual(len(re.findall(r'\] = CreatePlayerTextDraw\(playerid', out)), 2)
            self.assertIn('SetSelectable(playerid', out)
            self.assertIn('PlayerTextDrawSetPreviewVehicleColours(playerid', out)
            self.assertIn('TextDrawShowForPlayer(playerid, Title);', out)

            self.compile(tmp, GAMEMODE)


if __name__ == '__main__':
    unittest.main()
