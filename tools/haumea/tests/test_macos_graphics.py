# SPDX-License-Identifier: GPL-2.0-or-later
"""A missing native Mac renderer is distinct from a broken editor/MCP check."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/release'))
import macos_graphics
import check_namaka_engine


def library(choose=0, context=0, version=b'4.1 Core', pixels=True):
    gl=Mock()
    def pixel_format(attrs,out,count):
        assert list(attrs)==[73,74,99,0x3200,8,24,11,8,12,24,13,8,5,55,0,0]
        out._obj.value=123 if pixels and choose==0 else None
        count._obj.value=1 if pixels and choose==0 else 0
        return choose
    def create(pixel,share,out):
        out._obj.value=456 if context==0 else None
        return context
    gl.CGLChoosePixelFormat.side_effect=pixel_format
    gl.CGLCreateContext.side_effect=create
    gl.CGLGetCurrentContext.return_value=789
    gl.CGLSetCurrentContext.return_value=0
    gl.glGetString.side_effect=lambda key: version if key==0x1F02 else b'Apple test renderer'
    return gl


class MacGraphicsProbeTests(unittest.TestCase):
    def test_supported_renderer_and_cleanup(self):
        gl=library()
        self.assertEqual(macos_graphics.probe(gl),{'available':True,'version':'4.1 Core','renderer':'Apple test renderer'})
        self.assertEqual(gl.CGLSetCurrentContext.call_args.args[0],789)
        gl.CGLDestroyContext.assert_called_once()
        gl.CGLDestroyPixelFormat.assert_called_once()

    def test_no_renderer_and_old_context(self):
        for gl in (library(pixels=False),library(choose=10002),library(context=10009),library(version=b'3.2 Core')):
            with self.subTest(gl=gl),self.assertRaises(macos_graphics.GraphicsUnavailable):
                macos_graphics.probe(gl)

    def test_probe_errors_are_not_capability_skips(self):
        for gl in (library(choose=10000),library(context=10016),library(version=b'')):
            with self.subTest(gl=gl),self.assertRaises(RuntimeError) as error:
                macos_graphics.probe(gl)
            self.assertNotIsInstance(error.exception,macos_graphics.GraphicsUnavailable)


class MacEngineCheckTests(unittest.TestCase):
    def run_check(self,folder,probe_error=None,exit_code=1,version_code=0):
        game=folder/'game';(game/'data/maps').mkdir(parents=True)
        (game/'data/maps/fixture.ide').touch()
        (folder/'namaka-complete').touch()
        output=folder/'check'
        argv=['check','--engine',str(folder/'haumea'),'--game',str(game),'--output',str(output),
              '--tools','83','--probe-macos-graphics']
        version=subprocess.CompletedProcess([],version_code,'Haumea Studio test\n','')
        with patch.object(sys,'argv',argv),patch.object(sys,'prefix',str(folder)),patch.object(sys,'platform','darwin'), \
             patch.object(macos_graphics,'probe',side_effect=probe_error,return_value={'available':True}), \
             patch.object(check_namaka_engine.subprocess,'run',return_value=version), \
             patch.object(check_namaka_engine.subprocess,'Popen') as popen:
            popen.return_value.poll.return_value=exit_code
            return check_namaka_engine.main(),output,popen

    def test_missing_renderer_records_untested_and_exits_78(self):
        with tempfile.TemporaryDirectory() as tmp:
            code,output,popen=self.run_check(Path(tmp),macos_graphics.GraphicsUnavailable('no renderer'))
            self.assertEqual(code,78)
            popen.assert_not_called()
            self.assertEqual(json.loads((output/'graphics.json').read_text())['live_mcp_check'],'not run')
            self.assertTrue((output/'version.log').is_file())

    def test_capable_runner_editor_failure_stays_a_failure(self):
        with tempfile.TemporaryDirectory() as tmp,self.assertRaisesRegex(RuntimeError,'Editor exited with code 1'):
            self.run_check(Path(tmp))

    def test_broken_binary_or_probe_never_skips(self):
        with tempfile.TemporaryDirectory() as tmp,self.assertRaisesRegex(RuntimeError,'failed --version'):
            self.run_check(Path(tmp),macos_graphics.GraphicsUnavailable('no renderer'),version_code=1)
        with tempfile.TemporaryDirectory() as tmp,self.assertRaisesRegex(RuntimeError,'bad probe'):
            self.run_check(Path(tmp),RuntimeError('bad probe'))
