# SPDX-License-Identifier: GPL-2.0-or-later
"""FX parsing and simulation use original fixtures; game coverage is optional."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE / 'fixture'))
from particle_effects import project

DRIVER = r'''
#include "particlefx.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
using namespace ParticleFX;
int main(int argc,char**argv){
 std::ifstream f(argv[2]);Project p;std::string error,mode=argv[1];bool ok=p.load(f,&error);
 if(mode=="invalid"){assert(!ok);assert(!error.empty());assert(p.systems.empty());return 0;}
 if(!ok){std::cerr<<error;return 1;}
 if(mode=="names"){for(auto &s:p.systems)std::cout<<s.name<<'\n';return 0;}
 assert(p.find("FIRE")&&p.find("water_fountain")&&p.find("cigarette_smoke"));
 if(mode=="curves"){
  Curve c;c.keys={{0,0},{1,10}};assert(c.sample(.25,99)==2.5f);assert(c.sample(-1,99)==0);assert(c.sample(2,99)==10);
  c.looped=true;assert(c.sample(1.25,99)==2.5f);
  assert(p.find("fire")->primitives[0].value(Rate,0,1,0,0)==60);
 }else if(mode=="physics"){
  Instance water(p.find("water_fountain"),12);for(int k=0;k<60;k++)water.advance(1.f/60,{});
  assert(water.particles.size()>=59&&water.particles.size()<=60);
  assert(water.particles.front().position.z>2&&water.particles.front().position.z<4);
  assert(water.particles.front().velocity.z<0); // rises, then falls under gravity
  Instance fire(p.find("fire"),12);for(int k=0;k<60;k++)fire.advance(1.f/60,{});
  auto older=fire.appearance(fire.particles.front()),newer=fire.appearance(fire.particles.back());
  assert(older.width>newer.width&&older.alpha<newer.alpha);
 }else if(mode=="transform"){
  Instance a(p.find("fire"),123),b(p.find("fire"),123);
  Matrix transform;transform.right={0,1,0};transform.up={0,0,1};transform.at={1,0,0};transform.pos={10,20,30};
  for(int k=0;k<12;k++){a.advance(1.f/60,{});b.advance(1.f/60,transform);}
  assert(a.particles.size()==b.particles.size()&&!a.particles.empty());
  for(size_t k=0;k<a.particles.size();k++){
   Vec expected=transform.point(a.particles[k].position),actual=b.particles[k].position;
   assert(std::fabs(expected.x-actual.x)<.0001f&&std::fabs(expected.y-actual.y)<.0001f&&std::fabs(expected.z-actual.z)<.0001f);
  }
 }else if(mode=="framerate"){
  Instance a(p.find("cigarette_smoke"),7),b(p.find("cigarette_smoke"),7);
  for(int k=0;k<60;k++)a.advance(1.f/60,{});
  for(int k=0;k<30;k++)b.advance(1.f/30,{});
  assert(a.particles.size()==b.particles.size());
  for(size_t k=0;k<a.particles.size();k++)assert(std::fabs(a.particles[k].position.z-b.particles[k].position.z)<.0001f);
 }else if(mode=="budget"){
  Instance a(p.find("fire"));for(int k=0;k<600;k++){a.advance(1.f/60,{}, {},7);assert(a.particles.size()<=7);}
  assert(!a.particles.empty());a.advance(.1f,{}, {},0);assert(a.particles.empty());
 }else if(mode=="burst"){
  auto system=*p.find("fire");system.playMode=0;system.length=.2f;Instance a(&system);
  for(int k=0;k<12;k++)a.advance(1.f/60,{});assert(!a.particles.empty());
  for(int k=0;k<120;k++)a.advance(1.f/60,{});assert(a.particles.empty());
 }
}
'''


@unittest.skipUnless(shutil.which(os.environ.get('CXX', 'c++')), 'needs a C++ compiler')
class ParticleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='haumea-particle-tests-')
        cls.root = Path(cls.tmp.name)
        cls.exe = cls.root / 'driver'
        driver = cls.root / 'driver.cpp'
        driver.write_text(DRIVER)
        result = subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-I', str(ROOT / 'tools/haumea'),
                                 str(driver), str(ROOT / 'tools/haumea/particlefx.cpp'), '-o', str(cls.exe)],
                                capture_output=True, text=True)
        if result.returncode:
            cls.tmp.cleanup()
            raise AssertionError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_case(self, mode, text=None, path=None):
        if path is None:
            path = self.root / 'test.fxp'
            path.write_text(project() if text is None else text)
        result = subprocess.run([str(self.exe), mode, str(path)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_curves_and_emission_channels(self): self.run_case('curves')
    def test_ballistics_colour_size_and_lifetime(self): self.run_case('physics')
    def test_emitter_position_and_rotation(self): self.run_case('transform')
    def test_frame_rate_independence(self): self.run_case('framerate')
    def test_particle_budget(self): self.run_case('budget')
    def test_one_shot_burst_stops(self): self.run_case('burst')

    def test_reject_malformed_projects(self):
        for text in ('FX_SYSTEM_DATA:\nNAME: broken\n', project().replace('VAL: 60', 'VAL: nan', 1),
                     project().replace('NUM_KEYS: 1', 'NUM_KEYS: 999999', 1),
                     project().replace('VAL: 60', 'VAL: 1e30', 1)):
            with self.subTest(text=text[:100]): self.run_case('invalid', text)

    @unittest.skipUnless(os.environ.get('HAUMEA_PARTICLE_GAME'), 'optional installed-game coverage')
    def test_all_samp_particle_objects_have_fx_definitions(self):
        game = Path(os.environ['HAUMEA_PARTICLE_GAME'])
        names = set(self.run_case('names', path=game / 'models/effects.fxp').splitlines())
        models = {}
        for line in (game / 'SAMP/SAMP.ide').read_text(errors='replace').splitlines():
            fields = [x.strip() for x in line.split(',')]
            if len(fields) >= 2 and fields[0].isdigit() and 18668 <= int(fields[0]) <= 18748:
                models[int(fields[0])] = fields[1].lower()
        self.assertEqual(len(models), 81)
        with (game / 'SAMP/SAMP.img').open('rb') as stream:
            self.assertEqual(stream.read(4), b'VER2')
            entries = {}
            for _ in range(struct.unpack('<I', stream.read(4))[0]):
                offset, size, size2, name = struct.unpack('<IHH24s', stream.read(32))
                entries[name.split(b'\0')[0].decode().lower()] = offset * 2048, (size or size2) * 2048
            for model, name in models.items():
                offset, size = entries[name + '.dff']; stream.seek(offset); data = stream.read(size)
                at = data.find(struct.pack('<I', 0x253F2F8))
                self.assertGreaterEqual(at, 0, str(model))
                effect_name = data[at + 36:at + 60].split(b'\0')[0].decode().lower()
                self.assertIn(effect_name, names, f'{model}: {effect_name}')
