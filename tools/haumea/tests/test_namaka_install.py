# SPDX-License-Identifier: GPL-2.0-or-later
"""Namaka's installer (tools/release/namaka_install.py) with the network cut off.

Builds the wheel from this checkout (make_bundle, also used by namaka.uis), puts it with a wheelhouse and the
installer in a scratch bundle, then installs into a scratch venv with no
network: Python 3.14 installs everything from the wheelhouse. The MCP server
must start and list as many tools as CI expects (AGENT_MCP_TOOLS). Then
status, repair (the venv is rebuilt, the old one kept until it works) and
uninstall.

Needs NAMAKA_WHEELHOUSE (a wheelhouse from tools/release/fetch_wheelhouse.sh
or an Agent Alpha/Namaka bundle) and a CPython 3.14 (python3.14, or this
Python). The network is cut with `unshare -rn` when it works, else pip gets
a proxy that goes nowhere.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
INSTALLER = ROOT / 'tools' / 'release' / 'namaka_install.py'
WHEELHOUSE = os.environ.get('NAMAKA_WHEELHOUSE')


def python314():
    if sys.version_info[:2] == (3, 14):
        return sys.executable
    return shutil.which('python3.14')


def expected_tools():
    workflow = (ROOT / '.github' / 'workflows' / 'build-haumea.yml').read_text()
    return int(re.search(r'AGENT_MCP_TOOLS:\s*(\d+)', workflow).group(1))


def no_network_prefix():
    """unshare -rn when this system allows it (a user namespace without network)."""
    if shutil.which('unshare') and subprocess.run(['unshare', '-rn', 'true'], capture_output=True).returncode == 0:
        return ['unshare', '-rn']
    return []


def offline_env():
    # Without unshare: any download goes to a proxy that isn't there.
    return dict(os.environ, HTTP_PROXY='http://127.0.0.1:9', HTTPS_PROXY='http://127.0.0.1:9',
                http_proxy='http://127.0.0.1:9', https_proxy='http://127.0.0.1:9', PIP_NO_INDEX='1')


def make_bundle(work):
    """A Namaka bundle in work/bundle: the wheel built from this checkout, the
    wheelhouse and the installer. None without NAMAKA_WHEELHOUSE or Python 3.14."""
    if not (WHEELHOUSE and Path(WHEELHOUSE).is_dir() and python314()):
        return None
    work = Path(work)
    src = work / 'src'
    src.mkdir(parents=True)
    # Built from a copy, so no egg-info lands in the checkout.
    shutil.copy2(ROOT / 'pyproject.toml', src / 'pyproject.toml')
    shutil.copytree(ROOT / 'tools' / 'agent', src / 'tools' / 'agent',
                    ignore=shutil.ignore_patterns('__pycache__'))
    bundle = work / 'bundle'
    bundle.mkdir()
    subprocess.run([sys.executable, '-c', 'import sys; from setuptools import build_meta; '
                    'build_meta.build_wheel(sys.argv[1])', str(bundle)], cwd=src, check=True,
                   capture_output=True)
    shutil.copytree(WHEELHOUSE, bundle / 'wheelhouse')
    shutil.copy2(INSTALLER, bundle / 'namaka_install.py')
    return bundle


@unittest.skipUnless(WHEELHOUSE and Path(WHEELHOUSE or '.').is_dir() and python314(),
                     'set NAMAKA_WHEELHOUSE and have CPython 3.14 to run')
class NamakaInstallTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix='namaka-'))
        cls.bundle = make_bundle(cls.tmp)
        cls.venv = cls.tmp / 'config' / 'namaka' / 'venv'

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_installer(self, action, *extra):
        argv = no_network_prefix() + [python314(), str(self.bundle / 'namaka_install.py'), action,
                                      '--bundle', str(self.bundle), '--venv', str(self.venv)] + list(extra)
        result = subprocess.run(argv, capture_output=True, text=True, env=offline_env(), timeout=900)
        return result.returncode, result.stdout + result.stderr

    def test_install_repair_uninstall(self):
        wheels = list(self.bundle.glob('haumea_namaka-*.whl'))
        self.assertEqual(len(wheels), 1, 'the wheel is named haumea_namaka-*')

        code, out = self.run_installer('install')
        self.assertEqual(code, 0, out[-3000:])
        self.assertIn('STEP 3/4 Installing Namaka from the bundle (offline)', out)
        self.assertIn('TOOLS %d' % expected_tools(), out)
        self.assertTrue((self.venv / 'namaka-complete').is_file())
        self.assertEqual((self.venv / 'namaka-complete').read_text().strip(), wheels[0].name)

        code, out = self.run_installer('status')
        self.assertEqual(code, 0, out[-3000:])
        self.assertIn('PYTHON 3.14', out)
        self.assertRegex(out, r'VERSION \S+')
        self.assertIn('TOOLS %d' % expected_tools(), out)

        # Repair rebuilds it in place; nothing is left aside.
        code, out = self.run_installer('repair')
        self.assertEqual(code, 0, out[-3000:])
        self.assertIn('Setting the old install aside', out)
        self.assertFalse(self.venv.with_name('venv.old').exists())

        code, out = self.run_installer('uninstall')
        self.assertEqual(code, 0, out[-3000:])
        self.assertFalse(self.venv.exists())

    def test_cancelled_run_is_put_back(self):
        # A repair stopped after moving the old install aside: the next run restores it.
        code, out = self.run_installer('install')
        self.assertEqual(code, 0, out[-3000:])
        old = self.venv.with_name('venv.old')
        self.venv.rename(old)
        self.venv.mkdir()	# half-made, no namaka-complete
        code, out = self.run_installer('status')
        self.assertNotEqual(code, 0)	# status doesn't recover...
        code, out = self.run_installer('uninstall')	# ...but every changing run does
        self.assertIn('Put back the Namaka install', out)
        self.assertFalse(self.venv.exists())
        self.assertFalse(old.exists())


if __name__ == '__main__':
    unittest.main()
