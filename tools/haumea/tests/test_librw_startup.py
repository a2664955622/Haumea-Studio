# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise librw's actual startup functions with a strict NSGL stand-in."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
LIBRW = Path(os.environ.get('LIBRW', ROOT.parent / 'librw'))


def function(source, name):
    start = source.index(name)
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


@unittest.skipUnless(shutil.which('c++') and (LIBRW / 'src/engine.cpp').is_file(),
                     'needs a compiler and LIBRW source checkout')
class LibrwStartupTests(unittest.TestCase):
    def compile_run(self, code):
        with tempfile.TemporaryDirectory(prefix='haumea-gl-startup-') as tmp:
            source = Path(tmp, 'check.cpp')
            source.write_text(code)
            binary = Path(tmp, 'check')
            subprocess.run(['c++', '-std=c++17', str(source), '-o', str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True)

    def test_macos_requests_a_supported_context(self):
        source = (LIBRW / 'src/gl/gl3device.cpp').read_text()
        at = source.index('startGLFW(void)')
        profiles = source[source.rfind('static struct', 0, at):source.rfind('static int', 0, at)]
        self.compile_run(r'''
#include <cassert>
#define __APPLE__ 1
#define nil nullptr
#define RWERROR(x) ((void)0)
enum { GLFW_OPENGL_API = 1, GLFW_OPENGL_ES_API, GLFW_OPENGL_PROFILE,
 GLFW_OPENGL_CORE_PROFILE, GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE,
 GLFW_RED_BITS, GLFW_GREEN_BITS, GLFW_BLUE_BITS, GLFW_REFRESH_RATE,
 GLFW_SAMPLES, GLFW_CLIENT_API, GLFW_CONTEXT_VERSION_MAJOR,
 GLFW_CONTEXT_VERSION_MINOR, VIDEOMODEEXCLUSIVE };
struct GLFWwindow {} window;
struct DisplayMode { struct {int redBits,greenBits,blueBits,refreshRate,width,height;} mode; int flags; } modes[1];
struct { DisplayMode *modes = ::modes; int currentMode, numSamples, winWidth,winHeight;
 const char *winTitle; void *monitor; GLFWwindow *window, **pWindow;
 int presentWidth,presentHeight,presentOffX,presentOffY; } glGlobals;
struct {bool gles; int glversion;} gl3Caps;
int hints[30], attempts;
bool failAll;
void glfwerr(int,const char*) {}
void glfwSetErrorCallback(void(*)(int,const char*)) {}
void glfwWindowHint(int key,int value) {hints[key]=value;}
GLFWwindow *glfwCreateWindow(int,int,const char*,void*,void*) {
 attempts++;
 if(failAll || hints[GLFW_OPENGL_PROFILE]!=GLFW_OPENGL_CORE_PROFILE ||
    hints[GLFW_OPENGL_FORWARD_COMPAT]!=GLFW_TRUE) return nullptr;
 assert(hints[GLFW_CLIENT_API] == GLFW_OPENGL_API);
 assert(hints[GLFW_CONTEXT_VERSION_MAJOR] == 3);
 assert(hints[GLFW_CONTEXT_VERSION_MINOR] == 3);
 return &window;
}
void glfwMakeContextCurrent(GLFWwindow*) {}
void glfwDestroyWindow(GLFWwindow*) {}
using GLADloadproc = void*(*)(const char*);
void *glfwGetProcAddress(const char*) {return nullptr;}
int gladLoadGLES2Loader(GLADloadproc,int) {return 1;}
int gladLoadGLLoader(GLADloadproc,int) {return 1;}
''' + profiles + '\nint\n' + function(source, 'startGLFW(void)') + r'''
int main() {
 GLFWwindow *out = nullptr; glGlobals.pWindow = &out;
 assert(startGLFW() == 1 && out == &window);
 assert(attempts == 1 && gl3Caps.glversion == 33 && !gl3Caps.gles);
 failAll = true; attempts = 0;
 assert(startGLFW() == 0 && attempts == 1);
}''')

    def test_failed_device_never_constructs_gpu_plugins(self):
        source = (LIBRW / 'src/engine.cpp').read_text()
        self.compile_run(r'''
#include <cassert>
#define nil nullptr
#define RWERROR(x) ((void)0)
using bool32 = int;
using uint = unsigned;
enum { DEVICEINIT, DEVICEFINALIZE, NUM_PLATFORMS = 1 };
int constructors, finalizes;
bool failDevice;
struct Plugins { void construct(void*) {constructors++;} };
struct Driver { static Plugins s_plglist[1]; };
Plugins Driver::s_plglist[1];
struct Engine {
 enum State {Opened,Started}; static State state; static Plugins s_plglist;
 struct Device { int system(int req,void*,int) {
  if(req==DEVICEINIT) return !failDevice;
  finalizes++; return 1;
 }} device;
 Driver *driver[1]; static bool32 start();
} instance, *engine = &instance;
Engine::State Engine::state = Engine::Opened;
Plugins Engine::s_plglist;
namespace rw {Engine *engine = ::engine;}
void readTGA(){} void writeTGA(){} void readBMP(){} void writeBMP(){} void readPNG(){} void writePNG(){}
struct Image {static void registerFileFormat(const char*,void(*)(),void(*)()) {}};
bool32
''' + function(source, 'Engine::start(void)') + r'''
int main() {
 failDevice = true;
 assert(Engine::start() == 0);
 assert(constructors == 0 && finalizes == 0 && Engine::state == Engine::Opened);
 failDevice = false;
 assert(Engine::start() == 1);
 assert(constructors == 2 && finalizes == 1 && Engine::state == Engine::Started);
}''')


if __name__ == '__main__':
    unittest.main()
