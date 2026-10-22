"""The updater against a local fake release server, never GitHub.

The editor's --update-check / --update-install / --update-rollback modes run
the banner's own code without a window: pick this channel's newest release,
download SHA256SUMS and the bundle, check it, unpack it (refusing unsafe
archives) and stage it; then install it into a scratch folder by its
MANIFEST, keeping edited files as .local and never touching anything else,
and roll it back exactly.
"""
from pathlib import Path
import glob
import hashlib
import http.server
import io
import json
import os
import platform
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from haumea_test_env import editor_env

ROOT = Path(__file__).resolve().parents[3]
BINARY = os.environ.get('HAUMEA_BINARY') or next(iter(sorted(
    glob.glob(str(ROOT / 'bin' / '*' / 'Release' / 'haumea')))), None)
BUNDLE = 'haumea-linux-amd64.tar.gz'


def headless(env, scratch):
    """Never the user's desktop: no X display, a Wayland socket that can't
    exist and a private runtime folder (libwayland would otherwise fall back
    to wayland-0 in the user's XDG_RUNTIME_DIR, and GLFW prefers Wayland), and
    HAUMEA_TEST_XVFB=1 so the editor forces GLFW to X11. xvfb-run then sets
    DISPLAY."""
    for var in ('DISPLAY', 'XAUTHORITY', 'WAYLAND_SOCKET'):
        env.pop(var, None)
    env['WAYLAND_DISPLAY'] = '/nonexistent/haumea-test'
    env['XDG_SESSION_TYPE'] = 'x11'
    env['XDG_RUNTIME_DIR'] = tempfile.mkdtemp(prefix='runtime-', dir=scratch)	# 0700
    env['HAUMEA_TEST_XVFB'] = '1'
    # Software GL starts one thread per core otherwise: two are plenty for a test.
    env['LP_NUM_THREADS'] = '2'
    return env


def sha(data):
    return hashlib.sha256(data).hexdigest()


def bundle(files, extra=None):
    """A release bundle: files (path -> bytes) plus their MANIFEST, as a .tar.gz."""
    manifest = ''.join(f'{sha(data)}  {path}\n' for path, data in sorted(files.items()))
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode='w:gz', format=tarfile.USTAR_FORMAT) as tar:
        for path, data in [('MANIFEST', manifest.encode())] + sorted(files.items()):
            info = tarfile.TarInfo(path)
            info.size = len(data)
            info.mode = 0o755 if path == 'haumea' else 0o644
            tar.addfile(info, io.BytesIO(data))
        if extra:
            extra(tar)
    return out.getvalue()


class FakeGitHub:
    """Serves /releases and /assets/<tag>/<name> from a dict."""

    def __init__(self):
        self.files = {}
        files = self.files

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                data = files.get(self.path)
                if data is None:
                    self.send_response(404)
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def log_message(self, *args):
                pass

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.url = f'http://127.0.0.1:{self.server.server_port}'
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def publish(self, releases):
        """releases: [(tag, {asset name: bytes})], newest first like GitHub."""
        self.files.clear()
        listing = []
        for tag, assets in releases:
            entries = []
            for name, data in assets.items():
                url = f'{self.url}/assets/{tag}/{name}'
                self.files[f'/assets/{tag}/{name}'] = data
                entries.append({'name': name, 'size': len(data), 'browser_download_url': url})
            listing.append({'tag_name': tag, 'draft': False, 'body': f'Notes for {tag}',
                            'html_url': f'{self.url}/page/{tag}', 'assets': entries})
        self.files['/releases'] = json.dumps(listing).encode()

    def release(self, tag, files, sums=None, archive=None):
        data = archive if archive is not None else bundle(files)
        text = sums if sums is not None else f'{sha(data)}  {BUNDLE}\n'
        return (tag, {BUNDLE: data, 'SHA256SUMS': text.encode()})

    def close(self):
        self.server.shutdown()


NEW = {'haumea': b'new editor', 'fonts/a.ttf': b'new font', 'prefabs/starter.haumea': b'new prefab'}


@unittest.skipUnless(BINARY and sys.platform.startswith('linux') and platform.machine() == 'x86_64',
                     'needs the Linux x86_64 editor')
class UpdaterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.github = FakeGitHub()

    @classmethod
    def tearDownClass(cls):
        cls.github.close()

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix='haumea-update-'))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_editor(self, *args, current='1.0.0.10', url=None, skip=None, channel='production', go_back=False,
                   build='production'):
        env = editor_env(str(self.tmp))
        env.pop('HAUMEA_NO_UPDATE', None)
        headless(env, str(self.tmp))
        env['HAUMEA_UPDATE_URL'] = url or self.github.url
        env['HAUMEA_UPDATE_CHANNEL'] = channel
        env['HAUMEA_UPDATE_CURRENT'] = current
        if skip:
            env['HAUMEA_UPDATE_SKIP'] = skip
        if go_back:
            env['HAUMEA_UPDATE_GO_BACK'] = '1'
        # The build's own channel: production, beta, dev, pr. Official unless a test
        # says so, whatever the binary was built as (CI builds dev).
        env['HAUMEA_UPDATE_BUILD'] = build
        report = self.tmp / 'report.json'
        report.unlink(missing_ok=True)
        result = subprocess.run([BINARY, *args, str(report)], env=env, capture_output=True, text=True,
                                timeout=120)
        self.assertTrue(report.exists(), result.stdout + result.stderr)
        return json.loads(report.read_text())

    def check(self, **kw):
        return self.run_editor('--update-check', str(self.tmp / 'work'), **kw)

    def staged(self):
        return self.tmp / 'work' / 'staged'

    # ---- Checking ----

    def test_newer_release_is_downloaded_verified_and_staged(self):
        self.github.publish([self.github.release('v1.0.0.20-production', NEW),
                             self.github.release('v1.0.0.15-production', NEW)])
        report = self.check()
        self.assertEqual(report['result'], 'staged', report)
        self.assertEqual(report['version'], '1.0.0.20')
        self.assertEqual((self.staged() / 'haumea').read_bytes(), b'new editor')
        self.assertTrue((self.staged() / 'MANIFEST').exists())
        self.assertEqual((self.staged() / '.haumea-notes').read_text(), 'Notes for v1.0.0.20-production')

    def test_same_older_skipped_and_other_channel_are_up_to_date(self):
        self.github.publish([self.github.release('v1.0.0.20-production', NEW),
                             self.github.release('v1.1.0.30-beta', NEW)])
        for current, skip in (('1.0.0.20', None), ('1.1.0.0', None), ('1.0.0.10', '1.0.0.20')):
            report = self.check(current=current, skip=skip)
            self.assertEqual(report['result'], 'up_to_date', (current, skip, report))
        self.assertFalse(self.staged().exists())

    def staged_release(self):
        return (self.staged() / '.haumea-release').read_text().splitlines()

    def test_production_ignores_beta_and_beta_takes_the_newest_of_both(self):
        self.github.publish([self.github.release('v1.0.0.30-beta', NEW),
                             self.github.release('v1.0.0.20-production', NEW),
                             self.github.release('v1.0.0.25-master', NEW)])	# a retired channel
        report = self.check(channel='production')
        self.assertEqual((report['result'], report.get('version')), ('staged', '1.0.0.20'), report)
        shutil.rmtree(self.tmp / 'work')
        report = self.check(channel='beta')
        self.assertEqual((report['result'], report.get('version')), ('staged', '1.0.0.30'), report)
        # A production release newer than every beta: beta takes it too.
        self.github.publish([self.github.release('v1.0.0.40-production', NEW),
                             self.github.release('v1.0.0.30-beta', NEW)])
        shutil.rmtree(self.tmp / 'work')
        report = self.check(channel='beta')
        self.assertEqual((report['result'], report.get('version')), ('staged', '1.0.0.40'), report)

    def test_back_on_production_never_downgrades_unless_asked(self):
        # A beta build (1.0.0.30) whose user switched back to Production.
        self.github.publish([self.github.release('v1.0.0.30-beta', NEW),
                             self.github.release('v1.0.0.20-production', NEW)])
        report = self.check(channel='production', current='1.0.0.30')
        self.assertEqual(report['result'], 'up_to_date', report)
        self.assertFalse(self.staged().exists())
        # "Go back to the latest Production now": the older production release,
        # staged as a go-back so the next start may install it.
        report = self.check(current='1.0.0.30', go_back=True)
        self.assertEqual((report['result'], report.get('version')), ('staged', '1.0.0.20'), report)
        self.assertIn('go-back', self.staged_release())
        # A plain newer update is not a go-back.
        self.github.publish([self.github.release('v1.0.0.50-production', NEW)])
        shutil.rmtree(self.tmp / 'work')
        report = self.check(current='1.0.0.30')
        self.assertEqual((report['result'], report.get('version')), ('staged', '1.0.0.50'), report)
        self.assertNotIn('go-back', self.staged_release())

    def test_a_dev_build_takes_its_official_build_and_newer_never_older(self):
        # A CI artifact from main (channel dev) at build 20.
        self.github.publish([self.github.release('v1.0.0.20-production', NEW)])
        report = self.check(build='dev', current='1.0.0.20')
        self.assertEqual((report['result'], report.get('version'), report.get('unofficial')),
                         ('staged', '1.0.0.20', True), report)
        self.github.publish([self.github.release('v1.0.0.25-production', NEW),
                             self.github.release('v1.0.0.20-production', NEW)])
        shutil.rmtree(self.tmp / 'work')
        report = self.check(build='dev', current='1.0.0.20')
        self.assertEqual((report['result'], report.get('version')), ('staged', '1.0.0.25'), report)
        self.github.publish([self.github.release('v1.0.0.17-production', NEW)])
        shutil.rmtree(self.tmp / 'work')
        report = self.check(build='dev', current='1.0.0.20')
        self.assertEqual(report['result'], 'up_to_date', report)
        # An official production build isn't offered its own version again.
        self.github.publish([self.github.release('v1.0.0.20-production', NEW)])
        report = self.check(build='production', current='1.0.0.20')
        self.assertEqual(report['result'], 'up_to_date', report)

    def test_a_pr_build_can_switch_to_the_latest_official_release(self):
        # A pull request's build number can run ahead of main's.
        self.github.publish([self.github.release('v1.0.0.30-production', NEW)])
        report = self.check(build='pr', current='1.0.0.50')
        self.assertEqual(report['result'], 'up_to_date', report)
        report = self.check(build='pr', current='1.0.0.50', go_back=True)
        self.assertEqual((report['result'], report.get('version')), ('staged', '1.0.0.30'), report)
        self.assertIn('go-back', self.staged_release())

    def test_bad_checksum_stages_nothing(self):
        self.github.publish([self.github.release('v1.0.0.20-production', NEW, sums='0' * 64 + f'  {BUNDLE}\n')])
        report = self.check()
        self.assertEqual(report['result'], 'bad_checksum', report)
        self.assertFalse(self.staged().exists())

    def test_offline_is_quiet(self):
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            port = s.getsockname()[1]
        report = self.check(url=f'http://127.0.0.1:{port}')
        self.assertEqual(report['result'], 'offline', report)

    def test_plain_http_outside_the_test_server_is_refused(self):
        tag, assets = self.github.release('v1.0.0.20-production', NEW)
        self.github.publish([(tag, assets)])
        listing = json.loads(self.github.files['/releases'])
        for asset in listing[0]['assets']:
            asset['browser_download_url'] = asset['browser_download_url'].replace('127.0.0.1', 'localhost')
        self.github.files['/releases'] = json.dumps(listing).encode()
        report = self.check()
        self.assertEqual(report['result'], 'error', report)
        self.assertIn('non-https', report['error'])

    def test_unsafe_or_broken_archives_are_refused(self):
        def escape(tar):
            info = tarfile.TarInfo('../escaped.txt')
            info.size = 4
            tar.addfile(info, io.BytesIO(b'evil'))

        def symlink(tar):
            info = tarfile.TarInfo('fonts/link')
            info.type = tarfile.SYMTYPE
            info.linkname = '/etc/passwd'
            tar.addfile(info)

        good = bundle(NEW)
        cases = {'escape': bundle(NEW, escape), 'symlink': bundle(NEW, symlink),
                 'truncated': good[:len(good) // 2], 'not gzip': b'PK\x03\x04 not a tarball'}
        for name, archive in cases.items():
            self.github.publish([self.github.release('v1.0.0.20-production', NEW, archive=archive)])
            report = self.check()
            self.assertEqual(report['result'], 'error', (name, report))
            self.assertFalse(self.staged().exists(), name)
            self.assertFalse((self.tmp / 'work' / 'staged.new').exists(), name)
            self.assertFalse((self.tmp / 'work' / 'escaped.txt').exists(), name)
            self.assertFalse((self.tmp / 'escaped.txt').exists(), name)

    # ---- Installing and rolling back ----

    def snapshot(self, folder):
        return {str(p.relative_to(folder)): p.read_bytes() for p in sorted(folder.rglob('*')) if p.is_file()}

    def test_install_by_manifest_keeps_local_edits_and_user_files_then_rolls_back(self):
        self.github.publish([self.github.release('v1.0.0.20-production', NEW)])
        self.assertEqual(self.check()['result'], 'staged')
        install = self.tmp / 'install'
        (install / 'fonts').mkdir(parents=True)
        (install / 'maps').mkdir()
        old = {'haumea': b'old editor', 'fonts/a.ttf': b'old font', 'old_only.txt': b'gone in the new one'}
        for path, data in old.items():
            (install / path).write_bytes(data)
        (install / '.haumea-manifest').write_text(''.join(f'{sha(d)}  {p}\n' for p, d in old.items()))
        (install / 'fonts' / 'a.ttf').write_bytes(b'my own font')	# edited locally
        (install / 'maps' / 'my.pwn').write_bytes(b'my map')		# the user's, in no manifest
        (install / 'gta_sa.exe').write_bytes(b'the game')
        before = self.snapshot(install)

        report = self.run_editor('--update-install', str(self.staged()), str(install))
        self.assertEqual(report['result'], 'installed', report)
        self.assertEqual(report['kept_local'], ['fonts/a.ttf'])
        self.assertEqual((install / 'haumea').read_bytes(), b'new editor')
        self.assertEqual((install / 'fonts' / 'a.ttf').read_bytes(), b'new font')
        self.assertEqual((install / 'fonts' / 'a.ttf.local').read_bytes(), b'my own font')
        self.assertEqual((install / 'prefabs' / 'starter.haumea').read_bytes(), b'new prefab')
        self.assertFalse((install / 'old_only.txt').exists())
        self.assertEqual((install / 'maps' / 'my.pwn').read_bytes(), b'my map')
        self.assertEqual((install / 'gta_sa.exe').read_bytes(), b'the game')
        self.assertTrue(os.access(install / 'haumea', os.X_OK))

        report = self.run_editor('--update-rollback', str(install))
        self.assertEqual(report['result'], 'rolled_back', report)
        self.assertEqual(self.snapshot(install), before)
        self.assertFalse((install / '.haumea-previous').exists())
        report = self.run_editor('--update-rollback', str(install))
        self.assertEqual(report['result'], 'error', report)

    def test_install_refuses_a_staged_file_that_does_not_match(self):
        self.github.publish([self.github.release('v1.0.0.20-production', NEW)])
        self.assertEqual(self.check()['result'], 'staged')
        (self.staged() / 'fonts' / 'a.ttf').write_bytes(b'tampered')
        install = self.tmp / 'install'
        install.mkdir()
        (install / 'haumea').write_bytes(b'old editor')
        report = self.run_editor('--update-install', str(self.staged()), str(install))
        self.assertEqual(report['result'], 'error', report)
        self.assertEqual(self.snapshot(install), {'haumea': b'old editor'})

    # ---- The banner ----

    @unittest.skipUnless(shutil.which('xvfb-run'), 'needs xvfb-run')
    def test_banner_downloads_and_offers_restart(self):
        # A version far above this one, on Production (what every build but a
        # beta one follows by default).
        self.github.publish([self.github.release('v99.0.0.1-production', NEW)])
        game = self.tmp / 'game'
        subprocess.run([sys.executable, str(Path(__file__).parent / 'fixture' / 'make_game.py'), str(game)],
                       check=True, capture_output=True)
        server = self.tmp / 'server'
        (server / 'gamemodes').mkdir(parents=True)
        env = editor_env(str(self.tmp))
        env.pop('HAUMEA_NO_UPDATE', None)
        headless(env, str(self.tmp))
        env['HAUMEA_UPDATE_URL'] = self.github.url
        env['UPDATE_SHOT'] = str(self.tmp / 'banner.png')
        result = subprocess.run(['xvfb-run', '-a', '-s', '-screen 0 1600x1000x24', BINARY, '--game', str(game),
                                 '--workspace', str(server), '--ui-script',
                                 str(Path(__file__).parent / 'updater_banner.uis')],
                                cwd=self.tmp, env=env, capture_output=True, text=True, timeout=600)
        log = '\n'.join(l for l in result.stdout.splitlines() if l.startswith('[ui-script]'))
        self.assertEqual(result.returncode, 0, log or result.stderr[-2000:])
        staged = next(Path(env['XDG_CONFIG_HOME']).rglob('update/staged'), None)
        self.assertIsNotNone(staged, 'nothing was staged')
        self.assertEqual((staged / '.haumea-release').read_text().split()[0], '99.0.0.1')


if __name__ == '__main__':
    unittest.main()
