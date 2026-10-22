"""What Namaka is doing shows on the keycast overlay.

One editor in its own Xvfb with a private config and the agent socket at a
short private path. The test places two pickups and moves the camera through
HaumeaService (what the MCP tools call); the editor's UI script waits for the
overlay to show "Namaka: placing elements: 2" (two place_element calls added
up in one line) and then "Namaka: moving the camera", and the editor's exit
code says whether it saw them. With keycast_agent 0 in savesettings.txt, the
same edits show nothing.
"""
from pathlib import Path
import contextlib
import glob
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / 'tools' / 'agent'))
from haumea_test_env import editor_env, game_slot  # noqa: E402

BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
XVFB = shutil.which('xvfb-run')
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')


def waits(condition, times=20):
    # Each wait_until gives up after 600 frames; repeated, it outlasts a slow start.
    return ''.join(['wait_until %s\n' % condition] * times)


SHOWN = ('open "$AGENTCAST_MAP"\nwait 5\n' + waits('keycast_contains "placing elements: 2"') +
         'assert keycast_contains "Namaka: placing elements: 2"\n' +
         waits('keycast_contains "moving the camera"') +
         'assert keycast_contains "Namaka: moving the camera"\n')
# Off: the edits are done (the trigger file says so) and nothing showed.
HIDDEN = ('open "$AGENTCAST_MAP"\nwait 5\n' + waits('file_contains "$AGENTCAST_TRIGGER" "done"') +
          'assert keycast_lacks "Namaka"\n')


@unittest.skipUnless(BINARY and XVFB and sys.platform.startswith('linux'), 'needs the editor and xvfb-run on Linux')
class AgentcastTests(unittest.TestCase):
    def start(self, script, settings=''):
        # Short: AF_UNIX paths are limited to 108 bytes.
        self.tmp = Path(tempfile.mkdtemp(prefix='hkc-', dir='/tmp'))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        game = GAME_DIR
        if not game:
            game = str(self.tmp / 'game')
            subprocess.run([sys.executable, str(HERE / 'fixture' / 'make_game.py'), game],
                           check=True, capture_output=True)
        (self.tmp / 'script.uis').write_text(script)
        self.trigger = self.tmp / 'trigger'
        self.trigger.write_text('')
        self.socket = self.tmp / 'a.sock'
        env = editor_env(str(self.tmp), virtual_display=True)
        env['HAUMEA_TEST_XVFB'] = '1'
        env['AGENTCAST_TRIGGER'] = str(self.trigger)
        shutil.copy(HERE / 'ui' / 'fixtures' / 'ui_map.pwn', self.tmp / 'map.pwn')
        env['AGENTCAST_MAP'] = str(self.tmp / 'map.pwn')
        if settings:
            config = Path(env['XDG_CONFIG_HOME'], 'haumea')
            config.mkdir(parents=True, exist_ok=True)
            (config / 'savesettings.txt').write_text(settings)
        self.log = open(self.tmp / 'out.txt', 'w')
        self.addCleanup(self.log.close)
        slot = contextlib.ExitStack()
        slot.enter_context(game_slot(game))
        self.addCleanup(slot.close)
        self.editor = subprocess.Popen(
            [XVFB, '-a', '-s', '-screen 0 1600x1000x24', BINARY, '--game', game,
             '--agent-socket', str(self.socket), '--ui-script', str(self.tmp / 'script.uis')],
            cwd=self.tmp, env=env, stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True)
        self.addCleanup(self.stop)
        from service import HaumeaService
        self.service = HaumeaService(self.socket, self.tmp / 'assets.sqlite3',
                                     discovery_dir=self.tmp / 'discoveries', state_dir=self.tmp / 'state')
        end = time.time() + 240
        while time.time() < end:
            try:
                if self.service.engine('ping').get('result') == 'pong':
                    break
            except Exception:
                pass
            if self.editor.poll() is not None:
                break
            time.sleep(0.5)
        else:
            self.fail('the editor did not answer on ' + str(self.socket))
        self.assertIsNone(self.editor.poll(), self.output())

    def stop(self):
        try:
            os.killpg(self.editor.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.editor.wait()

    def output(self):
        return (self.tmp / 'out.txt').read_text(errors='replace')[-3000:]

    def edit(self):
        self.service.place_elements([{'kind': 'pickup', 'x': 2004, 'y': -3004, 'z': 5, 'snap': False},
                                     {'kind': 'pickup', 'x': 2008, 'y': -3004, 'z': 5, 'snap': False}])
        self.service.move_camera(position=[1990, -3020, 20], target=[2004, -3004, 5])
        self.trigger.write_text('done')

    def finished(self):
        try:
            return self.editor.wait(timeout=240)
        except subprocess.TimeoutExpired:
            self.fail('the UI script did not finish\n' + self.output())

    def test_agent_actions_show(self):
        self.start(SHOWN)
        self.edit()
        self.assertEqual(self.finished(), 0, self.output())

    def test_hidden_when_turned_off(self):
        self.start(HIDDEN, settings='keycast_agent 0\n')
        self.edit()
        self.assertEqual(self.finished(), 0, self.output())


if __name__ == '__main__':
    unittest.main()
