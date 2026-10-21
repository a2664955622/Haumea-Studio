#!/usr/bin/env python3
"""The Windows build, checked on Linux: cross-compile it with MinGW-w64, run
it under Wine and compare it with the Linux build.

    windows_check.py [--gfx gl3,d3d9] [--no-build] [--no-capture]

For each graphics backend: tools/release/build-windows-mingw.sh builds
haumea.exe, then every map in fixtures/ and pawn/maps/ is opened and saved
(--pawn-roundtrip) by the Linux editor and by the exe under Wine, and the
two outputs must be identical byte for byte. A capture_screen smoke check
then opens a map in the exe and grabs a frame with the editor's panels over
the agent's TCP bridge; it must be a real image, not a blank one.

Wine runs in a fresh prefix of its own, with HAUMEA_CONFIG_DIR pointing at
a private config folder (Wine sets APPDATA itself), so nothing of the
user's Wine setup or Haumea settings is touched.

The game folder is $HAUMEA_GAME_DIR, else a synthetic one from
fixture/make_game.py (no GTA files, as in CI). The Linux editor is
$HAUMEA_BINARY, else bin/*/Release/haumea. Exits 0 when everything
matches, 1 on a difference or failure, 2 when MinGW, Wine or the Linux
editor is missing (with the reason).
"""
from pathlib import Path
import argparse
import glob
import os
import secrets
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MAPS = sorted(HERE.glob('fixtures/*.pwn')) + sorted(HERE.glob('pawn/maps/*.pwn'))


def winpath(path):
    """A Unix path as Wine's Z: drive sees it."""
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')


def skip(reason):
    print('skipped: ' + reason)
    sys.exit(2)


def run(args, env=None, timeout=600, cwd=None):
    return subprocess.run(args, env=env, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def png_is_real_image(path):
    """True when the PNG decodes to more than a handful of distinct colours."""
    data = Path(path).read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        return False
    pos, idat, width, height, colour = 8, b'', 0, 0, 0
    while pos < len(data):
        size, kind = struct.unpack('>I4s', data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + size]
        if kind == b'IHDR':
            width, height, _, colour = struct.unpack('>IIBB', chunk[:10])
        elif kind == b'IDAT':
            idat += chunk
        pos += 12 + size
    channels = {2: 3, 6: 4}.get(colour)
    if not channels or width < 64 or height < 64:
        return False
    raw = zlib.decompress(idat)
    stride = width * channels + 1
    colours = set()
    # Rows spread over the image, ignoring PNG filters: varied bytes are enough.
    for row in range(0, height, max(1, height // 40)):
        line = raw[row * stride + 1:(row + 1) * stride]
        for x in range(0, len(line) - channels, channels * max(1, width // 40)):
            colours.add(line[x:x + 3])
    return len(colours) > 20


class WindowsCheck:
    def __init__(self, args):
        self.args = args
        self.work = Path(tempfile.mkdtemp(prefix='haumea-windows-'))
        self.failures = 0

    def fail(self, message):
        print('FAIL ' + message)
        self.failures += 1

    def setup(self):
        for tool in ('x86_64-w64-mingw32-g++', 'x86_64-w64-mingw32-windres', 'wine', 'wineserver'):
            if not shutil.which(tool):
                skip(tool + ' is not installed (MinGW-w64 and Wine are needed)')
        self.linux = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
            glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
        if not self.linux:
            skip('no Linux editor to compare with (build it, or set HAUMEA_BINARY)')
        self.game = os.environ.get('HAUMEA_GAME_DIR')
        if not self.game or not Path(self.game).is_dir():
            self.game = str(self.work / 'game')
            subprocess.run([sys.executable, str(HERE / 'fixture' / 'make_game.py'), self.game],
                           check=True, capture_output=True)
            print('== synthetic game folder (no GTA files)')
        # A fresh Wine prefix: nothing of the user's Wine setup is used or touched.
        self.prefix = self.work / 'wineprefix'
        self.wine_env = dict(os.environ, WINEPREFIX=str(self.prefix), WINEDEBUG='-all',
                             WINEDLLOVERRIDES='mscoree,mshtml=')
        result = run(['wineboot', '-i'], env=self.wine_env, timeout=600)
        if result.returncode != 0:
            skip('wineboot failed: ' + result.stderr[-500:])
        print('== Wine ' + run(['wine', '--version']).stdout.strip() + ', fresh prefix')

    def build(self, gfx):
        exe = self.work / 'build' / ('win-amd64-' + gfx) / 'haumea.exe'
        if self.args.no_build:
            prebuilt = os.environ.get('HAUMEA_WINDOWS_EXE_' + gfx.upper())
            return Path(prebuilt) if prebuilt else None
        env = dict(os.environ, WORK=str(self.work / 'build'))
        result = run([str(ROOT / 'tools' / 'release' / 'build-windows-mingw.sh'), gfx], env=env, timeout=3600)
        if result.returncode != 0 or not exe.is_file():
            self.fail('%s: the MinGW build failed\n%s' % (gfx, (result.stdout + result.stderr)[-3000:]))
            return None
        print('ok   %s: built %s (%d bytes)' % (gfx, exe.name, exe.stat().st_size))
        return exe

    def roundtrips(self, gfx, exe):
        for source in MAPS:
            name = source.stem
            # Same file name in both: the exporter names its functions after it.
            out = self.work / gfx / name
            (out / 'linux').mkdir(parents=True)
            (out / 'windows').mkdir()
            config = out / 'config'
            config.mkdir()
            linux_env = dict(os.environ, HAUMEA_CONFIG_DIR=str(config / 'linux'), HAUMEA_NO_MIGRATE='1')
            run([self.linux, '--game', self.game, '--pawn-roundtrip', str(source),
                 str(out / 'linux' / 'map.pwn')], env=linux_env)
            wine_env = dict(self.wine_env, HAUMEA_CONFIG_DIR=winpath(config / 'windows'))
            run(['wine', str(exe), '--game', winpath(self.game), '--pawn-roundtrip',
                 winpath(source), winpath(out / 'windows' / 'map.pwn')], env=wine_env, timeout=300)
            linux_file, windows_file = out / 'linux' / 'map.pwn', out / 'windows' / 'map.pwn'
            if not linux_file.is_file():
                self.fail('%s %s: the Linux editor wrote nothing' % (gfx, name))
            elif not windows_file.is_file():
                self.fail('%s %s: the Windows editor wrote nothing' % (gfx, name))
            elif linux_file.read_bytes() != windows_file.read_bytes():
                diff = run(['diff', str(linux_file), str(windows_file)]).stdout
                self.fail('%s %s: Windows output differs from Linux\n%s' % (gfx, name, diff[:1500]))
            else:
                print('ok   %s %s: identical (%d bytes)' % (gfx, name, linux_file.stat().st_size))

    def capture(self, gfx, exe):
        # The editor with a map open, over the agent's authenticated TCP bridge.
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            port = s.getsockname()[1]
        token = secrets.token_hex(24)
        shot = self.work / gfx / 'screen.png'
        config = self.work / gfx / 'capture-config'
        config.mkdir(parents=True, exist_ok=True)
        env = dict(self.wine_env, HAUMEA_CONFIG_DIR=winpath(config), HAUMEA_ENGINE_TCP_PORT=str(port),
                   HAUMEA_ENGINE_TOKEN=token)
        editor = subprocess.Popen(['wine', str(exe), '--game', winpath(self.game), '--open', winpath(MAPS[0])],
                                  env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sys.path.insert(0, str(ROOT / 'tools'))
        from agent.haumea_ipc import HaumeaClient  # noqa: E402
        os.environ['HAUMEA_ENGINE_TCP_PORT'] = str(port)
        os.environ['HAUMEA_ENGINE_TOKEN'] = token
        try:
            client = HaumeaClient(timeout=30.0)
            deadline = time.time() + 180
            while True:
                try:
                    status = client.command('map_status')
                    if status.get('map', {}).get('path'):
                        break
                except Exception:
                    pass
                if time.time() > deadline or editor.poll() is not None:
                    self.fail('%s capture: the editor did not come up with the map' % gfx)
                    return
                time.sleep(2)
            client.command('capture_screen', winpath(shot))
            for _ in range(30):	# written on the next frame
                if shot.is_file() and shot.stat().st_size > 0:
                    break
                time.sleep(1)
            if not shot.is_file():
                self.fail('%s capture: no image was written' % gfx)
            elif not png_is_real_image(shot):
                self.fail('%s capture: the image is blank or unreadable (%s)' % (gfx, shot))
            else:
                print('ok   %s capture: %s (%d bytes)' % (gfx, shot.name, shot.stat().st_size))
        finally:
            os.environ.pop('HAUMEA_ENGINE_TCP_PORT', None)
            os.environ.pop('HAUMEA_ENGINE_TOKEN', None)
            # Only this prefix's processes: it is ours.
            run(['wineserver', '-k'], env=self.wine_env, timeout=60)
            editor.wait(timeout=60)

    def main(self):
        try:
            self.setup()
            for gfx in self.args.gfx.split(','):
                print('== ' + gfx)
                exe = self.build(gfx)
                if exe is None:
                    if self.args.no_build:
                        print('skipped %s: --no-build and no HAUMEA_WINDOWS_EXE_%s' % (gfx, gfx.upper()))
                    continue
                self.roundtrips(gfx, exe)
                if not self.args.no_capture:
                    self.capture(gfx, exe)
        finally:
            run(['wineserver', '-k'], env=getattr(self, 'wine_env', None), timeout=60) \
                if hasattr(self, 'wine_env') else None
            if self.failures == 0 and not self.args.keep:
                shutil.rmtree(self.work, ignore_errors=True)
            else:
                print('work folder kept: %s' % self.work)
        print('Windows check: %s' % ('passed' if self.failures == 0 else '%d failure(s)' % self.failures))
        return 0 if self.failures == 0 else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--gfx', default='gl3,d3d9')
    parser.add_argument('--no-build', action='store_true',
                        help='use HAUMEA_WINDOWS_EXE_GL3 / _D3D9 instead of building')
    parser.add_argument('--no-capture', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the work folder')
    sys.exit(WindowsCheck(parser.parse_args()).main())
