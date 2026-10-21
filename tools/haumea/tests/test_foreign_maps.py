# SPDX-License-Identifier: GPL-2.0-or-later
"""Open an MTA:SA .map and an IPL text file in the real editor and check the Pawn.

Needs a built editor, a GTA San Andreas directory with SA-MP installed and a
display: set HAUMEA_GAME_DIR (and HAUMEA_BINARY if it is not in bin/).
The editor runs with a private XDG_CONFIG_HOME so the user's settings stay put.
"""
from pathlib import Path
import glob
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from haumea_test_env import editor_env  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
FIXTURES = Path(__file__).resolve().parent / 'fixtures'
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
HAVE_DISPLAY = bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))


@unittest.skipUnless(GAME_DIR and BINARY and HAVE_DISPLAY,
                     'set HAUMEA_GAME_DIR and build the editor to run')
class ForeignMapTests(unittest.TestCase):
    def roundtrip(self, name):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp, name)
            shutil.copy(FIXTURES / name, source)
            target = Path(tmp, 'out.pwn')
            env = editor_env(tmp)
            result = subprocess.run([BINARY, '--game', GAME_DIR, '--pawn-roundtrip', str(source), str(target)],
                                    cwd=GAME_DIR, capture_output=True, text=True, timeout=600, env=env)
            self.assertIn('0 skipped on save', result.stdout, result.stdout[-2000:])
            self.assertEqual(source.read_bytes(), (FIXTURES / name).read_bytes(), 'the source must not be written')
            return target.read_text(), result.stdout

    def test_mta_map(self):
        out, log = self.roundtrip('mta_sample.map')
        # MTA and SA-MP turn objects the same way: the angles carry over.
        self.assertIn('CreateObject(1634, 2490.000000, -1670.000000, 13.300000, 10.000000, 20.000000, 90.000000);', out)
        self.assertIn('CreateObject(1271, 2494.000000, -1666.000000, 13.800000, 0.000000, 0.000000, 45.000000);', out)
        # Vehicles: heading, palette or RGB colours, paintjob, parts and plate.
        self.assertRegex(out, r'new (\w+) = CreateVehicle\(560, 2488\.000000, -1680\.000000, 13\.100000, 90\.000000, 37, 1, -1\);')
        self.assertIn('ChangeVehiclePaintjob(', out)
        self.assertIn(', 1010);', out)
        self.assertIn('SetVehicleNumberPlate(', out)
        self.assertRegex(out, r'CreateVehicle\(522, [^\n]*180\.000000, \d+, \d+, -1\);')
        self.assertIn('CreateActor(280, 2486.000000, -1672.000000, 13.500000, -90.000000);', out)
        self.assertIn('CreatePickup(1240, 2,', out)
        self.assertIn('CreatePickup(1242, 2,', out)
        # removeWorldObject removes the model and its LOD.
        self.assertIn('RemoveBuildingForPlayer(playerid, 3563, 2063.132812, -1731.781250, 15.273438, 0.25);', out)
        self.assertIn('RemoveBuildingForPlayer(playerid, 3555, 2063.132812, -1731.781250, 15.273438, 0.25);', out)
        # What SA-MP can't show is reported.
        for dropped in ('object scale', 'object alpha', 'double-sided', 'marker', 'repair',
                        'vehicle tilt', '<spawnpoint>'):
            self.assertIn(dropped, log)

    def test_ipl_text(self):
        out, log = self.roundtrip('ipl_sample.ipl')
        self.assertIn('#include <streamer>', out)
        self.assertIn('CreateDynamicObject(14720, 216.554688, 1292.218750, 1081.117188, 0.000000, 0.000000, 0.000000, -1, 1, -1);', out)
        # The IPL's stored quaternion (z = -0.707) is a quarter turn.
        self.assertIn('CreateDynamicObject(2524, 219.671875, 1072.992188, 1083.164062, 0.000000, 0.000000, 90.000000, -1, 6, -1);', out)
        # Interior flags (257 = 1 | 256) are dropped; LOD instances are left out.
        self.assertIn('CreateDynamicObject(1502, 219.070312, 1287.031250, 1081.132812, 0.000000, 0.000000, 90.000000, -1, 1, -1);', out)
        self.assertEqual(out.count('CreateDynamicObject(1502,'), 1)
        self.assertIn('LOD instance', log)


if __name__ == '__main__':
    unittest.main()
