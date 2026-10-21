#!/usr/bin/env python3
"""Package an agent engine and Python wheel without local/game data."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import zipfile

p = argparse.ArgumentParser()
p.add_argument('--engine', required=True, type=Path)
p.add_argument('--wheel', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--name', required=True)
p.add_argument('--glfw', type=Path, help='macOS GLFW shared library to bundle')
p.add_argument('--wheelhouse', type=Path,
    help='prebuilt Python dependency wheels for an offline Agent Alpha setup')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
stage = a.output / a.name
stage.mkdir(exist_ok=False)
# The launchers run ./haumea, whatever the build artifact was called.
engine = stage / ('haumea.exe' if a.engine.suffix.lower() == '.exe' else 'haumea')
shutil.copy2(a.engine, engine)
shutil.copy2(a.wheel, stage / a.wheel.name)
shutil.copytree(root / 'fonts', stage / 'fonts', ignore=shutil.ignore_patterns('.DS_Store'))
# The starter prefabs, which the editor finds next to itself.
shutil.copytree(root / 'tools/haumea/prefabs', stage / 'prefabs', ignore=shutil.ignore_patterns('.DS_Store'))
shutil.copy2(root / 'tools/release/AGENT_README.md', stage / 'README.md')
setup_name = 'setup-agent.ps1' if engine.suffix.lower() == '.exe' else 'setup-agent.sh'
shutil.copy2(root / 'tools/release' / setup_name, stage / setup_name)
if engine.suffix.lower() != '.exe':
    shutil.copy2(root / 'tools/release/run-haumea-agent.sh', stage / 'run-haumea-agent.sh')
    shutil.copy2(root / 'tools/release/launch-haumea-agent.sh', stage / 'launch-haumea-agent.sh')
shutil.copy2(root / 'tools/haumea/minilzo/COPYING', stage / 'COPYING-LZO')
commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
# The editor and Agent Alpha are built from the same commit and version.
version_h = (root / 'tools/haumea/version.h').read_text()
version = re.search(r'#define\s+HAUMEA_VERSION\s+"([^"]+)"', version_h).group(1)
(stage / 'BUILD_INFO.json').write_text(json.dumps({'source_commit': commit,
    'project': 'Haumea', 'agent': 'Agent Alpha',
    'haumea_version': version,
    'agent_alpha_release': f'v{version}-agent-alpha',
    'platform': platform.platform(), 'python_wheel': a.wheel.name}, indent=2) + '\n')
if a.wheelhouse:
    shutil.copytree(a.wheelhouse, stage / 'wheelhouse',
        ignore=shutil.ignore_patterns('.DS_Store'))
    # setup-agent's helper: another Python downloads only its binary wheels.
    shutil.copy2(root / 'tools/release/missing_wheels.py', stage / 'missing_wheels.py')
if a.glfw:
    lib = stage / 'lib'; lib.mkdir()
    bundled = lib / 'libglfw.3.dylib'
    shutil.copy2(a.glfw.resolve(), bundled)
    license_file = a.glfw.resolve().parents[1] / 'LICENSE.md'
    if not license_file.exists(): raise SystemExit('GLFW LICENSE.md missing beside library prefix')
    shutil.copy2(license_file, stage / 'LICENSE-GLFW.md')
    deps = subprocess.check_output(['otool', '-L', str(engine)], text=True)
    glfw = next(line.strip().split(' (')[0] for line in deps.splitlines() if 'libglfw' in line)
    subprocess.run(['install_name_tool', '-change', glfw, '@executable_path/lib/libglfw.3.dylib', str(engine)], check=True)
    subprocess.run(['install_name_tool', '-id', '@loader_path/libglfw.3.dylib', str(bundled)], check=True)
    for f in [bundled, engine]:
        subprocess.run(['codesign', '--force', '--sign', '-', str(f)], check=True)
    deps = subprocess.check_output(['otool', '-L', str(engine)], text=True)
    if '/opt/homebrew' in deps or '/usr/local' in deps:
        raise SystemExit('Unbundled non-system dependency in engine')
files = sorted(f for f in stage.rglob('*') if f.is_file())
(stage / 'SHA256SUMS').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.relative_to(stage).as_posix()+'\n' for f in files))
archive = a.output / (a.name + '.zip')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sorted(stage.rglob('*')):
        if f.is_file(): z.write(f, f.relative_to(a.output))
print(archive)
