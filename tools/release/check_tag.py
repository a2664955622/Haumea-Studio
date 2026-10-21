#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""check_tag.py TAG: a release tag must match the source it points at.

Release tags are v1.0.0.<build>-<channel> (versionutil.h reads them the same
way), the channel production or beta: MAJOR.MINOR.PATCH must be version.h's,
and <build> the commit count of the tagged commit (`git rev-list --count TAG`). CI runs this on every pushed
v* tag; tag_release.sh makes the tags. Exit status 0 when the tag is right.
"""
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_buildinfo  # noqa: E402

CHANNELS = ('production', 'beta')
TAG = re.compile(r'v(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?-(.+)')
OLD_COMMIT = re.compile(r'-[0-9a-fA-F]{7,40}$')


def parse_tag(tag):
	"""(major, minor, patch, build or None, channel), or None for other tags.
	An old tag's trailing -<sha> is not part of its channel."""
	m = TAG.fullmatch(tag)
	if not m:
		return None
	major, minor, patch, build, rest = m.groups()
	channel = OLD_COMMIT.sub('', rest) if '-' in rest else rest
	if not channel:
		return None
	return int(major), int(minor), int(patch), None if build is None else int(build), channel


def git(*args):
	return subprocess.run(['git', '-C', str(gen_buildinfo.ROOT), *args], capture_output=True, text=True,
	                      check=True).stdout.strip()


def check(tag):
	parsed = parse_tag(tag)
	if parsed is None:
		return f'{tag}: not a release tag (v1.0.0.<build>-<channel>)'
	major, minor, patch, build, channel = parsed
	if channel not in CHANNELS:
		return f'{tag}: the channel is production or beta, not {channel}'
	want = gen_buildinfo.base_version()
	if (major, minor, patch) != want:
		return f'{tag}: version.h says {".".join(map(str, want))}'
	if build is None:
		return f'{tag}: no build number (v{major}.{minor}.{patch}.<build>-{channel})'
	count = int(git('rev-list', '--count', tag))
	if build != count:
		return f'{tag}: the tagged commit is build {count}, not {build}'
	return None


def main(argv):
	if len(argv) != 2:
		print(__doc__.split('\n\n')[0], file=sys.stderr)
		return 2
	problem = check(argv[1])
	if problem:
		print(problem, file=sys.stderr)
		return 1
	print(f'{argv[1]}: matches version.h and its commit')
	return 0


if __name__ == '__main__':
	sys.exit(main(sys.argv))
