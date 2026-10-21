"""Run the editor from tests without touching the user's own settings.

Haumea keeps settings, recent maps and window state in ~/.config/haumea
(%APPDATA%\\Haumea, ~/Library/Application Support/Haumea). Every editor a
test starts gets a fresh private config folder instead."""
import os
import sys
import tempfile


def editor_env(scratch):
    """Environment for one editor process, with its config under scratch."""
    config = tempfile.mkdtemp(prefix='haumea-config-', dir=scratch)
    env = dict(os.environ)
    env['XDG_CONFIG_HOME'] = config
    env['APPDATA'] = config
    # Start empty: never inherit the user's old <GTA>/haumea_data.
    env['HAUMEA_NO_MIGRATE'] = '1'
    if sys.platform == 'darwin':
        env['HOME'] = config
    return env
