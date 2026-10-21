#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare a librw checkout for Haumea.

Restores the small skeleton API that Haumea's settings panel uses, and
replaces librw's Dear ImGui with the docking build of the same release from
vendor/imgui-docking, so editor panels can be docked.
"""
from pathlib import Path
import argparse
import re
import shutil


parser = argparse.ArgumentParser()
parser.add_argument('librw', type=Path, help='librw checkout root')
root = parser.parse_args().librw.resolve()
header_path = root / 'skeleton' / 'skeleton.h'
source_path = root / 'skeleton' / 'skeleton.cpp'

header = header_path.read_text()
declaration = 'extern uint32 requestedMultiSamplingLevels;'
if declaration not in header:
    anchor = 'extern Globals globals;'
    if anchor not in header:
        raise SystemExit(f'Cannot find skeleton API insertion point in {header_path}')
    header_path.write_text(header.replace(anchor, f'{anchor}\n{declaration}', 1))

source = source_path.read_text()
definition = 'uint32 requestedMultiSamplingLevels = 1;'
if not re.search(r'\brequestedMultiSamplingLevels\s*=', source):
    anchor = 'Globals globals;'
    if anchor not in source:
        raise SystemExit(f'Cannot find skeleton state insertion point in {source_path}')
    source_path.write_text(source.replace(anchor, f'{anchor}\n{definition}', 1))

print('Applied Haumea multisampling compatibility to librw.')

# Only Dear ImGui's own files are replaced; librw's backend (imgui_impl_rw)
# and ImGuizmo stay as they are.
vendor = Path(__file__).resolve().parents[2] / 'vendor' / 'imgui-docking'
imgui_dir = root / 'skeleton' / 'imgui'
if not (imgui_dir / 'imgui.h').exists():
    raise SystemExit(f'Cannot find Dear ImGui in {imgui_dir}')
for source in sorted(vendor.iterdir()):
    if source.suffix in ('.h', '.cpp'):
        shutil.copyfile(source, imgui_dir / source.name)
# The Win32 and DX9 backends for the Windows D3D9 build (librw has none).
backends = imgui_dir / 'backends'
backends.mkdir(exist_ok=True)
for source in sorted((vendor / 'backends').iterdir()):
    if source.suffix in ('.h', '.cpp'):
        shutil.copyfile(source, backends / source.name)
print('Installed the Dear ImGui docking branch into librw.')

# Upstream librw only knows Windows and Linux. Add the macOS platforms
# Haumea's own premake5.lua builds (Homebrew GLFW), and make arm64 builds
# ARM64 rather than 32-bit ARM.
premake_path = root / 'premake5.lua'
premake = premake_path.read_text()
if 'macos-arm64-gl3' not in premake:
    edits = [
        ('\tfilter {}\n\n\tfilter "configurations:Debug"',
         '\tfilter { "system:macosx" }\n'
         '\t\tplatforms { "macos-amd64-null", "macos-amd64-gl3",\n'
         '\t\t\t"macos-arm64-null", "macos-arm64-gl3" }\n'
         '\tfilter {}\n\n\tfilter "configurations:Debug"'),
        ('\tfilter { "platforms:*arm*" }\n\t\tarchitecture "ARM"\n',
         '\tfilter { "platforms:*arm*" }\n\t\tarchitecture "ARM"\n'
         '\tfilter { "platforms:*arm64*" }\n\t\tarchitecture "ARM64"\n'),
        ('\tfilter { "platforms:linux*" }\n\t\tsystem "linux"\n',
         '\tfilter { "platforms:linux*" }\n\t\tsystem "linux"\n'
         '\tfilter { "platforms:macos*" }\n\t\tsystem "macosx"\n'
         '\tfilter { "platforms:macos*gl3" }\n'
         '\t\tincludedirs { "/opt/homebrew/include", "/usr/local/include" }\n'),
    ]
    for anchor, replacement in edits:
        if premake.count(anchor) != 1:
            raise SystemExit(f'Cannot find macOS insertion point in {premake_path}')
        premake = premake.replace(anchor, replacement, 1)
    premake_path.write_text(premake)
print('Added macOS platforms to librw.')
