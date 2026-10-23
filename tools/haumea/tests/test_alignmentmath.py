# SPDX-License-Identifier: GPL-2.0-or-later
"""Check ruler constraints, signed angles, guide projection and edge/centre snaps."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class AlignmentMathTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'needs a C++ compiler')
    def test_geometry(self):
        src = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='haumea-alignment-') as tmp:
            source = Path(tmp, 'check.cpp')
            source.write_text(r'''#include "alignmentmath.h"
#include <cassert>
using namespace Alignment;
bool near(float a,float b) {return std::abs(a-b)<.0001f;}
int main() {
 auto d=Constrain({3,-7,2},true,false,15);
 assert(d[0]==0 && d[1]==-7 && d[2]==0);
 d=Constrain({3,2,-9},true,false,15);
 assert(d[0]==0 && d[1]==0 && d[2]==-9);
 d=Constrain({3,4,5},false,true,45);
 assert(near(d[0],d[1]) && near(std::hypot(d[0],d[1]),5) && d[2]==5);
 d=Constrain({3,4,5},true,true,45);
 assert(near(d[0],d[1]) && d[2]==0);
 d=Constrain({0,0,0},true,true,0);
 assert(near(d[0],0) && near(d[1],0));
 for(float heading : {-179.f,-90.f,0.f,45.f,179.f})
  for(float pitch : {-60.f,0.f,60.f}) {
   auto a=Angles(FromAngles(13,heading,pitch));
   assert(near(a[0],13) && near(a[1],heading) && near(a[2],pitch));
  }
 assert(near(Angles({3,4,12})[0],13));
 assert(near(Projection({25,4,9},{0,0,0},{10,0,0}),2.5f));
 assert(near(Projection({-5,4,9},{0,0,0},{10,0,0}),-.5f));
 assert(Projection({5,4,9},{0,0,0},{0,0,0})==0);
 assert(near(AxisCorrection(9,11,10.2f,.5f),.2f)); // centre
 assert(near(AxisCorrection(9,11,11.3f,.5f),.3f)); // outer edge
 assert(near(AxisCorrection(9,11,8.8f,.5f),-.2f)); // inner edge
 assert(AxisCorrection(9,11,15,.5f)>.5f);
}''')
            binary = Path(tmp, 'check')
            subprocess.run(['c++', '-std=c++17', '-I', str(src), str(source), '-o', str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
