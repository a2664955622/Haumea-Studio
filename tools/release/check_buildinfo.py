#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""check_buildinfo.py BINARY: the editor must carry this checkout's commit.

A build that never wrote haumea_buildinfo.h still compiles (versionutil.cpp
falls back to build 0 of an unknown commit), and the updater then calls every
release out of date. CI runs this on each editor it builds: the commit
gen_buildinfo.py reports must be a real one and appear in the binary. Exit
status 0 when it does.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_buildinfo  # noqa: E402


def main(argv=None):
	argv = sys.argv[1:] if argv is None else argv
	if len(argv) != 1:
		raise SystemExit('usage: check_buildinfo.py BINARY')
	build, commit = gen_buildinfo.build_info()
	if commit == 'unknown' or build == 0:
		raise SystemExit(f'no build number or commit here (build {build}, commit {commit})')
	if commit.encode() + b'\0' not in Path(argv[0]).read_bytes():
		raise SystemExit(f'{argv[0]} does not carry commit {commit}: '
		                 'haumea_buildinfo.h was not written before versionutil.cpp compiled')
	print(f'{argv[0]}: build {build}, commit {commit}')
	return 0


if __name__ == '__main__':
	sys.exit(main())
