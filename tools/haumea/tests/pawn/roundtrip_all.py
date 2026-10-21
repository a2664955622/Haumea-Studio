#!/usr/bin/env python3
"""roundtrip_all.py maps-dir work-dir [--update]: the Pawn exporter round trip.

The editor (HAUMEA_BINARY, with HAUMEA_GAME_DIR) opens and saves each map
(--pawn-roundtrip) into work-dir/v1, then saves that output again into
work-dir/v2. Both saves must match, and v1 must match expected/ (--update
copies v1 there instead). Maps run several at once (TEST_JOBS, default the
CPUs/8, at least 1); results print in map order. Exit status 1 when any map fails.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import difflib
import os
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent


def jobs():
	try:
		return max(1, int(os.environ['TEST_JOBS']))
	except (KeyError, ValueError):
		return max(1, (os.cpu_count() or 8) // 8)


def diff(a, b, limit):
	lines = list(difflib.unified_diff(a.read_text(errors='replace').splitlines(),
	                                  b.read_text(errors='replace').splitlines(), lineterm='', n=0))
	return lines[2:2 + limit]


def roundtrip(map_path, work, update):
	"""(ok, lines to print) for one map."""
	name = map_path.name
	for src, dst in ((map_path, work / 'v1' / name), (work / 'v1' / name, work / 'v2' / name)):
		log = work / f'{dst.parent.name}-{name}.log'
		# Its own config folder: editors running at once mustn't share settings files.
		config = work / f'config-{dst.parent.name}-{name}'
		config.mkdir(exist_ok=True)
		env = dict(os.environ, XDG_CONFIG_HOME=str(config), APPDATA=str(config))
		with open(log, 'w') as out:
			subprocess.run([os.environ['HAUMEA_BINARY'], '--game', os.environ['HAUMEA_GAME_DIR'],
			                '--pawn-roundtrip', str(src), str(dst)], stdout=out, stderr=subprocess.STDOUT, env=env)
		if not dst.is_file():
			return False, [f'FAIL {name}: the editor wrote no output ({dst.parent.name})'] + \
				log.read_text(errors='replace').splitlines()[-5:]
	v1, v2, expected = work / 'v1' / name, work / 'v2' / name, HERE / 'expected' / name
	if v1.read_bytes() != v2.read_bytes():
		return False, [f'FAIL {name}: saving the saved map changes it'] + diff(v1, v2, 10)
	if update:
		shutil.copyfile(v1, expected)
		return True, [f'updated expected/{name}']
	if not expected.is_file() or v1.read_bytes() != expected.read_bytes():
		return False, [f'FAIL {name}: the output differs from pawn/expected (run with --update-expected if intended)'] + \
			(diff(expected, v1, 20) if expected.is_file() else [])
	return True, [f'ok   {name}']


def main():
	maps_dir, work = Path(sys.argv[1]), Path(sys.argv[2])
	update = '--update' in sys.argv[3:]
	(work / 'v1').mkdir(parents=True, exist_ok=True)
	(work / 'v2').mkdir(parents=True, exist_ok=True)
	maps = sorted(maps_dir.glob('*.pwn'))
	with ThreadPoolExecutor(jobs()) as pool:
		results = list(pool.map(lambda m: roundtrip(m, work, update), maps))
	for ok, lines in results:
		print('\n'.join(lines))
	return 0 if all(ok for ok, _ in results) else 1


if __name__ == '__main__':
	sys.exit(main())
