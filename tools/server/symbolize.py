#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""symbolize.py REPORT.json SYMBOLS [--any-build]: a crash report's stack
with function names and source lines.

SYMBOLS is the release's haumea-symbols-<version>.zip (or the folder it
unpacks to). Needs addr2line for Linux builds and llvm-symbolizer for
Windows (PDB) and macOS (dSYM) builds. The report's build_id must be the one
the zip's BUILD_IDS.txt lists for its platform, or the names would be another
binary's; --any-build symbolizes anyway.
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import symbols  # noqa: E402


def main(argv):
	any_build = '--any-build' in argv
	argv = [a for a in argv if a != '--any-build']
	if len(argv) != 3:
		print(__doc__.strip(), file=sys.stderr)
		return 2
	report = json.loads(Path(argv[1]).read_text())
	source = Path(argv[2])
	with tempfile.TemporaryDirectory() as tmp:
		folder = source
		if source.is_file():
			folder = Path(tmp) / 'symbols'
			symbols.unpack(source, folder)
		if not any_build and not symbols.build_id_matches(folder, report['os'], report.get('build_id')):
			print(f"build_id {report.get('build_id') or '(none)'} is not this release's "
			      f"{symbols.PLATFORM[report['os']]} build: --any-build to symbolize anyway", file=sys.stderr)
			return 1
		debug = symbols.debug_file(folder, report['os'])
		if not debug:
			print(f'no {symbols.PLATFORM[report["os"]]} symbols in {source}', file=sys.stderr)
			return 1
		own = [i for i, f in enumerate(report['frames']) if f['module'] in symbols.OWN]
		resolved = dict(zip(own, symbols.run_symbolizer(debug, report['os'],
		                                                [report['frames'][i]['offset'] for i in own])))
	reason = report.get('crash', {}).get('signal') or report.get('crash', {}).get('code') or \
		report.get('error', {}).get('where', '')
	print(f"{report['kind']} {reason} in Haumea Studio {report['version']} ({report.get('commit', '?')}), "
	      f"{report['os']} {report.get('os_version', '')}")
	for i, f in enumerate(report['frames']):
		fn, where = resolved.get(i, (f.get('symbol') or '??', ''))
		print(f"#{i:<3} {f['module']:<18} {f['offset']:<12} {fn}" + (f'  {where}' if where else ''))
	return 0


if __name__ == '__main__':
	sys.exit(main(sys.argv))
