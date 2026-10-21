# SPDX-License-Identifier: GPL-2.0-or-later
"""Version numbers: parsing tags old and new, the four-part compare, the
build info generator, and the agent wheel's version.

Compiles tools/haumea/versionutil.cpp with a small driver (needs g++).
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
sys.path.insert(0, str(ROOT / 'tools' / 'release'))
import gen_buildinfo  # noqa: E402
import check_tag  # noqa: E402

DRIVER = r'''
#include "versionutil.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
	if(strcmp(argv[1], "parse") == 0){
		HaumeaVersion v;
		if(!ParseHaumeaVersion(argv[2], &v)) { puts("invalid"); return 0; }
		printf("%d.%d.%d.%d\n", v.part[0], v.part[1], v.part[2], v.part[3]);
	}else if(strcmp(argv[1], "channel") == 0){
		char out[64];
		puts(HaumeaTagChannel(argv[2], out, sizeof(out)) ? out : "none");
	}else if(strcmp(argv[1], "compare") == 0){
		HaumeaVersion a, b;
		ParseHaumeaVersion(argv[2], &a);
		ParseHaumeaVersion(argv[3], &b);
		int c = CompareHaumeaVersion(a, b);
		puts(c < 0 ? "<" : c > 0 ? ">" : "=");
	}else if(strcmp(argv[1], "current") == 0)
		printf("%s|%s|%d|%s\n", HaumeaFullVersion(), HaumeaVersionDisplay(), HaumeaBuildNumber(), HaumeaCommit());
	return 0;
}
'''


@unittest.skipUnless(shutil.which('g++'), 'needs g++')
class VersionUtilTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.tmp = tempfile.mkdtemp(prefix='haumea-version-')
		Path(cls.tmp, 'driver.cpp').write_text(DRIVER)
		# Copies, so a build's own haumea_buildinfo.h next to the source isn't used.
		for name in ('versionutil.cpp', 'versionutil.h', 'version.h'):
			shutil.copy2(ROOT / 'tools' / 'haumea' / name, Path(cls.tmp, name))
		# A build info header of our own, so the numbers are known.
		Path(cls.tmp, 'haumea_buildinfo.h').write_text('#define HAUMEA_BUILD 57\n#define HAUMEA_COMMIT "abc1234"\n')
		cls.exe = str(Path(cls.tmp, 'driver'))
		subprocess.run(['g++', '-std=c++17', '-I', cls.tmp,
		                str(Path(cls.tmp, 'driver.cpp')), str(Path(cls.tmp, 'versionutil.cpp')),
		                '-o', cls.exe], check=True)

	@classmethod
	def tearDownClass(cls):
		shutil.rmtree(cls.tmp, ignore_errors=True)

	def run_driver(self, *args):
		return subprocess.run([self.exe, *args], capture_output=True, text=True, check=True).stdout.strip()

	def test_parse(self):
		cases = {
			'1.41.0': '1.41.0.0', '1.41.0.57': '1.41.0.57', 'v1.41.0.57-master': '1.41.0.57',
			'v1.41.0-master-abc1234': '1.41.0.0', 'v1.41.0.57-agent-alpha': '1.41.0.57',
			'v1.41.0.57-PE': '1.41.0.57', '1.41': '1.41.0.0',
			'': 'invalid', 'master': 'invalid', '1..2': 'invalid', '1.2.3.4.5': 'invalid',
			'1.2x': 'invalid', 'v': 'invalid',
		}
		for text, expected in cases.items():
			self.assertEqual(self.run_driver('parse', text), expected, text)

	def test_channel(self):
		cases = {
			'v1.41.0-master-abc1234': 'master', 'v1.41.0.57-agent-alpha': 'agent-alpha',
			'v1.41.0.57-PE': 'PE', 'v1.41.0.57-master': 'master',
			'v1.41.0-agent-alpha-0123456789abcdef0123456789abcdef01234567': 'agent-alpha',
			'v1.41.0': 'none', 'v1.41.0-': 'none',
			'v1.41.0.57-beef': 'beef',	# under 7 hex digits: a channel, not a commit
		}
		for tag, expected in cases.items():
			self.assertEqual(self.run_driver('channel', tag), expected, tag)

	def test_compare(self):
		cases = [
			('1.41.0.57', '1.41.0.56', '>'), ('1.41.0.9', '1.41.0.10', '<'),
			('1.41.1.3', '1.41.0.900', '>'), ('1.41.0', '1.41.0.0', '='),
			# An old tag (no build) against a new one of the same X.Y.Z.
			('v1.41.0-master-abc1234', 'v1.41.0.57-master', '<'),
			('1.42.0', '1.41.9.999', '>'), ('2.0.0.1', '10.0.0.0', '<'),
		]
		for a, b, expected in cases:
			self.assertEqual(self.run_driver('compare', a, b), expected, (a, b))

	def test_old_installs_need_a_patch_bump(self):
		# What installs from before four-part versions do: X.Y.Z only. A new
		# build of the same X.Y.Z looks the same to them, a PATCH bump newer.
		def old_newer(remote, local):
			r = [int(x) for x in re.match(r'v?(\d+)\.(\d+)\.(\d+)', remote).groups()]
			l = [int(x) for x in re.match(r'v?(\d+)\.(\d+)\.(\d+)', local).groups()]
			return r > l
		self.assertFalse(old_newer('v1.41.0.80-master', '1.41.0'))
		self.assertTrue(old_newer('v1.41.1.80-master', '1.41.0'))

	def test_current(self):
		full, display, build, commit = self.run_driver('current').split('|')
		major, minor, patch = gen_buildinfo.base_version()
		self.assertEqual(full, f'{major}.{minor}.{patch}.57')
		self.assertEqual(display, f'{full} (abc1234)')
		self.assertEqual((build, commit), ('57', 'abc1234'))


class BuildInfoTests(unittest.TestCase):
	def test_header_and_wheel_version(self):
		with tempfile.TemporaryDirectory() as tmp:
			env = dict(os.environ, HAUMEA_BUILD='123', HAUMEA_COMMIT='abc1234-dirty')
			header, py = Path(tmp, 'b.h'), Path(tmp, 'v.py')
			subprocess.run([sys.executable, str(ROOT / 'tools/release/gen_buildinfo.py'),
			                '--header', str(header), '--python', str(py)], env=env, check=True)
			self.assertIn('#define HAUMEA_BUILD 123', header.read_text())
			self.assertIn('#define HAUMEA_COMMIT "abc1234-dirty"', header.read_text())
			major, minor, patch = gen_buildinfo.base_version()
			# PEP 440: a dirty tree is a local version.
			self.assertIn(f'__version__ = "{major}.{minor}.{patch}.123+dirty"', py.read_text())
			# Unchanged content is not rewritten, so nothing recompiles.
			before = header.stat().st_mtime_ns
			os.utime(header, ns=(before - 10**9, before - 10**9))
			subprocess.run([sys.executable, str(ROOT / 'tools/release/gen_buildinfo.py'),
			                '--header', str(header)], env=env, check=True)
			self.assertEqual(header.stat().st_mtime_ns, before - 10**9)

	def test_bad_environment(self):
		env = dict(os.environ, HAUMEA_BUILD='12a')
		result = subprocess.run([sys.executable, str(ROOT / 'tools/release/gen_buildinfo.py'), '--print'],
		                        env=env, capture_output=True, text=True)
		self.assertNotEqual(result.returncode, 0)

	def test_committed_wheel_version_matches_version_h(self):
		text = (ROOT / 'tools/agent/_version.py').read_text()
		major, minor, patch = gen_buildinfo.base_version()
		self.assertIn(f'__version__ = "{major}.{minor}.{patch}.0"', text)


class ReleaseTagTests(unittest.TestCase):
	def test_parse_tag(self):
		self.assertEqual(check_tag.parse_tag('v1.41.0.57-master'), (1, 41, 0, 57, 'master'))
		self.assertEqual(check_tag.parse_tag('v1.41.0-master-abc1234'), (1, 41, 0, None, 'master'))
		self.assertEqual(check_tag.parse_tag('v1.41.0.57-agent-alpha'), (1, 41, 0, 57, 'agent-alpha'))
		self.assertEqual(check_tag.parse_tag('v1.41.0.57-PE'), (1, 41, 0, 57, 'PE'))
		for bad in ('1.41.0.57-master', 'v1.41.0.57', 'v1.41-master', 'release-1'):
			self.assertIsNone(check_tag.parse_tag(bad), bad)

	@unittest.skipUnless(shutil.which('git'), 'needs git')
	def test_tag_scripts_in_a_clone(self):
		# A throwaway clone: tags made here never reach the real repository.
		with tempfile.TemporaryDirectory() as tmp:
			clone = Path(tmp, 'repo')
			subprocess.run(['git', 'clone', '-q', '--no-local', str(ROOT), str(clone)], check=True)
			for name, value in (('user.name', 'test'), ('user.email', 'test@example.com'),
			                    ('tag.gpgSign', 'false'), ('commit.gpgSign', 'false')):
				subprocess.run(['git', '-C', str(clone), 'config', name, value], check=True)
			# The clone has this checkout's version files, committed.
			for rel in ('tools/haumea/version.h', 'tools/release/gen_buildinfo.py', 'tools/release/check_tag.py',
			            'tools/release/tag_release.sh'):
				shutil.copy2(ROOT / rel, clone / rel)
			subprocess.run(['git', '-C', str(clone), 'add', '-A'], check=True)
			subprocess.run(['git', '-C', str(clone), 'commit', '-qm', 'versions', '--allow-empty'], check=True)
			env = dict(os.environ)
			env.pop('HAUMEA_BUILD', None)
			env.pop('HAUMEA_COMMIT', None)
			script = str(clone / 'tools/release/tag_release.sh')
			run = lambda *a: subprocess.run(a, cwd=clone, env=env, capture_output=True, text=True)
			count = run('git', 'rev-list', '--count', 'HEAD').stdout.strip()
			major, minor, patch = gen_buildinfo.base_version(clone)
			made = run('sh', script, '--create', 'agent-alpha')
			self.assertEqual(made.returncode, 0, made.stderr)
			tag = f'v{major}.{minor}.{patch}.{count}-agent-alpha'
			self.assertIn(tag, made.stdout)
			ok = run(sys.executable, str(clone / 'tools/release/check_tag.py'), tag)
			self.assertEqual(ok.returncode, 0, ok.stderr)
			# The same X.Y.Z again is refused: old installs would not see it.
			run('git', 'commit', '-qm', 'more', '--allow-empty')
			again = run('sh', script, 'agent-alpha')
			self.assertNotEqual(again.returncode, 0)
			self.assertIn('bump HAUMEA_VERSION_PATCH', again.stderr)
			# Another channel has its own tags.
			self.assertEqual(run('sh', script, 'PE').returncode, 0)
			# A wrong build number or version fails the CI check.
			wrong = f'v{major}.{minor}.{patch}.{int(count) + 5}-PE'
			run('git', 'tag', '-a', wrong, '-m', 'wrong')
			self.assertNotEqual(run(sys.executable, str(clone / 'tools/release/check_tag.py'), wrong).returncode, 0)
			wrong = f'v{major}.{minor + 1}.0.{int(count) + 1}-PE'
			run('git', 'tag', '-a', wrong, '-m', 'wrong')
			self.assertIn('version.h says',
			              run(sys.executable, str(clone / 'tools/release/check_tag.py'), wrong).stderr)
			# Never pushed: the clone's origin has none of these tags.
			self.assertNotIn('-agent-alpha', subprocess.run(['git', '-C', str(ROOT), 'tag', '--list', tag],
			                                                capture_output=True, text=True).stdout)


if __name__ == '__main__':
	unittest.main()
