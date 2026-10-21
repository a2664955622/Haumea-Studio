# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the editor's Euler conversion against the basis samp.dll builds."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
LIBRW = os.environ.get('LIBRW')


def extract(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


@unittest.skipUnless(LIBRW and (Path(LIBRW) / 'rw.h').exists(), 'set LIBRW to a built librw')
class SampRotationTests(unittest.TestCase):
    def test_matches_samp_object_basis(self):
        source = (ROOT / 'tools/haumea/haumea.cpp').read_text()
        functions = '\n'.join('static ' + extract(source, name) for name in (
            'rw::Quat\nQuatFromMatrix(', 'rw::Quat\nNormalizeQuatOrIdentity(',
            'rw::V3d\nGetObjectRotationDegrees(', 'rw::Quat\nMakeObjectRotationDegrees('))
        fixture = r'''
#include <rw.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
using namespace rw;
template <typename T> T clamp(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }
''' + functions + r'''
int main(){
	srand(7);
	for(int t = 0; t < 5000; t++){
		float x = rand()%360 - 180, y = rand()%360 - 180, z = rand()%360 - 180;
		Quat q = MakeObjectRotationDegrees({ x, y, z });
		Matrix m;
		m.rotate(conj(q), COMBINEREPLACE);	// ObjectInst::UpdateMatrix
		// samp.dll SetObjectRuntimeRotationDegreesAndBasis: R = Rz * Rx * Ry
		double r = M_PI/180, sx = sin(x*r), cx = cos(x*r), sy = sin(y*r), cy = cos(y*r),
			sz = sin(z*r), cz = cos(z*r);
		double want[9] = { cz*cy - sz*sx*sy, cz*sx*sy + sz*cy, -sy*cx, -sz*cx, cz*cx, sx,
			sz*sx*cy + cz*sy, sz*sy - cz*sx*cy, cy*cx };
		double got[9] = { m.right.x, m.right.y, m.right.z, m.up.x, m.up.y, m.up.z,
			m.at.x, m.at.y, m.at.z };
		for(int i = 0; i < 9; i++)
			if(fabs(want[i] - got[i]) > 1e-4){ printf("basis %g %g %g\n", x, y, z); return 1; }
		Quat back = MakeObjectRotationDegrees(GetObjectRotationDegrees(q));
		if(fabs(q.x*back.x + q.y*back.y + q.z*back.z + q.w*back.w) < 1 - 1e-5){
			printf("roundtrip %g %g %g\n", x, y, z);
			return 1;
		}
	}
	return 0;
}
'''
        libdir = next(Path(LIBRW, 'lib').glob('*/Release'), None)
        self.assertIsNotNone(libdir, 'librw has not been built')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp, 'rotation.cpp')
            exe = Path(tmp, 'rotation')
            src.write_text(fixture)
            subprocess.run(['c++', '-std=c++11', '-DRW_GL3', '-I', LIBRW, str(src),
                            str(libdir / 'librw.a'), '-lGL', '-lglfw', '-lpthread', '-o', str(exe)],
                           check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == '__main__':
    unittest.main()
