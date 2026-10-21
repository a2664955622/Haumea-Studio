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
    if sys.platform == 'darwin':
        env['HOME'] = config
    return env
