"""Open a mixed Pawn map in the real editor, save it twice, and check the result.

Needs a built editor, a GTA San Andreas directory with SA-MP installed and a
display: set HAUMEA_GAME_DIR (and HAUMEA_BINARY if it is not in bin/).
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
from haumea_test_env import editor_env  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = Path(__file__).resolve().parent / 'fixtures' / 'pawn_roundtrip.pwn'
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
HAVE_DISPLAY = bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))


@unittest.skipUnless(GAME_DIR and BINARY and HAVE_DISPLAY,
                     'set HAUMEA_GAME_DIR and build the editor to run')
class PawnRoundtripTests(unittest.TestCase):
    def roundtrip(self, source, target):
        # Haumea runs from anywhere: the game folder comes from --game.
        result = subprocess.run([BINARY, '--game', GAME_DIR, '--pawn-roundtrip', str(source), str(target)],
                                cwd=target.parent, env=editor_env(target.parent),
                                capture_output=True, text=True, timeout=600)
        self.assertIn('pawn-roundtrip:', result.stdout, result.stdout[-2000:])
        return target.read_text()

    def test_open_save_is_faithful_and_stable(self):
        with tempfile.TemporaryDirectory() as tmp:
            first = self.roundtrip(FIXTURE, Path(tmp, 'map.pwn'))
            copy = Path(tmp, 'copy.pwn')
            copy.write_text(first)
            second = self.roundtrip(copy, Path(tmp, 'map.pwn'))
            self.assertEqual(first, second, 'saving an opened map must not change it')

        # RemoveBuildingForPlayer removes exactly the listed model (no LOD cascade).
        self.assertEqual(len(re.findall(r'RemoveBuildingForPlayer\(playerid, 3563,', first)), 1)
        self.assertNotIn('RemoveBuildingForPlayer(playerid, 3555,', first)
        # Texture Studio's reused tmpobjid: each object keeps its own material.
        self.assertRegex(first, r'1600\.000000[^\n]*\n[^\n]*"white", 0xFFFF0000')
        self.assertRegex(first, r'1604\.000000[^\n]*\n[^\n]*"white", 0xFF00FF00')
        # Variables as optional arguments keep the object (defaults are used).
        self.assertRegex(first, r'CreateDynamicObject\(3578, 1620\.000000[^\n]*-1, -1, -1, STREAMER_OBJECT_SD, 300\.00\)')
        self.assertIn('"a51", "concretewall22_256", 0x00000000', first)
        # CreateDynamicObjectEx stream/draw distances.
        self.assertRegex(first, r'1608\.000000[^\n]*, 150\.00, 120\.00\);')
        # Streamer world/interior/distances, names, groups and notes.
        self.assertIn('new sign = CreateDynamicObject(19353, 1612.000000, -1890.000000, 14.000000, '
                      '10.000000, 20.000000, 30.000000, 5, 3, -1, 200.00, 150.00); '
                      '// wall001 @group("Shop") @note("front sign")', first)
        # Streamer material text puts the index before the text.
        self.assertIn('SetDynamicObjectMaterialText(sign, 1, "OPEN"', first)
        # The attached crate sits on its parent and the attachment is kept.
        self.assertIn('new crate = CreateDynamicObject(1271, 1616.000000, -1890.000000, 14.300000, '
                      '15.000000, 25.000000, 80.000000', first)
        self.assertIn('AttachDynamicObjectToObject(crate, base, 0.000000, 0.000000, 0.800000, '
                      '15.000000, 25.000000, 35.000000, true);', first)
        # Vehicle paintjob, parts (one per slot, only fitting ones) and plate.
        self.assertRegex(first, r'new (\w+) = AddStaticVehicleEx\(562, 1600\.000000, -1900\.000000, '
                         r'13\.200000, 90\.000000, 140, 201, 300\);[^\n]*\n'
                         r'\tChangeVehiclePaintjob\(\1, 1\);\n'
                         r'\tAddVehicleComponent\(\1, 1036\);\n'
                         r'\tAddVehicleComponent\(\1, 1009\);\n'
                         r'\tAddVehicleComponent\(\1, 1080\);\n'
                         r'\tAddVehicleComponent\(\1, 1171\);\n'
                         r'\tSetVehicleNumberPlate\(\1, "NEU \\"FOX\\""\);\n')
        self.assertNotIn('1010)', first)
        self.assertNotIn('1100)', first)
        # Sound places: the stream goes to _ShowForPlayer, the game sound to this
        # map's own _AddSound helper with a streamer sphere behind it.
        self.assertIn('PlayAudioStreamForPlayer(playerid, "http://example.com/radio.pls", 1630.000000, '
                      '-1890.000000, 13.500000, 60.00, true); // sound', first)
        self.assertRegex(first, r'\tHaumea_\w+_AddSound\(1142, 1634\.000000, -1890\.000000, 13\.500000, '
                         r'40\.00, false, 3, 1\); // sound @note\("crowd"\)')
        self.assertIn('CreateDynamicSphere(x, y, z, radius, worldid, interiorid);', first)
        self.assertNotIn('Haumea_old_', first)
        # A hand-written name nothing else uses keeps its name and is marked
        # used, so pawncc does not warn (204).
        self.assertRegex(first, r'\tnew lamp = CreateDynamicObject\(1215, [^\n]*\n\t#pragma unused lamp\n')
        self.assertRegex(first, r'\tnew bike = AddStaticVehicleEx\(468, [^\n]*\n\t#pragma unused bike\n')
        self.assertEqual(first.count('#pragma unused'), 2)
        # Checkpoints (streamer only): a race is chained by its next points,
        # written in order and tagged with its name.
        self.assertIn('CreateDynamicCP(1644.000000, -1890.000000, 13.500000, 3.00, -1, -1, -1, '
                      'STREAMER_CP_SD); // checkpoint', first)
        self.assertRegex(first, r'CreateDynamicRaceCP\(0, 1650\.000000, -1890\.000000, 13\.500000, '
                         r'1660\.000000, -1890\.000000, 13\.500000, 8\.00, [^\n]*@race\("race1"\)\n'
                         r'\tCreateDynamicRaceCP\(1, 1660\.000000, -1890\.000000, 13\.500000, '
                         r'0\.000000, 0\.000000, 0\.000000, 8\.00, [^\n]*@race\("race1"\)\n')


if __name__ == '__main__':
    unittest.main()
