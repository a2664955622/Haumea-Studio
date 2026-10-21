# SPDX-License-Identifier: GPL-2.0-or-later
"""A material colour tints a retextured object as the SA-MP client does.

A beta tester's skatepark: cactus 754 retextured with 728's gtatreeshi
berrybush1 and the colour 0xFF00FF33 is dark green in game but was orange
in Haumea. The client, read from the 0.3.7-R5 samp.dll decompilation
(reSA-MP objectruntimehelpers.cpp):
- the colour arrives as open.mp writes it, ABGR (Shared/NetCode/object.hpp
  writeUINT32(materialColour.ABGR())), so in memory it is RwRGBA r, g, b, a:
  Pawn's 0xAARRGGBB is plain ARGB;
- SetObjectMaterialInObjectRuntime stores the texture and the colour, and
  ApplyObjectMaterialStateToLiveEntityCautious sets, for a non-zero colour,
  the material colour, rpGEOMETRYMODULATE on, rpGEOMETRYPRELIT off and the
  surface properties to 1, 0, 1; a colour of 0 leaves the model's own.
The object is then lit like any object, not like a building: gta-reversed
CEntity::SetupLighting and SetLightColoursForPedsCarsAndObjects, the
objects' ambient and the directional light, each at half strength.

So the coloured object's pixels are the texture times the colour, lit: green
(0x00FF33 keeps no red), never orange (the prelight, colour ignored), never
black (unlit), and no brighter than the texture times the colour. And
0xFF000000 is a colour too (not 0): opaque black, so a black object, as the
tester's map has it on 12 slots.

The editor runs in its own Xvfb with a private config, on the synthetic
game (fixture/make_game.py), whose 754 has a warm prelight, unless
HAUMEA_GAME_DIR is set.
"""
from pathlib import Path
import glob
import os
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / 'tools' / 'agent'))
from haumea_test_env import editor_env  # noqa: E402

BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
XVFB = shutil.which('xvfb-run')
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')

# High over the sea, so nothing of the world is in the picture.
MAP = '''#include <a_samp>
#include <streamer>

stock SkateparkMaterials()
{{
    new tmpobjid;
    tmpobjid = CreateObject(754, -3.0, 3000.0, 600.0, 0.0, 0.0, 0.0);
{plain}
    tmpobjid = CreateDynamicObject(754, 3.0, 3000.0, 600.0, 0.0, 0.0, 0.0, -1, 0, -1, 300.00, 300.00);
{dynamic}
}}
'''
PLAIN = '\tSetObjectMaterial(tmpobjid, 0, 728, "gtatreeshi", "berrybush1", 0xFF00FF33);'
DYNAMIC = '\tSetDynamicObjectMaterial(tmpobjid, 0, 728, "gtatreeshi", "berrybush1", 0xFF00FF33);'
BLACK = '\tSetObjectMaterial(tmpobjid, 0, 728, "gtatreeshi", "berrybush1", 0xFF000000);'
CAMERA = (0.0, 2988.0, 601.0, 0.0, 3000.0, 600.5, 50.0)


def read_png(path):
    """(width, height, rows of (r, g, b)) of an 8-bit RGB or RGBA PNG."""
    data = Path(path).read_bytes()
    pos, idat = 8, b''
    while pos < len(data):
        size, kind = struct.unpack('>I4s', data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + size]
        if kind == b'IHDR':
            width, height, depth, colour = struct.unpack('>IIBB', chunk[:10])
        elif kind == b'IDAT':
            idat += chunk
        pos += 12 + size
    assert depth == 8 and colour in (2, 6), (depth, colour)
    channels = 3 if colour == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + left) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + up) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 0xFF
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 0xFF
        rows.append([tuple(line[x * channels:x * channels + 3]) for x in range(width)])
        previous = line
    return width, height, rows


@unittest.skipUnless(BINARY and XVFB and sys.platform.startswith('linux'), 'needs the editor and xvfb-run on Linux')
class MaterialColourTests(unittest.TestCase):
    def setUp(self):
        # Short: AF_UNIX paths are limited to 108 bytes.
        self.tmp = Path(tempfile.mkdtemp(prefix='hmc-', dir='/tmp'))
        self.game = GAME_DIR
        if not self.game:
            self.game = str(self.tmp / 'game')
            subprocess.run([sys.executable, str(HERE / 'fixture' / 'make_game.py'), self.game],
                           check=True, capture_output=True)
        self.editor = None
        self.log = None

    def tearDown(self):
        if self.editor:
            try:
                os.killpg(self.editor.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            self.editor.wait()
        if self.log:
            self.log.close()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def client(self):
        from haumea_ipc import HaumeaClient
        work = self.tmp / 'editor'
        work.mkdir()
        env = editor_env(str(work), virtual_display=True)
        env['HAUMEA_TEST_XVFB'] = '1'
        socket = self.tmp / 'a.sock'
        self.log = open(work / 'out.txt', 'w')
        self.editor = subprocess.Popen([XVFB, '-a', '-s', '-screen 0 1280x800x24', BINARY, '--game', self.game,
                                        '--agent-socket', str(socket)],
                                       cwd=work, env=env, stdout=self.log, stderr=subprocess.STDOUT,
                                       start_new_session=True)
        client = HaumeaClient(socket, timeout=300)
        end = time.time() + 600
        while time.time() < end:
            try:
                if client.command('ping').get('ok'):
                    return client
            except Exception:
                pass
            time.sleep(1)
        self.fail('the editor never answered: ' + (work / 'out.txt').read_text()[-2000:])

    def capture(self, client, name, plain, dynamic):
        path = self.tmp / (name + '.pwn')
        path.write_text(MAP.format(plain=plain, dynamic=dynamic))
        self.assertTrue(client.command('map_open', str(path), 1).get('ok'))
        client.command('camera', *CAMERA)
        time.sleep(3)
        shot = self.tmp / (name + '.png')
        self.assertTrue(client.command('capture', str(shot)).get('ok'))
        return read_png(shot)

    def test_colour_tints_the_retextured_object_green(self):
        client = self.client()
        width, height, plain = self.capture(client, 'plain', '', '')
        _, _, coloured = self.capture(client, 'coloured', PLAIN, DYNAMIC)
        for side, x0, x1 in (('CreateObject', 0, width // 2), ('CreateDynamicObject', width // 2, width)):
            # The object's pixels: those the material changed.
            changed = [coloured[y][x] for y in range(0, height, 2) for x in range(x0, x1, 2)
                       if sum(abs(a - b) for a, b in zip(plain[y][x], coloured[y][x])) > 24]
            self.assertGreater(len(changed), 200, side + ': the material changed nothing')
            green = [p for p in changed if p[1] > 30 and p[1] > 2 * p[0] and p[1] > 1.5 * p[2]]
            self.assertGreater(len(green), 0.8 * len(changed),
                               '%s: %d of %d pixels green, e.g. %s' % (side, len(green), len(changed),
                                                                       changed[len(changed) // 2]))
            if not GAME_DIR:
                # berrybush1 is (110, 120, 70): times 0x00FF33, lit at most fully.
                brightest = sorted(p[1] for p in green)[int(len(green) * 0.95)]
                self.assertLessEqual(brightest, 125, side + ': lit brighter than the texture')

        # 0xFF000000: alpha FF, no red, green or blue. Non-zero, so applied.
        _, _, black = self.capture(client, 'black', BLACK, '')
        changed = [black[y][x] for y in range(0, height, 2) for x in range(0, width // 2, 2)
                   if sum(abs(a - b) for a, b in zip(plain[y][x], black[y][x])) > 24]
        self.assertGreater(len(changed), 200, '0xFF000000 changed nothing')
        dark = [p for p in changed if max(p) <= 24]
        self.assertGreater(len(dark), 0.8 * len(changed),
                           '0xFF000000: %d of %d pixels black, e.g. %s' % (len(dark), len(changed),
                                                                         changed[len(changed) // 2]))


if __name__ == '__main__':
    unittest.main()
