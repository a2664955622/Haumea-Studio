# SPDX-License-Identifier: GPL-2.0-or-later
"""Undo, autosave recovery and safe saving, on a map with every element kind.

Needs a built editor, a GTA San Andreas directory with SA-MP installed and a
display: set HAUMEA_GAME_DIR (and HAUMEA_BINARY if it is not in bin/).
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
FIXTURE = Path(__file__).resolve().parent / 'fixtures' / 'every_element.pwn'
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
HAVE_DISPLAY = bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))


@unittest.skipUnless(GAME_DIR and BINARY and HAVE_DISPLAY,
                     'set HAUMEA_GAME_DIR and build the editor to run')
class RobustnessTests(unittest.TestCase):
    def run_editor(self, tmp, *args):
        return subprocess.run([BINARY, '--game', GAME_DIR, *args], cwd=tmp, env=editor_env(tmp),
                              capture_output=True, text=True, timeout=600)

    def test_every_edit_is_one_exact_undo_step(self):
        # Each kind of edit, then undo and redo, compared as exported Pawn.
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp, 'map.pwn')
            source.write_text(FIXTURE.read_text())
            report = Path(tmp, 'report.txt')
            result = self.run_editor(tmp, '--undo-selftest', str(source), str(report))
            text = report.read_text() if report.exists() else result.stdout[-2000:]
            self.assertIn('undo self-test:', text, text)
            failures = [line for line in text.splitlines() if line.startswith('FAIL')]
            self.assertEqual(failures, [], text)

    def test_autosave_recovers_the_same_map(self):
        # An autosave is the exporter's output; recovering opens it. Opening
        # and saving it again must give the same file, with every kind kept.
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp, 'map.pwn')
            source.write_text(FIXTURE.read_text())
            # Same file name: the exporter names its functions after the file.
            Path(tmp, 'autosave').mkdir()
            Path(tmp, 'recovered').mkdir()
            autosave = Path(tmp, 'autosave', 'map.pwn')
            recovered = Path(tmp, 'recovered', 'map.pwn')
            self.run_editor(tmp, '--pawn-roundtrip', str(source), str(autosave))
            self.run_editor(tmp, '--pawn-roundtrip', str(autosave), str(recovered))
            first, second = autosave.read_text(), recovered.read_text()
            self.assertEqual(first, second, 'recovering an autosave must not change the map')
            for call in ('CreateDynamicObject', 'AddStaticVehicleEx', 'ChangeVehiclePaintjob',
                         'SetVehicleNumberPlate', 'CreateDynamicPickup', 'CreateDynamic3DTextLabel',
                         'CreateDynamicActor', 'AddPlayerClass', 'CreateDynamicCP', 'CreateDynamicRaceCP',
                         'GangZoneCreate', 'CreateDynamicMapIcon', 'NPC_Create', 'NPC_AddPointToPath',
                         'PlayAudioStreamForPlayer', 'TextDrawCreate'):
                self.assertIn(call, first, call + ' was lost')
            # Saving goes through a temporary file renamed over the map.
            self.assertEqual(list(Path(tmp).rglob('*.tmp')), [])


if __name__ == '__main__':
    unittest.main()
