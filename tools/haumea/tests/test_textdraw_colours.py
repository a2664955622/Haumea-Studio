"""TextDraw colour tokens as the game draws them.

CFont::ParseToken (gta_sa.exe 0x718F00; gta-reversed Font.cpp): ~r~ ~g~ ~b~
~y~ ~p~ ~w~ ~s~ and lower-case ~l~ set a HUD colour (HudColours.cpp) keeping
the alpha; ~h~ multiplies the colour by 1.5 (at most 255); only the token's
first letter counts, up to the closing '~'. Each sets the tag; ~n~ doesn't.

CFont::ProcessCurrentString (0x71A220) starts every line, after ~n~ or a word
wrap, by putting sprintf("~%c~", tag) (format at 0x872930) in front of the
rest of the text, and every line is drawn from the textdraw's colour. So a
line starts in TextDrawColor plus the last colour token before it:
~y~a~n~b is yellow twice, ~w~~h~a~n~b's second line is TextDrawColor
highlighted (the user's finding), and ~r~~h~a~n~b's is too, not light red.

Needs a built editor and a game folder (HAUMEA_GAME_DIR, or the synthetic one
run_all.sh makes).
"""
from pathlib import Path
import glob
import os
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from haumea_test_env import editor_env, run_editor  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
HAVE_DISPLAY = bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))

BASE = '406080FF'	# TextDrawColor: a colour ~h~ visibly lightens, not grey or white
YELLOW, LIGHT_GREY, RED, BLACK = 'E2C063FF', 'E1E1E1FF', 'B4191DFF', '000000FF'
HIGHLIGHTED = '6090C0FF'	# BASE * 1.5

# text -> each line's colours, in order of appearance
CASES = {
    'test~n~test': [[BASE], [BASE]],
    '~y~test~n~test': [[YELLOW], [YELLOW]],
    '~w~~h~test~n~test': [['FFFFFFFF'], [HIGHLIGHTED]],
    '~h~~h~a~n~b': [['90D8FFFF'], [HIGHLIGHTED]],	# 0x40*2.25 = 0x90, 0x80*2.25 > 255
    '~r~~h~a~n~b': [['FF252BFF'], [HIGHLIGHTED]],
    'a~n~~w~b': [[BASE], [LIGHT_GREY]],
    '~y~a~n~b~n~c': [[YELLOW], [YELLOW], [YELLOW]],
    '~h~a~n~b~n~c': [[HIGHLIGHTED], [HIGHLIGHTED], [HIGHLIGHTED]],
    'a~r~b~w~c': [[BASE, RED, LIGHT_GREY]],
    '~s~a': [[LIGHT_GREY]],
    '~red~a': [[RED]],
    '~L~a~l~b': [[BASE, BLACK]],
    'a~N~b': [[BASE], [BASE]],
    '~k~~PED_FIREWEAPON~ a': [[BASE]],	# not a colour: no tag, no change
}


@unittest.skipUnless(GAME_DIR and BINARY and HAVE_DISPLAY,
                     'set HAUMEA_GAME_DIR and build the editor to run')
class TextDrawColourTokenTests(unittest.TestCase):
    def test_tokens(self):
        args = [BINARY, '--game', GAME_DIR]
        for text in CASES:
            args += ['--textdraw-colours', text, BASE]
        with tempfile.TemporaryDirectory() as tmp:
            result = run_editor(args, cwd=tmp, env=editor_env(tmp), capture_output=True, text=True)
        blocks = result.stdout.split('textdraw-colours:\n')[1:]
        self.assertEqual(len(blocks), len(CASES), result.stdout[-2000:])
        for (text, expected), block in zip(CASES.items(), blocks):
            lines = [l.split(':', 1)[1].split() for l in block.splitlines() if l.startswith('line ')]
            with self.subTest(text=text):
                self.assertEqual(lines, expected)


if __name__ == '__main__':
    unittest.main()
