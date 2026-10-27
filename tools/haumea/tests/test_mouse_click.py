# SPDX-License-Identifier: GPL-2.0-or-later
"""Drive the production click classifier with real press/move/release sequences."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class MouseClickTests(unittest.TestCase):
    def test_jitter_drag_and_long_press(self):
        source = (ROOT / 'tools/haumea/Pad.cpp').read_text()
        code = source[source.index('CPad CPad::Pads'):source.index('void\nCPad::Update(short')]
        clicked = source[source.index('bool\nCPad::IsMButtonClicked'):source.index('bool\nCPad::IsMButtonDown')]
        fixture = r'''
#include <cassert>
#include <cstring>
#include <cstdint>
using uint16=uint16_t;
#define KEY_NUMKEYS 16
float uiScale=1;
float GetUiScale(){return uiScale;}
struct CMouseControllerState{int btns=0,x=0,y=0;};
struct CPad{
 static CPad Pads[2];
 static uint16 oldKeystates[16],newKeystates[16],tempKeystates[16];
 static CMouseControllerState oldMouseState,newMouseState,tempMouseState;
 static int clickState,clickx,clicky,clickbtn;
 static CPad*GetPad(int i){return &Pads[i];}
 void Update(short){}
 static void UpdatePads();
 static bool IsMButtonClicked(int);
 static void CancelClick(){if(clickState==1)clickState=0;}
};
'''
        cases = r'''
void input(int buttons,int x,int y){
 CPad::tempMouseState.btns=buttons; CPad::tempMouseState.x=x; CPad::tempMouseState.y=y;
 CPad::UpdatePads();
}
int main(){
 for(int button : {1,2,3}){
  int mask=1<<(button-1);
  input(0,100,100); input(mask,100,100);
  input(mask,103,102); input(0,103,102);
  assert(CPad::IsMButtonClicked(button));
  input(0,100,100); input(mask,100,100);
  for(int frame=0;frame<100;frame++)input(mask,100,100);
  input(0,100,100); assert(CPad::IsMButtonClicked(button));
  input(0,100,100); input(mask,100,100); input(mask,120,100);
  input(0,100,100); assert(!CPad::IsMButtonClicked(button));
  input(mask,100,100); CPad::CancelClick(); input(0,100,100);
  assert(!CPad::IsMButtonClicked(button));
 }
 uiScale=2;
 input(4,100,100); input(0,111,100); assert(CPad::IsMButtonClicked(3));
 input(0,100,100); input(4,100,100); input(0,113,100); assert(!CPad::IsMButtonClicked(3));
}
'''
        with tempfile.TemporaryDirectory(prefix='haumea-click-test-') as directory:
            cpp, exe = Path(directory) / 'click.cpp', Path(directory) / 'click'
            cpp.write_text(fixture + code + clicked + '#include <initializer_list>\n' + cases)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
