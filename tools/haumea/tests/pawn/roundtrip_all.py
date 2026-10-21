#!/usr/bin/env python3
"""roundtrip_all.py maps-dir work-dir [--update]: the Pawn exporter round trip.

The editor (HAUMEA_BINARY, with HAUMEA_GAME_DIR) opens and saves each map
(--pawn-roundtrip) into work-dir/v1, then saves that output again into
work-dir/v2. Both saves must match, and v1 must match expected/ (--update
copies v1 there instead). The maps are split among a few editors (TEST_JOBS,
default the CPUs/8, at least 1; for the real game no more than its machine-wide
slots), each loading the game once and doing both saves of its maps.
Results print in map order. Exit status 1 when any map fails.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import difflib
import os
import shutil
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from haumea_test_env import (EditorTimeout, FIXTURE_TIMEOUT, REAL_GAME_TIMEOUT, is_real_game,  # noqa: E402
                             real_game_slots, run_editor)


def jobs():
	try:
		return max(1, int(os.environ['TEST_JOBS']))
	except (KeyError, ValueError):
		return max(1, (os.cpu_count() or 8) // 8)


def diff(a, b, limit):
	lines = list(difflib.unified_diff(a.read_text(errors='replace').splitlines(),
	                                  b.read_text(errors='replace').splitlines(), lineterm='', n=0))
	return lines[2:2 + limit]


def run_chunk(maps, work, number):
	"""One editor for these maps: every v1 save, then every v2 save of a v1.
	Returns (log text, timed out)."""
	pairs = [(m, work / 'v1' / m.name) for m in maps] + [(work / 'v1' / m.name, work / 'v2' / m.name) for m in maps]
	# Its own config folder: editors running at once mustn't share settings files.
	config = work / f'config-{number}'
	config.mkdir(exist_ok=True)
	env = dict(os.environ, XDG_CONFIG_HOME=str(config), APPDATA=str(config))
	game = os.environ['HAUMEA_GAME_DIR']
	timeout = (REAL_GAME_TIMEOUT if is_real_game(game) else FIXTURE_TIMEOUT) + 10 * len(pairs)
	log = work / f'editor-{number}.log'
	try:
		with open(log, 'w') as out:
			run_editor([os.environ['HAUMEA_BINARY'], '--game', game, '--pawn-roundtrip',
			            *[str(p) for pair in pairs for p in pair]], stdout=out, env=env, timeout=timeout)
		return log.read_text(errors='replace'), None
	except EditorTimeout as error:
		return log.read_text(errors='replace'), str(error)


def check(map_path, work, update, log, timed_out):
	"""(ok, lines to print) for one map, after its editor ran."""
	name = map_path.name
	for dst in (work / 'v1' / name, work / 'v2' / name):
		if not dst.is_file():
			mine = [line for line in log.splitlines() if name in line]
			return False, [f'FAIL {name}: the editor wrote no output ({dst.parent.name})'] + \
				([timed_out] if timed_out else mine[-3:] + log.splitlines()[-5:])
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
	editors = jobs()
	if is_real_game(os.environ['HAUMEA_GAME_DIR']):
		editors = min(editors, real_game_slots())
	editors = max(1, min(editors, len(maps)))
	chunks = [maps[i::editors] for i in range(editors)]
	with ThreadPoolExecutor(editors) as pool:
		logs = list(pool.map(lambda n: run_chunk(chunks[n], work, n), range(editors)))
	ran = {m: logs[n] for n in range(editors) for m in chunks[n]}
	results = [check(m, work, update, *ran[m]) for m in maps]
	for ok, lines in results:
		print('\n'.join(lines))
	return 0 if all(ok for ok, _ in results) else 1


if __name__ == '__main__':
	sys.exit(main())
