#!/usr/bin/env python3
"""Package the Windows Agent Alpha companion, optionally with the editor and dependency wheels."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile


parser = argparse.ArgumentParser()
parser.add_argument('--wheel', required=True, type=Path,
                    help='built haumea_agent wheel')
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--name', default='Haumea-Agent-Alpha-windows-x86_64')
parser.add_argument('--engine', type=Path,
                    help='haumea.exe to include (e.g. from build-windows-mingw.sh)')
parser.add_argument('--wheelhouse', type=Path,
                    help='Windows dependency wheels for an offline setup (fetch_wheelhouse.sh windows)')
args = parser.parse_args()

root = Path(__file__).resolve().parents[2]
wheel = args.wheel.resolve()
if not wheel.is_file() or not wheel.name.startswith('haumea_agent-') or wheel.suffix != '.whl':
    raise SystemExit('--wheel must name a built haumea_agent .whl file')

args.output.mkdir(parents=True, exist_ok=True)
stage = args.output / args.name
archive = args.output / f'{args.name}.zip'
if stage.exists():
    shutil.rmtree(stage)
if archive.exists():
    archive.unlink()
stage.mkdir()

shutil.copy2(wheel, stage / wheel.name)
shutil.copy2(root / 'tools/release/setup-agent.ps1', stage / 'setup-agent.ps1')
shutil.copy2(root / 'tools/release/AGENT_ALPHA_WINDOWS_README.md', stage / 'README.md')
if args.engine:
    shutil.copy2(args.engine, stage / 'haumea.exe')
    # What the editor finds next to itself, as in the plain Windows bundle.
    shutil.copytree(root / 'fonts', stage / 'fonts', ignore=shutil.ignore_patterns('.DS_Store'))
    shutil.copytree(root / 'tools/haumea/prefabs', stage / 'prefabs', ignore=shutil.ignore_patterns('.DS_Store'))
    shutil.copy2(root / 'tools/haumea/minilzo/COPYING', stage / 'COPYING-LZO')
if args.wheelhouse:
    shutil.copytree(args.wheelhouse, stage / 'wheelhouse', ignore=shutil.ignore_patterns('.DS_Store'))
commit = subprocess.check_output(
    ['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
(stage / 'BUILD_INFO.json').write_text(json.dumps({
    'project': 'Haumea Agent Alpha',
    'source_commit': commit,
    'target': 'windows-x86_64',
    'editor_included': bool(args.engine),
    'python_wheel': wheel.name,
    'dependency_wheelhouse_included': bool(args.wheelhouse),
}, indent=2) + '\n')

files = sorted(path for path in stage.rglob('*') if path.is_file())
(stage / 'SHA256SUMS').write_text(''.join(
    f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(stage).as_posix()}\n'
    for path in files))
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            bundle.write(path, path.relative_to(args.output))
print(archive)
