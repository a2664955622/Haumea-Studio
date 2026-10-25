# SPDX-License-Identifier: GPL-2.0-or-later
"""The production gizmo's centre/custom and individual local/world rotation math."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_samp_rotation import extract

ROOT = Path(__file__).resolve().parents[3]
LIBRW = os.environ.get('LIBRW')

@unittest.skipUnless(LIBRW and (Path(LIBRW) / 'rw.h').exists(), 'needs built librw')
class GizmoPivotTests(unittest.TestCase):
    def test_pivot_and_axis_invariants(self):
        text = (ROOT / 'tools/haumea/haumea.cpp').read_text()
        code = '\n'.join('static ' + extract(text, name) for name in (
            'rw::Quat\nNormalizeQuatOrIdentity(', 'void\nComputeGizmoRotation('))
        driver = '''#include <rw.h>\n#include <cmath>\n#include <cassert>\nusing namespace rw;\n''' + code + r'''
bool near(V3d a,V3d b){return length(sub(a,b))<.0001f;}
Quat stored(float angle,V3d axis){return conj(Quat::rotation(angle*3.1415926535f/180,axis));}
int main(){
 Quat identity={0,0,0,1},q;V3d p;
 ComputeGizmoRotation({10,0,0},identity,identity,stored(90,{0,0,1}),{0,0,0},false,false,&p,&q);
 assert(near(p,{0,10,0}));
 ComputeGizmoRotation({10,0,0},identity,identity,stored(90,{0,0,1}),{5,0,0},false,false,&p,&q);
 assert(near(p,{5,5,0}));
 ComputeGizmoRotation({0,0,0},identity,identity,stored(90,{0,0,1}),{5,0,0},false,false,&p,&q);
 assert(near(p,{5,-5,0}));
 Quat initial=stored(90,{0,0,1});
 ComputeGizmoRotation({10,20,30},initial,identity,stored(90,{1,0,0}),{0,0,0},true,true,&p,&q);
 assert(near(p,{10,20,30}));Matrix m;m.rotate(conj(q),COMBINEREPLACE);
 assert(near(m.right,{0,1,0})); // each object's own X axis stays in place
 ComputeGizmoRotation({10,20,30},initial,identity,stored(90,{1,0,0}),{0,0,0},true,false,&p,&q);
 m.rotate(conj(q),COMBINEREPLACE);assert(near(m.right,{0,0,1})); // world X carries Y into Z
 Quat negative={-initial.x,-initial.y,-initial.z,-initial.w};
 ComputeGizmoRotation({7,9,11},initial,initial,negative,{3,4,5},false,false,&p,&q);
 assert(near(p,{7,9,11}));
}'''
        libdir = next(Path(LIBRW, 'lib').glob('*/Release'))
        with tempfile.TemporaryDirectory(prefix='haumea-pivot-') as tmp:
            src, exe = Path(tmp, 'check.cpp'), Path(tmp, 'check')
            src.write_text(driver)
            subprocess.run(['c++', '-std=c++11', '-DRW_GL3', '-I', LIBRW, src,
                            str(libdir / 'librw.a'), '-lGL', '-lglfw', '-lpthread', '-o', exe], check=True)
            subprocess.run([exe], check=True)
