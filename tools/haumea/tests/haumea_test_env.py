# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the editor from tests without touching the user's own settings.

Haumea keeps settings, recent maps and window state in ~/.config/haumea
(%APPDATA%\\Haumea, ~/Library/Application Support/Haumea). Every editor a
test starts gets a fresh private config folder instead.

On Linux the editor never opens on the user's real display either: it only
starts inside run_all.sh's own Xvfb (HAUMEA_TEST_XVFB=1), or in an Xvfb the
test starts itself (virtual_display=True)."""
import os
import sys
import tempfile


# For any editor a test starts on Linux: no Wayland compositor to find.
NO_WAYLAND = {'WAYLAND_DISPLAY': '/nonexistent/haumea-test', 'XDG_SESSION_TYPE': 'x11'}

# Mesa's software renderer starts one render thread per CPU in every editor: with
# many test editors at once that is hundreds of threads. Two each (LP_NUM_THREADS,
# unless set), no GL thread, and always llvmpipe, so runs behave the same.
SOFTWARE_GL = {'MESA_GLTHREAD': 'false', 'GALLIUM_DRIVER': 'llvmpipe'}


def check_virtual_display():
    """Raise unless an editor started now would open on a virtual display
    (editor_env points Wayland nowhere, so DISPLAY decides)."""
    if not sys.platform.startswith('linux') or os.environ.get('HAUMEA_TEST_XVFB') == '1':
        return
    if os.environ.get('DISPLAY'):
        raise RuntimeError("refusing to open the editor on the real display: run the tests through "
                           "tools/haumea/tests/run_all.sh, which starts its own Xvfb, or under "
                           "xvfb-run with HAUMEA_TEST_XVFB=1")


def editor_env(scratch, virtual_display=False):
    """Environment for one editor process, with its config under scratch.
    virtual_display: the caller starts the editor in its own Xvfb (xvfb-run)."""
    if not virtual_display:
        check_virtual_display()
    config = tempfile.mkdtemp(prefix='haumea-config-', dir=scratch)
    env = dict(os.environ)
    # GLFW prefers Wayland and opens on the user's desktop even inside
    # xvfb-run, which only sets DISPLAY: unsetting WAYLAND_DISPLAY isn't enough,
    # libwayland then tries the session's default socket. Point it nowhere.
    env.update(NO_WAYLAND)
    env.pop('WAYLAND_SOCKET', None)
    # And no session runtime folder, where the desktop's sockets live.
    env['XDG_RUNTIME_DIR'] = tempfile.mkdtemp(prefix='haumea-runtime-', dir=scratch)	# 0700
    env['XDG_CONFIG_HOME'] = config
    env['APPDATA'] = config
    # Start empty: never inherit the user's old <GTA>/haumea_data.
    env['HAUMEA_NO_MIGRATE'] = '1'
    # Material-text fonts from a private folder: never the user's real one.
    env['HAUMEA_USER_FONTS_DIR'] = os.path.join(config, 'fonts')
    env.update(SOFTWARE_GL)
    env.setdefault('LP_NUM_THREADS', '2')
    # Never look for updates, or install one, from a test.
    env['HAUMEA_NO_UPDATE'] = '1'
    if sys.platform == 'darwin':
        env['HOME'] = config
    return env


# ---- Running an editor: real-game slots and a hard timeout ----
#
# The real game takes minutes to load when many editors do it at once, the
# synthetic fixture (fixture/make_game.py) seconds. So editors on the real
# game take one of a few machine-wide slots (across every run and session),
# and every editor gets a timeout that kills it and fails loudly.

import contextlib
import fcntl
import signal
import subprocess
import time
from pathlib import Path

REAL_GAME_TIMEOUT = 300
FIXTURE_TIMEOUT = 120


def is_real_game(game_dir):
    return bool(game_dir) and not Path(game_dir, 'data', 'maps', 'fixture.ide').is_file()


def real_game_slots():
    try:
        return max(1, int(os.environ.get('HAUMEA_REALGAME_SLOTS', '4')))
    except ValueError:
        return 4


@contextlib.contextmanager
def game_slot(game_dir):
    """Holds one of the machine-wide real-game slots (/tmp/ariane-realgame-N.lock)
    while the block runs; nothing for the fixture."""
    if not is_real_game(game_dir) or sys.platform == 'win32':
        yield
        return
    while True:
        for n in range(1, real_game_slots() + 1):
            handle = open(f'/tmp/ariane-realgame-{n}.lock', 'a')
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                handle.close()
                continue
            try:
                yield
            finally:
                handle.close()
            return
        time.sleep(1)


class EditorTimeout(AssertionError):
    pass


def run_editor(args, *, timeout=None, stdout=None, **kwargs):
    """subprocess.run for an editor (its game is the folder after --game): in a
    real-game slot when that is the real game, killed after timeout seconds
    (300 real game, 120 fixture; the wait for a slot doesn't count). A timeout
    raises EditorTimeout with the end of its output. stdout: a file to write
    to, else stdout and stderr are captured together in .stdout."""
    args = [str(a) for a in args]
    env = kwargs.get('env') or os.environ
    game_dir = args[args.index('--game') + 1] if '--game' in args else env.get('HAUMEA_GAME_DIR', '')
    kwargs.pop('capture_output', None)
    if timeout is None:
        timeout = REAL_GAME_TIMEOUT if is_real_game(game_dir) else FIXTURE_TIMEOUT
    capture = stdout is None
    kwargs.setdefault('text', True)
    with game_slot(game_dir):
        process = subprocess.Popen(args, stdout=subprocess.PIPE if capture else stdout,
                                   stderr=subprocess.STDOUT if capture or stdout else None,
                                   start_new_session=True, **kwargs)
        try:
            out, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            with contextlib.suppress(OSError):
                os.killpg(process.pid, signal.SIGKILL)
            out, _ = process.communicate()
            if not capture:
                with contextlib.suppress(OSError, AttributeError):
                    stdout.flush()
                    out = Path(stdout.name).read_text(errors='replace')
            tail = '\n'.join((out or '').splitlines()[-15:])
            raise EditorTimeout(f'editor killed after {timeout} s: {" ".join(map(str, args[:6]))}...\n'
                                f'last output:\n{tail}') from None
    return subprocess.CompletedProcess(args, process.returncode, out if capture else None, '' if capture else None)
