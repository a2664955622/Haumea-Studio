#!/usr/bin/env python3
"""Print CHANGELOG.md's section for a version: the release's "What's new".

    changelog_section.py <X.Y.Z> [CHANGELOG.md]

The section starts at the "## X.Y.Z" heading and ends before the next "## ".
With no section for that version, prints nothing and exits 1.
"""
from pathlib import Path
import sys

version = sys.argv[1]
path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parents[2] / 'CHANGELOG.md'
out, inside = [], False
for line in path.read_text(encoding='utf-8').splitlines():
    if line.startswith('## '):
        if inside:
            break
        inside = line[3:].split()[0].rstrip(':,') == version
        continue
    if inside:
        out.append(line)
text = '\n'.join(out).strip()
if not text:
    raise SystemExit(1)
print(text)
