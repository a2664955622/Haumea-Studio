# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise a live executable replacement, including the final exec step."""
from pathlib import Path
import glob
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from haumea_test_env import editor_env, run_editor

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)


@unittest.skipUnless(BINARY and sys.platform.startswith('linux'), 'needs a Linux editor build')
class UpdaterRelaunchTests(unittest.TestCase):
    def test_startup_executes_the_new_install_instead_of_the_backup(self):
        with tempfile.TemporaryDirectory(prefix='haumea self update ') as tmp:
            work = Path(tmp)
            install = work / 'Haumea Studio'
            install.mkdir()
            engine = install / 'haumea'
            shutil.copy2(BINARY, engine)
            old_hash = hashlib.sha256(engine.read_bytes()).hexdigest()
            untouched = install / 'my-map.pwn'
            untouched.write_text('// user map\n')
            game = work / 'game'
            subprocess.run([sys.executable, str(HERE / 'fixture/make_game.py'), str(game)],
                           check=True, capture_output=True)
            env = editor_env(work)
            env['HAUMEA_NO_UPDATE'] = '0'
            env['HAUMEA_UPDATE_URL'] = 'http://127.0.0.1:9'
            marker = work / 'updated-arguments.txt'
            env['HAUMEA_RELAUNCH_MARKER'] = str(marker)
            staged = Path(env['XDG_CONFIG_HOME'], 'haumea/update/staged')
            staged.mkdir(parents=True)
            # A replacement that records its real launch, rather than merely
            # trusting the updater's success notice or the installed file hash.
            replacement = b'#!/bin/sh\nprintf "%s\\n" "$@" > "$HAUMEA_RELAUNCH_MARKER"\n'
            (staged / 'haumea').write_bytes(replacement)
            (staged / 'haumea').chmod(0o755)
            (staged / 'MANIFEST').write_text(hashlib.sha256(replacement).hexdigest() + '  haumea\n')
            (staged / '.haumea-release').write_text('99.0.0.1\nproduction\n')
            args = ['--game', str(game), '--workspace', str(work)]
            result = run_editor([engine, *args], cwd=work, env=env, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertTrue(marker.exists(), result.stdout)
            self.assertEqual(marker.read_text().splitlines(), args)
            self.assertEqual(engine.read_bytes(), replacement)
            backup = install / '.haumea-previous/files/haumea'
            self.assertEqual(hashlib.sha256(backup.read_bytes()).hexdigest(), old_hash)
            self.assertEqual(untouched.read_text(), '// user map\n')


if __name__ == '__main__':
    unittest.main()
