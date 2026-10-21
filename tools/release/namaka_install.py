# SPDX-License-Identifier: GPL-2.0-or-later
"""namaka_install.py install|repair|update|uninstall|status --bundle DIR --venv DIR

Namaka's installer, run by the editor's Tools > Namaka panel with the Python
it found (and usable by hand). It installs the bundled wheel
(DIR/haumea_namaka-*.whl) and its dependencies from DIR/wheelhouse into the
venv:

- Python 3.14: everything from the wheelhouse, with no network.
- 3.10-3.13: the bundled pure-Python wheels, plus the binary ones this Python
  needs, downloaded with the same pins (wheelhouse/constraints-3.X.txt).
  Offline that fails with a message naming the version that works offline.

Repair and update rebuild the venv in its place: the old one is moved aside
first and comes back if anything fails or the run is cancelled (the next run
finds it), because a venv can't be moved once made. A finished venv has the
file namaka-complete in it.

Output, for the panel: "STEP n/m text" lines, pip's own output, and last
"DONE text" (exit 0) or "FAIL text" (exit 1; 2 when this Python can't do it).
"status" prints "VERSION x", "TOOLS n" and "PYTHON x.y" for an installed venv.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

OFFLINE_PYTHON = (3, 14)
COMPLETE = 'namaka-complete'


def say(*parts):
    print(*parts, flush=True)


def fail(text, code=1):
    say('FAIL ' + text)
    sys.exit(code)


def venv_python(venv):
    return venv / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')


def pip_env():
    """The user's own pip settings (a company index or proxy), with no prompts and no
    version check. The bundle steps pass --no-index, so only downloads use the index."""
    return dict(os.environ, PIP_DISABLE_PIP_VERSION_CHECK='1', PIP_NO_INPUT='1')


def run(argv, env=None):
    """Runs argv with its output passed through; True on success."""
    say('$ ' + ' '.join(str(a) for a in argv))
    process = subprocess.Popen([str(a) for a in argv], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               env=env or pip_env(), text=True, errors='replace')
    for line in process.stdout:
        say(line.rstrip())
    return process.wait() == 0


def bundled_wheel(bundle):
    # haumea_agent-* is the name it had as Agent Alpha.
    wheels = sorted(bundle.glob('haumea_namaka-*.whl')) or sorted(bundle.glob('haumea_agent-*.whl'))
    if not wheels:
        fail('the bundle has no Namaka wheel (%s): reinstall Haumea, or use a release build' % bundle)
    if len(wheels) > 1:
        # A bundle unzipped over an older one: which is newer isn't for a string sort to say.
        fail('the bundle has %d Namaka wheels (%s): remove the older ones from %s'
             % (len(wheels), ', '.join(w.name for w in wheels), bundle))
    return wheels[0]


def recover(venv):
    """Puts back a venv moved aside by a run that didn't finish."""
    old = venv.with_name(venv.name + '.old')
    if not old.exists():
        return
    if (venv / COMPLETE).exists():
        shutil.rmtree(old, ignore_errors=True)
    else:
        shutil.rmtree(venv, ignore_errors=True)
        old.rename(venv)
        say('Put back the Namaka install an earlier run had moved aside.')


def missing_binary_wheels(python, wheelhouse, constraints):
    """The pins no bundled wheel fits on that Python: the ones to download."""
    code = (
        'import sys\n'
        'from pathlib import Path\n'
        'from pip._vendor.packaging.tags import sys_tags\n'
        'from pip._vendor.packaging.utils import canonicalize_name, parse_wheel_filename\n'
        'supported = set(sys_tags()); have = set()\n'
        'for w in Path(sys.argv[1]).glob("*.whl"):\n'
        '    try: name, version, _, tags = parse_wheel_filename(w.name)\n'
        '    except Exception: continue\n'
        '    if tags & supported: have.add((name, str(version)))\n'
        'for line in Path(sys.argv[2]).read_text().splitlines():\n'
        '    line = line.split("#")[0].strip()\n'
        '    if "==" not in line: continue\n'
        '    name, version = line.split("==", 1)\n'
        '    if (canonicalize_name(name), version.split(";")[0].strip()) not in have:\n'
        '        print(name.strip() + "==" + version.strip())\n')
    result = subprocess.run([str(python), '-c', code, str(wheelhouse), str(constraints)],
                            capture_output=True, text=True, env=pip_env())
    if result.returncode != 0:
        fail('could not compare the bundled wheels with this Python:\n' + result.stderr[-2000:])
    return [line for line in result.stdout.splitlines() if line.strip()]


def install(bundle, venv, allow_network):
    if sys.version_info < (3, 10):
        fail('Namaka needs Python 3.10 or newer; this is %d.%d.' % sys.version_info[:2], 2)
    wheel = bundled_wheel(bundle)
    wheelhouse = bundle / 'wheelhouse'
    version = '%d.%d' % sys.version_info[:2]
    offline = sys.version_info[:2] == OFFLINE_PYTHON
    constraints = wheelhouse / ('constraints-%s.txt' % version)
    steps = 4 if offline else 5
    recover(venv)
    old = venv.with_name(venv.name + '.old')
    if venv.exists():
        say('STEP 1/%d Setting the old install aside' % steps)
        try:
            venv.rename(old)
        except OSError as error:
            fail('the Namaka install is in use (%s). Close the apps using Namaka (Claude) and try again.' % error)
    else:
        say('STEP 1/%d Preparing' % steps)
    venv.parent.mkdir(parents=True, exist_ok=True)

    def undo(text, code=1):
        shutil.rmtree(venv, ignore_errors=True)
        if old.exists():
            old.rename(venv)
            text += ' The previous install is unchanged.'
        fail(text, code)

    say('STEP 2/%d Making the Python environment (Python %s)' % (steps, version))
    if not run([sys.executable, '-m', 'venv', venv]) or not venv_python(venv).exists():
        undo('could not make a Python environment. On Debian or Ubuntu, install python3-venv '
             '(sudo apt install python3-venv) and try again.', 2)
    python = venv_python(venv)
    if offline:
        say('STEP 3/%d Installing Namaka from the bundle (offline)' % steps)
        ok = run([python, '-m', 'pip', 'install', '--no-input', '--no-index', '--find-links', wheelhouse,
                  str(wheel) + '[mcp]'])
        if not ok:
            undo('the bundled wheels did not install (see the log).')
    else:
        if not constraints.exists():
            undo('the bundle has no wheels for Python %s. Install Python 3.14 for an offline install.' % version, 2)
        missing = missing_binary_wheels(python, wheelhouse, constraints)
        download = venv.with_name(venv.name + '.download')
        shutil.rmtree(download, ignore_errors=True)
        if missing:
            say('STEP 3/%d Downloading the parts for Python %s (%d)' % (steps, version, len(missing)))
            if not allow_network:
                undo('Namaka needs Python 3.14 to install offline, or an internet connection.', 2)
            env = pip_env()
            ok = run([python, '-m', 'pip', 'download', '--no-input', '--no-deps', '--only-binary=:all:',
                      '-d', download] + missing, env)
            if not ok:
                shutil.rmtree(download, ignore_errors=True)
                undo('Namaka needs Python 3.14 to install offline, or an internet connection.', 2)
        else:
            say('STEP 3/%d Every part is in the bundle' % steps)
        say('STEP 4/%d Installing Namaka' % steps)
        argv = [python, '-m', 'pip', 'install', '--no-input', '--no-index', '--find-links', wheelhouse]
        if download.exists():
            argv += ['--find-links', download]
        ok = run(argv + ['-c', constraints, str(wheel) + '[mcp]'])
        shutil.rmtree(download, ignore_errors=True)
        if not ok:
            undo('Namaka did not install (see the log).')
    say('STEP %d/%d Checking it' % (steps, steps))
    tools = count_tools(python)
    if tools is None:
        undo('Namaka installed but does not start (see the log).')
    (venv / COMPLETE).write_text(wheel.name + '\n')
    shutil.rmtree(old, ignore_errors=True)
    say('TOOLS %d' % tools)
    say('DONE Namaka %s is installed, with %d tools.' % (wheel_version(wheel), tools))


def count_tools(python):
    code = ('import asyncio\n'
            'from haumea_agent_tools import haumea_mcp as m\n'
            'print(len(asyncio.run(m.mcp.list_tools())))\n')
    result = subprocess.run([str(python), '-I', '-c', code], capture_output=True, text=True, env=pip_env())
    if result.returncode != 0:
        say(result.stdout + result.stderr)
        return None
    try:
        return int(result.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        return None


def wheel_version(wheel):
    # haumea_agent-<version>-py3-none-any.whl
    return wheel.name.split('-')[1]


def status(venv):
    python = venv_python(venv)
    if not (venv / COMPLETE).exists() or not python.exists():
        fail('Namaka is not installed in %s.' % venv)
    result = subprocess.run([str(python), '-I', '-c',
                             'import sys, importlib.metadata as m\n'
                             'print("PYTHON %d.%d" % sys.version_info[:2])\n'
                             'for name in ("haumea-namaka", "haumea-agent"):\n'
                             '    try: print("VERSION " + m.version(name)); break\n'
                             '    except m.PackageNotFoundError: pass\n'],
                            capture_output=True, text=True, env=pip_env())
    say(result.stdout.strip())
    tools = count_tools(python)
    if tools is not None:
        say('TOOLS %d' % tools)
    say('DONE')


def uninstall(venv):
    recover(venv)
    if not venv.exists():
        say('DONE Namaka was not installed.')
        return
    say('STEP 1/1 Removing %s' % venv)
    try:
        shutil.rmtree(venv)
    except OSError as error:
        fail('could not remove it (%s). Close the apps using Namaka (Claude) and try again.' % error)
    say('DONE Namaka is uninstalled.')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('action', choices=('install', 'repair', 'update', 'uninstall', 'status'))
    parser.add_argument('--bundle', type=Path, help="the folder with Namaka's wheel and wheelhouse")
    parser.add_argument('--venv', type=Path, required=True)
    parser.add_argument('--offline', action='store_true', help='never download (fail instead)')
    args = parser.parse_args()
    venv = args.venv.resolve()
    if args.action == 'uninstall':
        uninstall(venv)
    elif args.action == 'status':
        status(venv)
    else:
        if args.bundle is None:
            fail('--bundle is needed to install')
        install(args.bundle.resolve(), venv, not args.offline)


if __name__ == '__main__':
    main()
