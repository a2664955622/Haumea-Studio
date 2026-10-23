#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Install this Linux bundle for the current user, or uninstall its owned files."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import sys

MARKER = '.haumea-install.json'


def desktop_quote(path):
    # Desktop Exec strings have their own escaping rules; they aren't shell commands.
    value = str(path).replace('\\', '\\\\\\\\').replace('"', '\\\\"')
    value = value.replace('`', '\\\\`').replace('$', '\\\\$').replace('%', '%%')
    return '"' + value + '"'


def install(source):
    data = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share').resolve()
    target = data / 'haumea-studio'
    launcher = Path.home() / '.local/bin/haumea'
    desktop = data / 'applications/haumea.desktop'
    if target.is_symlink():
        raise ValueError(f'{target} is a symbolic link. Choose a data directory without this link.')
    if target.exists() and not (target / MARKER).is_file():
        raise ValueError(f'{target} already exists and is not managed by this installer. Move it aside first.')
    if source == target:
        raise ValueError('Already installed here. Run haumea, or use --uninstall.')
    # Check collisions before copying anything; don't replace another app's launcher.
    previous = json.loads((target / MARKER).read_text()) if (target / MARKER).is_file() else {}
    for path in (launcher, desktop):
        if path.exists() or path.is_symlink():
            if (path.is_symlink() or not path.is_file() or
                    previous.get('integrations', {}).get(str(path)) != path.read_text()):
                raise ValueError(f'{path} already exists. Move it aside first.')
    if not (source / 'haumea').is_file():
        raise ValueError('Keep install-linux.py beside haumea in the extracted bundle.')
    files = sorted(p for p in source.rglob('*') if p.is_file() and not p.is_symlink())
    target.mkdir(parents=True, exist_ok=True)
    for path in files:
        rel = path.relative_to(source)
        dest = target / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dest)
    (target / 'haumea').chmod(0o755)
    launcher.parent.mkdir(parents=True, exist_ok=True)
    launcher.write_text('#!/bin/sh\nexec ' + shlex.quote(str(target / 'haumea')) + ' "$@"\n')
    launcher.chmod(0o755)
    desktop.parent.mkdir(parents=True, exist_ok=True)
    desktop.write_text('[Desktop Entry]\nType=Application\nName=Haumea Studio\n'
                       'Comment=The all-in-one editor for open.mp and SA-MP\n'
                       f'Exec={desktop_quote(target / "haumea")}\n'
                       f'Icon={target / "haumea.svg"}\nTerminal=false\n'
                       'Categories=Graphics;3DGraphics;Development;\nStartupWMClass=haumea\n')
    record = {'files': sorted(set(previous.get('files', [])) |
                              {p.relative_to(source).as_posix() for p in files}),
              'integrations': {str(p): p.read_text() for p in (launcher, desktop)}}
    (target / MARKER).write_text(json.dumps(record, indent=2) + '\n')
    print(f'Installed Haumea Studio in {target}\nOpen Haumea Studio from your applications menu.')
    print(f'To uninstall: python3 {shlex.quote(str(target / "install-linux.py"))} --uninstall')


def uninstall(source):
    marker = source / MARKER
    if not marker.is_file():
        raise ValueError('Run --uninstall with the installed copy of install-linux.py.')
    record = json.loads(marker.read_text())
    # Only remove files recorded by the installer. Maps or other added files stay.
    for name in record['files']:
        path = source / name
        if path.resolve().is_relative_to(source) and (path.is_file() or path.is_symlink()):
            path.unlink()
    for name, content in record['integrations'].items():
        path = Path(name)
        if path.is_file() and not path.is_symlink() and path.read_text() == content:
            path.unlink()
    marker.unlink()
    for path in sorted(source.rglob('*'), key=lambda p: len(p.parts), reverse=True):
        if path.is_dir() and not path.is_symlink():
            try:
                path.rmdir()
            except OSError:
                pass
    try:
        source.rmdir()
    except OSError:
        print(f'Additional files left in {source}.')
    print('Haumea Studio uninstalled. Your maps and settings have been kept.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--uninstall', action='store_true')
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    try:
        (uninstall if args.uninstall else install)(source)
    except (OSError, ValueError, KeyError) as error:
        print(f'Haumea Studio: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
