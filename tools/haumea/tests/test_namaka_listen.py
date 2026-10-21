"""A plain Haumea start listens for Namaka when Namaka is installed.

Three editors, each in its own Xvfb with a private config. Namaka is faked
as installed (HAUMEA_NAMAKA_VENV with a namaka-complete marker), and the
socket goes to a short private path (HAUMEA_NAMAKA_SOCKET), never the user's
~/.cache/haumea-agent.sock.
- The first editor, allowed to listen (HAUMEA_NAMAKA_LISTEN=1, since test
  runs don't otherwise), opens the socket and answers a ping.
- The second, with the same socket, finds it in use and says so, and the
  first still answers: one Haumea never takes another's socket.
- The third runs as a test normally would (no HAUMEA_NAMAKA_LISTEN) and
  listens on nothing.
"""
from pathlib import Path
import glob
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / 'tools' / 'agent'))
from haumea_test_env import editor_env  # noqa: E402

BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
XVFB = shutil.which('xvfb-run')
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')


def ping(path):
    """True when something answers the agent protocol's ping on path."""
    from haumea_ipc import HaumeaClient
    try:
        return bool(HaumeaClient(str(path)).command('ping').get('ok'))
    except Exception:
        return False


def wait_for(check, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if check():
            return True
        time.sleep(0.5)
    return False


@unittest.skipUnless(BINARY and XVFB and sys.platform.startswith('linux'), 'needs the editor and xvfb-run on Linux')
class NamakaListenTests(unittest.TestCase):
    def setUp(self):
        # Short: AF_UNIX paths are limited to 108 bytes.
        self.tmp = Path(tempfile.mkdtemp(prefix='hnl-', dir='/tmp'))
        self.game = GAME_DIR
        if not self.game:
            self.game = str(self.tmp / 'game')
            subprocess.run([sys.executable, str(HERE / 'fixture' / 'make_game.py'), self.game],
                           check=True, capture_output=True)
        venv = self.tmp / 'venv'
        (venv / 'bin').mkdir(parents=True)
        (venv / 'bin' / 'python').write_text('')
        (venv / 'namaka-complete').write_text('haumea_namaka-0.1.0-py3-none-any.whl\n')
        self.venv = venv
        self.socket = self.tmp / 'n.sock'
        self.editors = []
        self.logs = []

    def tearDown(self):
        for process in self.editors:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        for log in self.logs:
            log.close()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def start(self, name, listen):
        work = self.tmp / name
        work.mkdir()
        env = editor_env(str(work), virtual_display=True)
        env['HAUMEA_NAMAKA_VENV'] = str(self.venv)
        env['HAUMEA_NAMAKA_SOCKET'] = str(self.socket if listen else work / 'n.sock')
        env['HAUMEA_TEST_XVFB'] = '1'	# a test run, as run_all.sh marks it
        env.pop('HAUMEA_NAMAKA_LISTEN', None)
        if listen:
            env['HAUMEA_NAMAKA_LISTEN'] = '1'
        log = open(work / 'out.txt', 'w')
        self.logs.append(log)
        process = subprocess.Popen([XVFB, '-a', '-s', '-screen 0 1280x800x24', BINARY, '--game', self.game],
                                   cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        self.editors.append(process)
        return work

    def test_listens_once_and_never_steals(self):
        self.start('first', True)
        self.assertTrue(wait_for(lambda: self.socket.exists() and ping(self.socket), 240),
                        'the first editor did not listen on ' + str(self.socket))
        mode = self.socket.stat().st_mode & 0o777
        self.assertEqual(mode, 0o600, 'the socket is only for its user')

        second = self.start('second', True)
        self.assertTrue(wait_for(lambda: 'is in use by another process' in (second / 'out.txt').read_text(), 240),
                        (second / 'out.txt').read_text()[-2000:])
        self.assertTrue(ping(self.socket), 'the first editor lost its socket')

        third = self.start('third', False)
        # It is up once it says where the game is; give it as long again to (not) listen.
        self.assertTrue(wait_for(lambda: 'Game folder' in (third / 'out.txt').read_text(), 240))
        time.sleep(15)
        self.assertFalse((third / 'n.sock').exists(), 'a test run listened for Namaka')


if __name__ == '__main__':
    unittest.main()
