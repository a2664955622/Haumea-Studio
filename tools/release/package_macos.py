#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a macOS app and drag-to-Applications DMG from a complete portable stage.

Requires macOS's vtool, install_name_tool, codesign and hdiutil. Releases use
ad-hoc app signatures for Apple Silicon compatibility and an unsigned DMG.
Developer ID signing and notarization are not part of the release workflow.
"""
import argparse
from pathlib import Path
import plistlib
import re
import shutil
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]
BUNDLE_ID = 'io.github.itsneufox.haumea'


def write_icon(path):
    # Reuse the editor's generated RGBA mark; no extra image tools are needed.
    header = (ROOT / 'tools/haumea/haumea_icon.h').read_text()
    match = re.search(r'haumeaIcon128\[65536\] = \{(.*?)\};', header, re.S)
    pixels = bytes(int(n) for n in re.findall(r'\d+', match.group(1)))
    if len(pixels) != 128 * 128 * 4:
        raise ValueError('Invalid editor icon data')
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y * 512:(y + 1) * 512] for y in range(128))
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 128, 128, 8, 6, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
    path.write_bytes(b'icns' + struct.pack('>I', len(png) + 16) + b'ic07' + struct.pack('>I', len(png) + 8) + png)


def create_app(bundle, stage, version, minimum_os):
    if not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', version):
        raise ValueError('Expected a four-part Haumea version')
    if not (bundle / 'namaka/namaka_install.py').is_file():
        raise ValueError('The app must include Namaka')
    stage.mkdir(parents=True, exist_ok=False)
    app = stage / 'Haumea Studio.app'
    contents = app / 'Contents'
    macos = contents / 'MacOS'
    resources = contents / 'Resources'
    macos.mkdir(parents=True)
    shutil.copytree(bundle, resources)
    shutil.move(str(resources / 'haumea'), str(macos / 'haumea'))
    (macos / 'haumea').chmod(0o755)
    # The editor resolves resources beside its executable. Keep the data in
    # Resources and give those existing lookups relative links inside the app.
    for path in resources.iterdir():
        (macos / path.name).symlink_to('../Resources/' + path.name, target_is_directory=path.is_dir())
    write_icon(resources / 'haumea.icns')
    info = {
        'CFBundleName': 'Haumea Studio', 'CFBundleDisplayName': 'Haumea Studio',
        'CFBundleIdentifier': BUNDLE_ID, 'CFBundleExecutable': 'haumea',
        'CFBundlePackageType': 'APPL', 'CFBundleInfoDictionaryVersion': '6.0',
        'CFBundleShortVersionString': '.'.join(version.split('.')[:3]),
        'CFBundleVersion': version.split('.')[3], 'CFBundleIconFile': 'haumea.icns',
        'LSMinimumSystemVersion': minimum_os, 'NSHighResolutionCapable': True,
        # Finder launches do not read the user's shell startup files. These
        # standard Python locations let the Namaka panel find its interpreter.
        'LSEnvironment': {'PATH': '/opt/homebrew/opt/python@3.14/bin:/usr/local/opt/python@3.14/bin:'
                                '/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin'},
    }
    (contents / 'Info.plist').write_bytes(plistlib.dumps(info))
    (stage / 'Applications').symlink_to('/Applications', target_is_directory=True)
    (stage / 'START_HERE.txt').write_text(
        'HAUMEA STUDIO\n\nDrag Haumea Studio.app onto Applications, then open it from Applications.\n'
        'GLFW and Namaka are included. Choose your GTA San Andreas and server folders on first launch.\n'
        'For Namaka, open Windows > Namaka and install Python 3.10-3.14 when prompted.\n'
        'Python 3.14 supports offline Namaka setup.\n'
        'To uninstall, move Haumea Studio.app from Applications to the Trash. Maps and settings stay.\n')
    return app


def minimum_os(binary):
    output = subprocess.check_output(['vtool', '-show-build', str(binary)], text=True)
    versions = re.findall(r'\bminos\s+(\d+\.\d+(?:\.\d+)?)', output)
    if not versions:
        raise ValueError(f'Cannot determine the minimum macOS version of {binary}')
    return max(versions, key=lambda v: tuple(map(int, v.split('.'))))


def check_dependencies(binary, allowed):
    output = subprocess.check_output(['otool', '-L', str(binary)], text=True)
    for line in output.splitlines()[1:]:
        name = line.strip().split(' (', 1)[0]
        if name not in allowed and not name.startswith(('/usr/lib/', '/System/Library/')):
            raise ValueError(f'{binary} has an unbundled dependency: {name}')


def package(bundle, stage, output, version, glfw, identity='-'):
    glfw = glfw.resolve()
    required = max((minimum_os(bundle / 'haumea'), minimum_os(glfw)),
                   key=lambda v: tuple(map(int, v.split('.'))))
    app = create_app(bundle, stage, version, required)
    frameworks = app / 'Contents/Frameworks'
    frameworks.mkdir()
    library = frameworks / 'libglfw.3.dylib'
    shutil.copy2(glfw, library)
    library.chmod(0o755)
    shutil.copy2(glfw.parents[1] / 'LICENSE.md', app / 'Contents/Resources/LICENSE-GLFW.md')
    engine = app / 'Contents/MacOS/haumea'
    dependencies = subprocess.check_output(['otool', '-L', str(engine)], text=True)
    old = [line.strip().split(' (', 1)[0] for line in dependencies.splitlines()[1:] if 'libglfw' in line]
    if len(old) != 1:
        raise ValueError('Expected one dynamically linked GLFW library')
    relative = '@executable_path/../Frameworks/libglfw.3.dylib'
    subprocess.run(['install_name_tool', '-change', old[0], relative, str(engine)], check=True)
    subprocess.run(['install_name_tool', '-id', '@rpath/libglfw.3.dylib', str(library)], check=True)
    check_dependencies(engine, {relative})
    check_dependencies(library, {'@rpath/libglfw.3.dylib'})
    signing = ['--timestamp', '--options', 'runtime'] if identity != '-' else ['--timestamp=none']
    for path in (library, engine, app):
        subprocess.run(['codesign', '--force', '--sign', identity, *signing, str(path)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    output.mkdir(parents=True, exist_ok=True)
    dmg = output / 'Haumea-Studio-macOS-Apple-Silicon-Setup.dmg'
    subprocess.run(['hdiutil', 'create', '-volname', 'Haumea Studio', '-srcfolder', str(stage),
                    '-format', 'UDZO', '-ov', str(dmg)], check=True)
    if identity != '-':
        subprocess.run(['codesign', '--sign', identity, '--timestamp', '--identifier', BUNDLE_ID + '.dmg', str(dmg)], check=True)
    print(dmg)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--glfw', type=Path, required=True)
    parser.add_argument('--identity', default='-')
    package(**vars(parser.parse_args()))
