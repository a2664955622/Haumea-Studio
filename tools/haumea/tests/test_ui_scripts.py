"""Run the UI scripts in ui/ through the editor's own input driver.

Each script (ui/*.uis) is played with --ui-script under xvfb-run, against
the real game when HAUMEA_GAME_DIR names one (not a synthetic one), else against the synthetic
game from fixture/make_game.py; scripts with a "# needs: game" line only run
against the real game. Every run has its own config folder. A failing script
prints its log.
"""
from pathlib import Path
import glob
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from haumea_test_env import editor_env

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
UI = HERE / 'ui'
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
XVFB = shutil.which('xvfb-run')


def img_entries(path):
    with open(path, 'rb') as f:
        assert f.read(4) == b'VER2'
        count, = struct.unpack('<I', f.read(4))
        entries = {}
        for _ in range(count):
            offset, size, size2, name = struct.unpack('<IHH24s', f.read(32))
            entries[name.split(b'\0')[0].decode().lower()] = (offset * 2048, (size or size2) * 2048)
        return entries


def real_game():
    """HAUMEA_GAME_DIR when it is the real game: run_all.sh --fixture points it at the synthetic one."""
    game = os.environ.get('HAUMEA_GAME_DIR')
    try:
        return game if game and len(img_entries(Path(game, 'models', 'gta3.img'))) > 1000 else None
    except (OSError, AssertionError, struct.error):
        return None


REAL_GAME = real_game()


def needs(script):
    for line in script.read_text().splitlines():
        if line.startswith('# needs:'):
            return line.split(':', 1)[1].split()
    return []


@unittest.skipUnless(BINARY and XVFB, 'needs the editor and xvfb-run')
class UiScriptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='haumea-ui-')
        cls.game = REAL_GAME
        if not cls.game:
            cls.game = os.path.join(cls.tmp, 'game')
            subprocess.run([sys.executable, str(HERE / 'fixture' / 'make_game.py'), cls.game],
                           check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def play(self, name):
        script = UI / name
        if 'game' in needs(script) and not REAL_GAME:
            self.skipTest('needs the real game (HAUMEA_GAME_DIR)')
        work = tempfile.mkdtemp(dir=self.tmp)
        env = editor_env(work)
        env.pop('WAYLAND_DISPLAY', None)
        # A copy, so scripts can save over the maps they open.
        shutil.copytree(UI / 'fixtures', Path(work, 'fixtures'))
        env['UI_FIXTURES'] = str(Path(work, 'fixtures'))
        server = Path(work, 'server')
        (server / 'gamemodes').mkdir(parents=True)
        (server / 'models').mkdir()
        env['SERVER'] = str(server)
        args = [BINARY, '--ui-script', str(script)]
        if name == 'first_run.uis':
            env.pop('HAUMEA_GAME_DIR', None)	# no game folder known
        else:
            args[1:1] = ['--game', self.game, '--workspace', str(server)]
        if name == 'custom_model.uis':
            entries = img_entries(Path(self.game, 'models', 'gta3.img'))
            with open(Path(self.game, 'models', 'gta3.img'), 'rb') as f:
                for source, target in (('barrel4.dff', 'ui_barrel.dff'), ('dynbarrels.txd', 'ui_barrel.txd')):
                    offset, size = entries[source]
                    f.seek(offset)
                    Path(work, target).write_bytes(f.read(size))
            env['DFF'] = str(Path(work, 'ui_barrel.dff'))
            env['TXD'] = str(Path(work, 'ui_barrel.txd'))
        result = subprocess.run([XVFB, '-a', '-s', '-screen 0 1600x1000x24'] + args, cwd=work, env=env,
                                capture_output=True, text=True, timeout=600)
        log = '\n'.join(line for line in result.stdout.splitlines() if line.startswith('[ui-script]'))
        self.assertEqual(result.returncode, 0, log or result.stdout[-3000:] + result.stderr[-2000:])


def add_script_test(path):
    def test(self):
        self.play(path.name)
    setattr(UiScriptTests, 'test_' + path.stem, test)


for path in sorted(UI.glob('*.uis')):
    add_script_test(path)


if __name__ == '__main__':
    unittest.main()
