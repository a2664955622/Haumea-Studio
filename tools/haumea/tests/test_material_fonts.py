"""Material text as the SA-MP client draws it: SA-MP's own GTAWEAPON3 font and
strings built from #define macros.

The client (reSA-MP 0.3.7-R5, uihelpers.cpp CreateRenderedTextTextureWithFontAndSurface)
creates the font with D3DXCreateFontA(height = the font size, weight 400 or
700, the font name) and draws into a texture of the material size
(objectruntimehelpers.cpp MapObjectMaterialTextSizeCodeToDimensions: 60 is
128x128) with DT_NOCLIP, plus DT_CENTER|DT_VCENTER for alignment 1.

1. The user's sign, SetDynamicObjectMaterialText(o, 0, "3", 60, "GTAWEAPON3",
   140, 0, 0xFFFFFFFF, 0x00000000, 1), with a stand-in font in
   fixtures/fonts: family GTAWEAPON3, a symbol cmap ("3" at U+F033) and the
   real gtaweap3.ttf's usWinDescent of 65507 (-29). At 140 px the glyph
   overflows the 128x128 texture, as in game; read unsigned, that descent
   made it a speck.
2. ""LIGHTRED"MEGA" with #define LIGHTRED "{FF6347}" imports as
   "{FF6347}MEGA", and a macro defined nowhere is left out with a note
   (the tester's skatepark.pwn).

Needs a built editor and a game folder (HAUMEA_GAME_DIR, or the synthetic one
run_all.sh makes). Fonts come only from private folders.
"""
from pathlib import Path
import glob
import os
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from haumea_test_env import editor_env, run_editor  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
HAVE_DISPLAY = bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))
FONTS = HERE / 'fixtures' / 'fonts'


def read_png_alpha(path):
    """Width, height and the alpha rows of a PNG as lodepng writes it: it picks
    the smallest format, so RGBA, grey+alpha, a palette, or no alpha at all
    when every pixel is opaque."""
    data = Path(path).read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos, idat, trns = 8, b'', b''
    while pos < len(data):
        length, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            width, height, depth, colour = struct.unpack('>IIBB', body[:10])
        elif kind == b'tRNS':
            trns = body
        elif kind == b'IDAT':
            idat += body
        pos += 12 + length
    if colour in (0, 2) and not trns:
        return width, height, [bytes([255]) * width] * height
    assert depth == 8 and colour in (3, 4, 6), (depth, colour)
    bpp = {3: 1, 4: 2, 6: 4}[colour]
    raw, stride, rows, prev = zlib.decompress(idat), width * bpp, [], bytearray(width * bpp)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], prev[x - bpp] if x >= bpp else 0
            if kind == 1: line[x] = (line[x] + a) & 255
            elif kind == 2: line[x] = (line[x] + b) & 255
            elif kind == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        if colour == 3:
            rows.append(bytes(trns[i] if i < len(trns) else 255 for i in line))
        else:
            rows.append(bytes(line[bpp - 1::bpp]))
        prev = line
    return width, height, rows


@unittest.skipUnless(GAME_DIR and BINARY and HAVE_DISPLAY,
                     'set HAUMEA_GAME_DIR and build the editor to run')
class MaterialFontTests(unittest.TestCase):
    def private_env(self, tmp):
        env = editor_env(tmp)
        empty = Path(tmp, 'no-fonts')
        empty.mkdir(exist_ok=True)
        # Only the stand-in: no user, bundled or system GTAWEAPON3.
        env['HAUMEA_USER_FONTS_DIR'] = str(Path(tmp, 'user-fonts'))
        env['HAUMEA_BUNDLED_FONTS_DIR'] = str(empty)
        env['HAUMEA_FONT_DIRS'] = str(FONTS)
        return env

    def test_gtaweapon3_sign(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp, 'sign.png')
            result = run_editor([BINARY, '--game', GAME_DIR, '--material-text-png', out,
                                 '3', '60', 'GTAWEAPON3', '140', '0', '1'],
                                cwd=tmp, env=self.private_env(tmp), capture_output=True, text=True)
            self.assertIn('material-text-png: ' + str(out), result.stdout, result.stdout[-2000:])
            self.assertIn('gtaweapon3-standin.ttf', result.stdout)
            width, height, alpha = read_png_alpha(out)
            self.assertEqual((width, height), (128, 128))
            covered = sum(1 for row in alpha for a in row if a > 128)
            # Height 140 is the GDI cell (winAscent + winDescent = 1538 units),
            # so the 1800x1500-unit block is 164x136 px and covers the whole
            # texture; with the descent read unsigned it was a few pixels.
            self.assertGreater(covered, width * height * 0.8, 'the glyph is too small (%d px)' % covered)

    def test_macro_strings(self):
        source = '''#include <a_samp>
#include <streamer>
#define LIGHTRED "{FF6347}"

new tmpobjid;

stock Sign()
{
\ttmpobjid = CreateDynamicObject(19353, 1977.0, -1370.9, 38.3, 0.0, 0.0, 270.0, -1, 0, -1, 300.00, 300.00);
\tSetDynamicObjectMaterialText(tmpobjid, 0, ""LIGHTRED"MEGA", 130, "Arial", 60, 1, 0xFFFFFFFF, 0x00000000, 1);
\ttmpobjid = CreateDynamicObject(19353, 1977.0, -1370.9, 37.1, 0.0, 0.0, 270.0, -1, 0, -1, 300.00, 300.00);
\tSetDynamicObjectMaterialText(tmpobjid, 0, ""GREEN2"SERWER", 130, "Arial", 60, 1, 0xFFFFFFFF, 0x00000000, 1);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, 'in.pwn').write_text(source)
            result = run_editor([BINARY, '--game', GAME_DIR, '--pawn-roundtrip', Path(tmp, 'in.pwn'),
                                 Path(tmp, 'out.pwn')],
                                cwd=tmp, env=self.private_env(tmp), capture_output=True, text=True)
            self.assertIn('pawn-roundtrip:', result.stdout, result.stdout[-2000:])
            out = Path(tmp, 'out.pwn').read_text(encoding='latin-1')
            self.assertIn('"{FF6347}MEGA"', out)
            self.assertIn('"SERWER"', out)
            self.assertEqual(out.count('SetDynamicObjectMaterialText('), 2)


if __name__ == '__main__':
    unittest.main()
