#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""gen_buildinfo.py [--header OUT.h] [--python OUT.py] [--print]: the build's
number and commit, as open.mp numbers its builds.

The version is MAJOR.MINOR.PATCH from tools/haumea/version.h plus a build
number: the commit count (`git rev-list --count HEAD`, so it only goes up as
long as main is never squashed again). The commit is the short hash, with
"-dirty" when tracked files have changes. HAUMEA_BUILD and HAUMEA_COMMIT in
the environment override both (builds from a source archive); without git
they are 0 and "unknown".

--header writes the C header the editor includes (haumea_buildinfo.h),
--python the agent wheel's _version.py (PEP 440: 1.41.0.57, a dirty tree
1.41.0.57+dirty). Files are only rewritten when their content changes, so a
build that didn't change recompiles nothing. --print prints
"1.41.0.57 abc1234".
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def base_version(root=ROOT):
	text = (root / 'tools' / 'haumea' / 'version.h').read_text()
	parts = []
	for name in ('MAJOR', 'MINOR', 'PATCH'):
		m = re.search(rf'#define\s+HAUMEA_VERSION_{name}\s+(\d+)', text)
		if not m:
			raise SystemExit(f'version.h has no HAUMEA_VERSION_{name}')
		parts.append(int(m.group(1)))
	return tuple(parts)


def git(*args, root=ROOT):
	try:
		return subprocess.run(['git', '-C', str(root), *args], capture_output=True, text=True,
		                      check=True).stdout.strip()
	except (OSError, subprocess.CalledProcessError):
		return None


def build_info(root=ROOT):
	"""(build number, commit) from the environment, else git, else (0, "unknown")."""
	build, commit = os.environ.get('HAUMEA_BUILD'), os.environ.get('HAUMEA_COMMIT')
	if build is None:
		count = git('rev-list', '--count', 'HEAD', root=root)
		build = count if count and count.isdigit() else '0'
	if commit is None:
		commit = git('rev-parse', '--short=7', 'HEAD', root=root) or 'unknown'
		if commit != 'unknown' and git('status', '--porcelain', '--untracked-files=no', root=root):
			commit += '-dirty'
	if not str(build).isdigit():
		raise SystemExit(f'HAUMEA_BUILD must be a number, not {build!r}')
	if not re.fullmatch(r'[0-9A-Za-z._-]{1,40}', commit):
		raise SystemExit(f'HAUMEA_COMMIT has odd characters: {commit!r}')
	return int(build), commit


def write_if_changed(path, text):
	path = Path(path)
	if path.exists() and path.read_text() == text:
		return False
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(text)
	return True


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
	ap.add_argument('--header')
	ap.add_argument('--python')
	ap.add_argument('--print', action='store_true')
	a = ap.parse_args(argv)
	major, minor, patch = base_version()
	build, commit = build_info()
	full = f'{major}.{minor}.{patch}.{build}'
	if a.header:
		# Plain defines and a guard: haumea.rc includes it too.
		write_if_changed(a.header, '// Written by tools/release/gen_buildinfo.py; not in git.\n'
			'#ifndef HAUMEA_BUILDINFO_H\n#define HAUMEA_BUILDINFO_H\n'
			f'#define HAUMEA_BUILD {build}\n'
			f'#define HAUMEA_COMMIT "{commit}"\n'
			f'#define HAUMEA_FULL_VERSION "{full}"\n'
			f'#define HAUMEA_FILEVERSION {major},{minor},{patch},{build}\n'
			'#endif\n')
	if a.python:
		local = '+dirty' if commit.endswith('-dirty') else ''
		write_if_changed(a.python, '# The agent wheel\'s version: the editor\'s, with its build number.\n'
			'# Builds write it with tools/release/gen_buildinfo.py --python; the\n'
			'# committed file says build 0.\n'
			f'__version__ = "{full}{local}"\n')
	if a.print or not (a.header or a.python):
		print(full, commit)
	return 0


if __name__ == '__main__':
	sys.exit(main())
