# SPDX-License-Identifier: GPL-2.0-or-later
"""missing_wheels.py <wheelhouse> <constraints.txt>: the pins this Python has no bundled wheel for.

Run with the Python being installed into (setup-agent): prints one
"name==version" per line for each pin in constraints.txt that no wheel in
the wheelhouse can satisfy on this interpreter, so only those are downloaded.
"""
import sys
from pathlib import Path

from pip._vendor.packaging.tags import sys_tags
from pip._vendor.packaging.utils import canonicalize_name, parse_wheel_filename

wheelhouse, constraints = Path(sys.argv[1]), Path(sys.argv[2])
supported = set(sys_tags())
have = set()
for wheel in wheelhouse.glob('*.whl'):
    try:
        name, version, _, tags = parse_wheel_filename(wheel.name)
    except Exception:
        continue
    if tags & supported:
        have.add((name, str(version)))
for line in constraints.read_text().splitlines():
    line = line.split('#')[0].strip()
    if '==' not in line:
        continue
    name, version = line.split('==', 1)
    if (canonicalize_name(name), version.split(';')[0].strip()) not in have:
        print(f'{name.strip()}=={version.strip()}')
