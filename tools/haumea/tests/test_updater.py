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


def headless(env):
    """Never the user's desktop: no X display, and a Wayland socket that can't
    exist (libwayland would otherwise fall back to wayland-0 in
    XDG_RUNTIME_DIR, and GLFW prefers Wayland). xvfb-run then sets DISPLAY."""
    for var in ('DISPLAY', 'XAUTHORITY', 'WAYLAND_SOCKET'):
        env.pop(var, None)
    env['WAYLAND_DISPLAY'] = '/nonexistent/haumea-test'
    env['XDG_SESSION_TYPE'] = 'x11'
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

    def run_editor(self, *args, current='1.41.0.10', url=None, skip=None):
        env = editor_env(str(self.tmp))
        env.pop('HAUMEA_NO_UPDATE', None)
        headless(env)
        env['HAUMEA_UPDATE_URL'] = url or self.github.url
        env['HAUMEA_UPDATE_CHANNEL'] = 'master'
        env['HAUMEA_UPDATE_CURRENT'] = current
        if skip:
            env['HAUMEA_UPDATE_SKIP'] = skip
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
        self.github.publish([self.github.release('v1.41.0.20-master', NEW),
                             self.github.release('v1.41.0.15-master', NEW)])
        report = self.check()
        self.assertEqual(report['result'], 'staged', report)
        self.assertEqual(report['version'], '1.41.0.20')
        self.assertEqual((self.staged() / 'haumea').read_bytes(), b'new editor')
        self.assertTrue((self.staged() / 'MANIFEST').exists())
        self.assertEqual((self.staged() / '.haumea-notes').read_text(), 'Notes for v1.41.0.20-master')

    def test_same_older_skipped_and_other_channel_are_up_to_date(self):
        self.github.publish([self.github.release('v1.41.0.20-master', NEW),
                             self.github.release('v1.42.0.30-PE', NEW)])
        for current, skip in (('1.41.0.20', None), ('1.42.0.0', None), ('1.41.0.10', '1.41.0.20')):
            report = self.check(current=current, skip=skip)
            self.assertEqual(report['result'], 'up_to_date', (current, skip, report))
        self.assertFalse(self.staged().exists())

    def test_bad_checksum_stages_nothing(self):
        self.github.publish([self.github.release('v1.41.0.20-master', NEW, sums='0' * 64 + f'  {BUNDLE}\n')])
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
        tag, assets = self.github.release('v1.41.0.20-master', NEW)
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
            self.github.publish([self.github.release('v1.41.0.20-master', NEW, archive=archive)])
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
        self.github.publish([self.github.release('v1.41.0.20-master', NEW)])
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
        self.github.publish([self.github.release('v1.41.0.20-master', NEW)])
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
        # The editor's own channel and a version far above it.
        channel = subprocess.run([BINARY, '--version'], capture_output=True, text=True).stdout.split()[-1]
        self.github.publish([self.github.release(f'v99.0.0.1-{channel}', NEW)])
        game = self.tmp / 'game'
        subprocess.run([sys.executable, str(Path(__file__).parent / 'fixture' / 'make_game.py'), str(game)],
                       check=True, capture_output=True)
        server = self.tmp / 'server'
        (server / 'gamemodes').mkdir(parents=True)
        env = editor_env(str(self.tmp))
        env.pop('HAUMEA_NO_UPDATE', None)
        headless(env)
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
