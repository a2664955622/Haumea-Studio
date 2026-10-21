#!/usr/bin/env python3
"""Fail when a wheel in the wheelhouse isn't credited in THIRD_PARTY_NOTICES.md.

    check_notices.py <wheelhouse> <THIRD_PARTY_NOTICES.md>

The Namaka bundles ship these wheels, so each one's package must be named in
the notices, as its PyPI name in backticks (e.g. `pydantic-core`). Names
compare the PEP 503 way: case and -, _ and . don't matter.
"""
from pathlib import Path
import re
import sys


def normalise(name):
    return re.sub(r'[-_.]+', '-', name).lower()


wheelhouse, notices = Path(sys.argv[1]), Path(sys.argv[2])
credited = {normalise(n) for n in re.findall(r'`([A-Za-z0-9][A-Za-z0-9._-]*)`', notices.read_text(encoding='utf-8'))}
shipped = {normalise(w.name.split('-')[0]) for w in wheelhouse.glob('*.whl')}
missing = sorted(shipped - credited)
if missing:
    print('THIRD_PARTY_NOTICES.md does not credit: ' + ', '.join(missing), file=sys.stderr)
    sys.exit(1)
print(f'{len(shipped)} bundled packages, all credited')
