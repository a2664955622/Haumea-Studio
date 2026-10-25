# SPDX-License-Identifier: GPL-2.0-or-later
"""Curve subdivision, arc-length placement, and frames through a vertical loop."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class LayoutMathTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'needs a C++ compiler')
    def test_curves_and_frames(self):
        with tempfile.TemporaryDirectory(prefix='haumea-layout-math-') as tmp:
            source=Path(tmp,'check.cpp')
            source.write_text(r'''
#include "layoutmath.h"
#include <cassert>
using namespace LayoutMath;
bool near(Point a,Point b,float eps=.002f){return length(sub(a,b))<eps;}
int main(){
 std::vector<Knot> line(2);line[0].p={0,0,0};line[1].p={10,0,0};automaticHandles(line,false);
 auto s=sample(line,false,false,2);assert(s.size()==6);for(int i=0;i<6;i++){assert(near(s[i].p,{i*2.f,0,0}));assert(near(s[i].tangent,{1,0,0}));}
 assert(sample(line,true,false,0).empty());assert(sample(line,true,false,1,3).size()==3);
 Knot a=line[0],b=line[1];a.out={2,3,4};b.in={-2,3,-2};Knot originalA=a,originalB=b;
 Knot mid=split(a,b);
 for(int i=0;i<=100;i++){float t=i/100.f;Point divided=t<=.5f?bezier(a,mid,t*2):bezier(mid,b,t*2-1);assert(near(divided,bezier(originalA,originalB,t)));}
 mid.p[2]+=10;assert(bezier(a,mid,1)[2]>10);
 const float radius=10,k=.5522847498f;std::vector<Knot> loop;
 for(int i=0;i<4;i++){float angle=i*1.570796327f;Knot n;n.p={0,radius*std::cos(angle),radius*std::sin(angle)};n.out={0,-radius*std::sin(angle)*k,radius*std::cos(angle)*k};n.in=mul(n.out,-1);n.automatic=false;loop.push_back(n);}
 auto ring=sample(loop,true,true,.25f);assert(ring.size()>250&&ring.size()<254);
 for(size_t i=0;i<ring.size();i++){
  const auto &p=ring[i];assert(std::isfinite(p.p[2]));assert(std::abs(length(p.normal)-1)<.001f);assert(std::abs(dot(p.normal,p.tangent))<.001f);
  assert(std::abs(std::hypot(p.p[1],p.p[2])-radius)<.01f);
  if(i){assert(dot(p.normal,ring[i-1].normal)>.99f);assert(std::abs(length(sub(p.p,ring[i-1].p))-.25f)<.003f);}
 }
 assert(!near(ring.front().p,ring.back().p,.01f));
 std::vector<Knot> degenerate(3);for(auto &p:degenerate)p.p={1,1,1};assert(sample(degenerate,true,false,1).empty());
 std::vector<Knot> corner(3);corner[0].p={0,0,0};corner[1].p={10,0,0};corner[2].p={10,10,0};auto c=sample(corner,false,false,2);assert(near(c[6].tangent,{0,1,0}));
}''')
            exe=Path(tmp,'check')
            subprocess.run(['c++','-std=c++14','-I',str(Path(__file__).resolve().parents[1]),source,'-o',exe],check=True)
            subprocess.run([exe],check=True)
