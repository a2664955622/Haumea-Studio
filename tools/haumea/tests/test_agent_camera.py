"""Namaka moves the user's live camera: move_camera flies there, camera_back returns.

One editor in its own Xvfb with a private config, the agent socket at a short
private path, and a UI script that holds the right mouse button (looking
around) when the test writes "hold" to a trigger file, and lets go on
"release". Checked through HaumeaService (what the MCP tools call), and through
the MCP tools themselves when the mcp package is installed:
- move_camera to a pose changes the camera to it, and camera_back restores the
  pose from before;
- look_at [x, y, z] looks at that point;
- while the user looks around, a move is refused as busy and the camera stays;
  after they let go it works again.
"""
from pathlib import Path
import glob
import math
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
import contextlib  # noqa: E402
from haumea_test_env import editor_env, game_slot  # noqa: E402

BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
XVFB = shutil.which('xvfb-run')
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')

# Each wait_until gives up after 600 frames; repeated, the script waits long
# enough for a slow start, and once the trigger is there the rest pass at once.
SCRIPT = 'open "$CAMERA_MAP"\nwait 5\n' + ''.join(['wait_until file_contains "$CAMERA_TRIGGER" "hold"\n'] * 20) + '''move 50% 50%
wait 3
hold right
wait 3
move 52% 51%
''' + ''.join(['wait_until file_contains "$CAMERA_TRIGGER" "release"\n'] * 20) + '''release right
''' + ''.join(['wait_until file_contains "$CAMERA_TRIGGER" "quit"\n'] * 20)


def close(a, b, tolerance=0.01):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


@unittest.skipUnless(BINARY and XVFB and sys.platform.startswith('linux'), 'needs the editor and xvfb-run on Linux')
class AgentCameraTests(unittest.TestCase):
    def setUp(self):
        # Short: AF_UNIX paths are limited to 108 bytes.
        self.tmp = Path(tempfile.mkdtemp(prefix='hac-', dir='/tmp'))
        game = GAME_DIR
        if not game:
            game = str(self.tmp / 'game')
            subprocess.run([sys.executable, str(HERE / 'fixture' / 'make_game.py'), game],
                           check=True, capture_output=True)
        self.trigger = self.tmp / 'trigger'
        self.trigger.write_text('')
        script = self.tmp / 'camera.uis'
        script.write_text(SCRIPT)
        self.socket = self.tmp / 'a.sock'
        env = editor_env(str(self.tmp), virtual_display=True)
        env['HAUMEA_TEST_XVFB'] = '1'
        env['CAMERA_TRIGGER'] = str(self.trigger)
        shutil.copy(HERE / 'ui' / 'fixtures' / 'ui_map.pwn', self.tmp / 'map.pwn')
        env['CAMERA_MAP'] = str(self.tmp / 'map.pwn')
        self.log = open(self.tmp / 'out.txt', 'w')
        # On the real game, one of the machine-wide real-game slots for as long as it runs.
        slot = contextlib.ExitStack()
        slot.enter_context(game_slot(game))
        self.addCleanup(slot.close)	# after tearDown, also when setUp fails
        self.editor = subprocess.Popen(
            [XVFB, '-a', '-s', '-screen 0 1600x1000x24', BINARY, '--game', game,
             '--agent-socket', str(self.socket), '--ui-script', str(script)],
            cwd=self.tmp, env=env, stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True)
        self.addCleanup(self.stop)	# before the slot is let go, also when setUp fails
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
        self.trigger.write_text('quit')
        try:
            os.killpg(self.editor.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.editor.wait()
        self.log.close()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def output(self):
        return (self.tmp / 'out.txt').read_text(errors='replace')[-3000:]

    def camera(self):
        return self.service.engine('camera_context')['camera']

    def test_move_back_and_busy(self):
        start = self.camera()
        moved = self.service.move_camera(position=[1900, -2900, 60], target=[1950, -2950, 5])
        self.assertTrue(close(moved['camera']['position'], [1900, -2900, 60]), moved)
        self.assertTrue(close(moved['camera']['target'], [1950, -2950, 5]), moved)
        self.assertGreater(moved['camera_revision'], start['camera_revision'])
        self.assertTrue(close(self.camera()['position'], [1900, -2900, 60]))

        framed = self.service.move_camera(look_at=[2000, -3000, 10], distance=30, pitch=30)
        self.assertTrue(close(framed['camera']['target'], [2000, -3000, 10]), framed)
        offset = [p - t for p, t in zip(framed['camera']['position'], framed['camera']['target'])]
        self.assertAlmostEqual(math.sqrt(sum(v * v for v in offset)), 30, delta=0.05)
        self.assertAlmostEqual(offset[2], 15, delta=0.05)	# 30 degrees down

        back = self.service.camera_back()
        self.assertTrue(close(back['camera']['position'], [1900, -2900, 60]), back)
        back = self.service.camera_back()
        self.assertTrue(close(back['camera']['position'], start['position']), back)
        self.assertTrue(close(back['camera']['target'], start['target']), back)
        with self.assertRaisesRegex(Exception, 'no earlier camera'):
            self.service.camera_back()

        # The user looks around with the right mouse button: refused, camera kept.
        self.trigger.write_text('hold')
        refusal = None
        end = time.time() + 120
        while time.time() < end and refusal is None:
            try:
                self.service.move_camera(position=[1800, -2800, 40], target=[1850, -2850, 5])
                self.service.camera_back()
            except Exception as error:
                refusal = str(error)
            time.sleep(0.2)
        self.assertIsNotNone(refusal, 'never busy while the right mouse button was held\n' + self.output())
        self.assertIn('busy', refusal)
        self.assertIn('try again', refusal)
        held = self.camera()
        self.trigger.write_text('release')
        moved = None
        end = time.time() + 60
        while time.time() < end and moved is None:
            try:
                moved = self.service.move_camera(position=[1800, -2800, 40], target=[1850, -2850, 5])
            except Exception as error:
                self.assertIn('busy', str(error))
                time.sleep(0.2)
        self.assertIsNotNone(moved, 'still busy after the button was let go')
        self.assertTrue(close(moved['camera']['position'], [1800, -2800, 40]), moved)
        self.assertFalse(close(held['position'], [1800, -2800, 40]))

    def test_mcp_tools(self):
        # Importing it makes its own service: keep that one private too, and
        # leave the environment as it was for the other tests in this process.
        private = dict(HAUMEA_ENGINE_SOCKET=str(self.socket), HAUMEA_AGENT_STATE_DIR=str(self.tmp / 'state'),
                       HAUMEA_ASSET_DB=str(self.tmp / 'assets.sqlite3'),
                       HAUMEA_DISCOVERY_DIR=str(self.tmp / 'discoveries'))
        saved = {name: os.environ.get(name) for name in private}
        os.environ.update(private)
        try:
            try:
                import haumea_mcp
            except ImportError:
                self.skipTest('needs the mcp package (the Namaka venv)')
        finally:
            for name, value in saved.items():
                if value is None:
                    os.environ.pop(name, None)
                else:
                    os.environ[name] = value
        import asyncio
        own = haumea_mcp.service
        haumea_mcp.service = self.service
        try:
            tools = {tool.name for tool in asyncio.run(haumea_mcp.mcp.list_tools())}
            self.assertLessEqual({'move_camera', 'camera_back'}, tools)
            start = self.camera()
            asyncio.run(haumea_mcp.mcp.call_tool('move_camera', {'position': [1900, -2900, 60],
                                                                 'target': [1950, -2950, 5]}))
            self.assertTrue(close(self.camera()['position'], [1900, -2900, 60]))
            asyncio.run(haumea_mcp.mcp.call_tool('camera_back', {}))
            self.assertTrue(close(self.camera()['position'], start['position']))
        finally:
            haumea_mcp.service = own

if __name__ == '__main__':
    unittest.main()
