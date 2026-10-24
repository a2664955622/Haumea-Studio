#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Use an installed Namaka venv to check the editor's automatic listener and live MCP tools.

Run with the installed venv's Python. Supply the synthetic fixture game, never
a user's game folder. Linux runs require the project's isolated Xvfb environment.
"""
import argparse
import asyncio
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/haumea/tests'))
from haumea_test_env import editor_env  # noqa: E402


async def check_mcp(env, output, expected_tools):
    from mcp import ClientSession, StdioServerParameters
    from mcp.client.stdio import stdio_client
    from PIL import Image

    params = StdioServerParameters(command=sys.executable,
                                  args=['-m', 'haumea_agent_tools.haumea_mcp'], env=env)
    async with stdio_client(params) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            tools = (await session.list_tools()).tools
            if len(tools) != expected_tools:
                raise RuntimeError(f'Expected {expected_tools} MCP tools, got {len(tools)}')
            for name, arguments in (
                ('camera_context', {}), ('haumea_status', {}),
                ('capture_view', {'output_path': str(output / 'namaka.png')}),
            ):
                result = await session.call_tool(name, arguments)
                if result.is_error:
                    raise RuntimeError(f'{name} failed: {result}')
    with Image.open(output / 'namaka.png') as image:
        image.verify()
    print(f'{expected_tools} MCP tools; live camera, map status and PNG capture passed.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tools', type=int, required=True)
    parser.add_argument('--probe-macos-graphics', action='store_true',
                        help='probe CGL first; exit 78 only when this Mac has no supported native renderer')
    args = parser.parse_args()
    if not (args.game / 'data/maps/fixture.ide').is_file():
        parser.error('--game must be a synthetic game from tests/fixture/make_game.py')
    if not (Path(sys.prefix) / 'namaka-complete').is_file():
        parser.error('run with the Python from a Namaka venv installed by namaka_install.py')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.probe_macos_graphics:
        if sys.platform != 'darwin':
            parser.error('--probe-macos-graphics requires macOS')
        # Even a VM without graphics must load the installed executable and
        # report its version; bad binaries/dependencies remain hard failures.
        version = subprocess.run([str(args.engine.resolve()), '--version'], capture_output=True, text=True, timeout=30)
        (output / 'version.log').write_text(version.stdout + version.stderr)
        if version.returncode or not version.stdout.startswith('Haumea Studio '):
            raise RuntimeError('Installed editor failed --version; see version.log')
        from macos_graphics import GraphicsUnavailable, probe
        try:
            capability = probe()
        except GraphicsUnavailable as error:
            capability = {'available': False, 'reason': str(error), 'live_mcp_check': 'not run'}
            (output / 'graphics.json').write_text(json.dumps(capability, indent=2) + '\n')
            print('Native graphics unavailable: ' + str(error), file=sys.stderr)
            return 78
        (output / 'graphics.json').write_text(json.dumps(capability, indent=2) + '\n')
    env = editor_env(str(output))
    env['HAUMEA_CONFIG_DIR'] = str(output / 'config')
    env['HAUMEA_NAMAKA_VENV'] = sys.prefix
    env['HAUMEA_NAMAKA_LISTEN'] = '1'
    env['HAUMEA_DISCOVERY_DIR'] = str(output / 'discoveries')
    env['HAUMEA_AGENT_STATE_DIR'] = str(output / 'state')
    env['HAUMEA_ASSET_DB'] = str(output / 'assets.sqlite3')
    for key in ('HAUMEA_ENGINE_TCP_PORT', 'HAUMEA_ENGINE_TOKEN'):
        env.pop(key, None)
    # macOS AF_UNIX paths are limited to 104 bytes; runner.temp can be longer.
    with tempfile.TemporaryDirectory(prefix='hnm-', dir='/tmp') as socket_dir:
        endpoint = str(Path(socket_dir) / 'a.sock')
        env['HAUMEA_NAMAKA_SOCKET'] = endpoint
        env['HAUMEA_ENGINE_SOCKET'] = endpoint
        workspace = output / 'workspace'
        workspace.mkdir(exist_ok=True)
        with (output / 'engine.log').open('w') as log:
            editor = subprocess.Popen([str(args.engine.resolve()), '--game', str(args.game.resolve()),
                                       '--workspace', str(workspace)], cwd=output, env=env,
                                      stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 240
                while time.monotonic() < deadline and editor.poll() is None:
                    ping = subprocess.run([sys.executable, '-m', 'haumea_agent_tools.haumeactl', 'ping'],
                                          env=env, capture_output=True, text=True, timeout=30)
                    if ping.returncode == 0 and json.loads(ping.stdout).get('result') == 'pong':
                        break
                    time.sleep(.5)
                else:
                    log.flush()
                    details = (output / 'engine.log').read_text(errors='replace')[-16000:]
                    print('Editor startup log:\n' + (details or '(empty)'), file=sys.stderr)
                    code = editor.poll()
                    state = f'exited with code {code}' if code is not None else 'timed out'
                    raise RuntimeError(f'Editor {state} before its Namaka listener became ready; see engine.log')
                asyncio.run(asyncio.wait_for(check_mcp(env, output, args.tools), timeout=120))
            finally:
                editor.terminate()
                try:
                    editor.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    editor.kill()
                    editor.wait()


if __name__ == '__main__':
    raise SystemExit(main())
