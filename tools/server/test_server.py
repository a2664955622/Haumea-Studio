# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for haumea_server.py on localhost: validation, the ping upsert, crash
grouping, the cross-version issue key and regressions, retention, the
dashboard's auth and escaping, no IP addresses stored, and the Discord
alerts (batched, with a 429 honoured) against a fake webhook.

    python3 -m unittest tools/server/test_server.py
"""
import base64
import hashlib
import http.server
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import haumea_server  # noqa: E402

FORWARDED_IP = '203.0.113.77'


def free_port():
	with socket.socket() as s:
		s.bind(('127.0.0.1', 0))
		return s.getsockname()[1]


class FakeWebhook(http.server.BaseHTTPRequestHandler):
	posts = []
	busy = 0	# answer 429 this many times first

	def log_message(self, *a):
		pass

	def do_POST(self):
		body = self.rfile.read(int(self.headers['Content-Length']))
		if FakeWebhook.busy > 0:
			FakeWebhook.busy -= 1
			data = b'{"retry_after": 0.2}'
			self.send_response(429)
			self.send_header('Content-Length', str(len(data)))
			self.end_headers()
			self.wfile.write(data)
			return
		FakeWebhook.posts.append(json.loads(body))
		self.send_response(204)
		self.end_headers()


# The build-id the fake release's BUILD_IDS.txt lists for linux-amd64.
BUILD_ID = '0123456789abcdef0123456789abcdef01234567'


def report(version='1.0.0.10', frames=None, kind='crash', rid=None, channel='production', **extra):
	frames = frames or [{'module': 'haumea', 'offset': '0x1a2b3c', 'symbol': ''},
	                    {'module': 'libc.so.6', 'offset': '0x2a1f0', 'symbol': ''},
	                    {'module': 'haumea', 'offset': '0x10'}]
	frames = [dict({'symbol': ''}, **f) for f in frames]
	r = {'schema': 1, 'id': rid or os.urandom(16).hex(), 'kind': kind, 'created': '2026-10-21T19:40:00Z',
	     'version': version, 'commit': 'abc1234', 'channel': channel, 'os': 'linux',
	     'os_version': 'Ubuntu 24.04', 'arch': 'x86_64', 'gl_renderer': 'llvmpipe', 'gl_version': '4.5',
	     'build_id': BUILD_ID, 'frames': frames, 'log': ['started', 'opened <WORKSPACE>/gamemodes/map.pwn'],
	     'context': {'mode': 'map', 'window': 'Object Info'}}
	if kind == 'crash':
		r['crash'] = {'signal': 'SIGSEGV', 'address': '0x0'}
		reason = 'SIGSEGV'
	else:
		r['error'] = {'message': 'could not open <GTA>/models/gta3.img', 'where': 'cdimage.cpp:120', 'count': 2}
		reason = 'cdimage.cpp:120'
	own = [f for f in frames if f['module'] in ('haumea', 'haumea.exe')][:5]
	r['signature'] = hashlib.sha1((kind + '\n' + reason + '\n' +
	                               ''.join(f"{f['module']}+{f['offset']}\n" for f in own)).encode()).hexdigest()
	r.update(extra)
	return r


class ServerTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.tmp = Path(tempfile.mkdtemp(prefix='haumea-server-'))
		cls.hook = http.server.ThreadingHTTPServer(('127.0.0.1', 0), FakeWebhook)
		threading.Thread(target=cls.hook.serve_forever, daemon=True).start()
		cls.symbols_dir = cls.tmp / 'symbols'
		cls.functions = cls.make_symbols()
		port = free_port()
		config = dict(haumea_server.DEFAULTS, HAUMEA_LISTEN=f'127.0.0.1:{port}', HAUMEA_DB=str(cls.tmp / 'db.sqlite'),
		              HAUMEA_DASH_PASSWORD='s3cret', HAUMEA_SYMBOLS_DIR=str(cls.symbols_dir),
		              HAUMEA_FETCH_SYMBOLS='0', HAUMEA_RATE_PINGS='1000', HAUMEA_RATE_REPORTS='1000',
		              DISCORD_WEBHOOK_URL=f'http://127.0.0.1:{cls.hook.server_address[1]}/api/webhooks/1/x',
		              HAUMEA_ALERT_SPIKE='5', HAUMEA_ALERT_EVERY='3600')
		cls.httpd, cls.service, cls.stop = haumea_server.serve(config)
		threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()
		cls.base = f'http://127.0.0.1:{port}'

	@classmethod
	def tearDownClass(cls):
		cls.stop.set()
		cls.httpd.shutdown()
		cls.hook.shutdown()
		shutil.rmtree(cls.tmp, ignore_errors=True)

	@classmethod
	def make_symbols(cls):
		"""A tiny PIE built with -g stands in for haumea.debug: two
		functions whose offsets the reports use."""
		if not shutil.which('cc') or not shutil.which('addr2line') or not shutil.which('nm'):
			return None
		src = cls.tmp / 'fake.c'
		src.write_text('int crash_here(int *p) { return *p; }\nint caller(void) { return crash_here(0); }\n'
		               'int main(void) { return caller(); }\n')
		exe = cls.tmp / 'haumea.debug'
		subprocess.run(['cc', '-g', '-O0', '-fPIE', '-pie', str(src), '-o', str(exe)], check=True)
		syms = {l.split()[2]: l.split()[0] for l in subprocess.run(['nm', str(exe)], capture_output=True,
		        text=True).stdout.splitlines() if len(l.split()) == 3}
		ids = f'linux-amd64 {BUILD_ID}\nwindows-gl3 {"A" * 33}\n'
		for version in ('1.0.0.20', '1.0.0.21', '1.0.0.22'):
			folder = cls.symbols_dir / version / 'linux-amd64'
			folder.mkdir(parents=True)
			shutil.copy(exe, folder / 'haumea.debug')
			(folder.parent / 'BUILD_IDS.txt').write_text(ids)
		# The same symbols without BUILD_IDS.txt: nothing says whose they are.
		folder = cls.symbols_dir / '1.0.0.23' / 'linux-amd64'
		folder.mkdir(parents=True)
		shutil.copy(exe, folder / 'haumea.debug')
		with zipfile.ZipFile(cls.tmp / 'haumea-symbols-1.0.0.20.zip', 'w') as z:
			z.write(exe, 'linux-amd64/haumea.debug')
			z.writestr('BUILD_IDS.txt', ids)
		return {name: '0x' + syms[name].lstrip('0') for name in ('crash_here', 'caller')}

	def post(self, path, body, content_type='application/json', headers=None):
		data = body if isinstance(body, bytes) else json.dumps(body).encode()
		req = urllib.request.Request(self.base + path, data=data, method='POST',
		                             headers=dict({'Content-Type': content_type, 'X-Forwarded-For': FORWARDED_IP},
		                                          **(headers or {})))
		try:
			with urllib.request.urlopen(req) as r:
				return r.status, r.read()
		except urllib.error.HTTPError as e:
			return e.code, e.read()

	def get(self, path, auth=True):
		headers = {'Authorization': 'Basic ' + base64.b64encode(b'admin:s3cret').decode()} if auth else {}
		try:
			with urllib.request.urlopen(urllib.request.Request(self.base + path, headers=headers)) as r:
				return r.status, r.read().decode()
		except urllib.error.HTTPError as e:
			return e.code, e.read().decode()

	def db(self, sql, args=()):
		return self.service.store.q(sql, args)

	# ---- pings ----

	def test_ping_upsert(self):
		uid = os.urandom(16).hex()
		ping = {'uid': uid, 'version': '1.0.0.57', 'channel': 'production', 'os': 'linux'}
		self.assertEqual(self.post('/haumea/telemetry/ping', ping)[0], 204)
		self.assertEqual(self.post('/haumea/telemetry/ping', dict(ping, version='1.0.0.58', os='windows'))[0], 204)
		rows = self.db('SELECT version, os FROM installs WHERE uid = ?', (uid,))
		self.assertEqual(rows, [('1.0.0.58', 'windows')])
		self.assertEqual(self.db('SELECT COUNT(*) FROM active_days WHERE uid = ?', (uid,))[0][0], 1)

	def test_bad_pings(self):
		good = {'uid': 'a' * 32, 'version': '1.0.0.57', 'channel': 'beta', 'os': 'macos'}
		for bad in (dict(good, uid='A' * 32), dict(good, uid='a' * 31), dict(good, channel='master'),
		            dict(good, os='solaris'), dict(good, version='1.0.0'), dict(good, extra='x'),
		            {'uid': 'a' * 32}, ['not', 'an', 'object']):
			code, body = self.post('/haumea/telemetry/ping', bad)
			self.assertEqual(code, 400, bad)
			self.assertNotIn(b'master', body)	# never echoed
		self.assertEqual(self.post('/haumea/telemetry/ping', b'{"uid": "' + b'a' * 2000 + b'"}')[0], 413)
		self.assertEqual(self.post('/haumea/telemetry/ping', good, content_type='text/plain')[0], 415)
		self.assertEqual(self.post('/haumea/telemetry/ping', b'{nope')[0], 400)

	def test_rate_limit_in_memory(self):
		old = self.service.config['HAUMEA_RATE_PINGS']
		self.service.config['HAUMEA_RATE_PINGS'] = '3'
		try:
			ping = {'uid': 'b' * 32, 'version': '1.0.0.57', 'channel': 'production', 'os': 'linux'}
			codes = [self.post('/haumea/telemetry/ping', ping, headers={'X-Forwarded-For': '198.51.100.9'})[0]
			         for _ in range(5)]
			self.assertEqual(codes[:3], [204] * 3)
			self.assertEqual(codes[3:], [429] * 2)
		finally:
			self.service.config['HAUMEA_RATE_PINGS'] = old

	def test_no_ip_addresses_stored(self):
		self.post('/haumea/telemetry/ping', {'uid': 'c' * 32, 'version': '1.0.0.57', 'channel': 'dev', 'os': 'linux'})
		self.post('/haumea/reports', report())
		self.service.store.q('PRAGMA wal_checkpoint(FULL)')
		blob = b''.join(p.read_bytes() for p in self.tmp.glob('db.sqlite*'))
		for ip in (FORWARDED_IP, '198.51.100.9', '127.0.0.1'):
			self.assertNotIn(ip.encode(), blob)

	# ---- reports ----

	def test_grouping(self):
		frames = [{'module': 'haumea', 'offset': '0xdead00'}, {'module': 'haumea', 'offset': '0xbeef'}]
		a, b = report(frames=frames), report(frames=frames)
		self.assertEqual(self.post('/haumea/reports', a)[0], 204)
		self.assertEqual(self.post('/haumea/reports', b)[0], 204)
		self.assertEqual(self.post('/haumea/reports', a)[0], 204)	# a resend counts once
		rows = self.db('SELECT count FROM crash_groups WHERE signature = ? AND version = ?',
		               (a['signature'], a['version']))
		self.assertEqual(rows, [(2,)])
		# The same crash in another version is another group (offsets differ per build).
		self.post('/haumea/reports', report(version='1.0.0.11', frames=frames))
		self.assertEqual(self.db('SELECT COUNT(*) FROM crash_groups WHERE signature = ?', (a['signature'],))[0][0], 2)
		# Errors group by where they happened.
		e = report(kind='error', frames=[{'module': 'haumea', 'offset': '0x55'}])
		self.assertEqual(self.post('/haumea/reports', e)[0], 204)

	def test_bad_reports(self):
		r = report()
		cases = [dict(r, signature='0' * 40), dict(r, schema=2), dict(r, kind='hang'), dict(r, frames=[]),
		         dict(r, frames=[{'module': 'haumea', 'offset': 'x'}] * 2), dict(r, version='1.0'),
		         dict(r, log=['x'] * 201), dict(r, crash={'signal': 'SIGKILL'}), dict(r, uid='nope')]
		for bad in cases:
			self.assertEqual(self.post('/haumea/reports', bad)[0], 400)
		big = dict(r, log=['x' * 300] * 200, id=os.urandom(16).hex())
		big['frames'] = big['frames'] * 21
		self.assertEqual(self.post('/haumea/reports', json.dumps(big).encode() + b' ' * 300000)[0], 413)

	def test_fields_are_capped(self):
		r = report(gl_renderer='G' * 5000)
		self.assertEqual(self.post('/haumea/reports', r)[0], 204)
		self.assertEqual(self.db('SELECT LENGTH(gl_renderer) FROM reports WHERE id = ?', (r['id'],))[0][0], 200)

	# ---- dashboard ----

	def test_dashboard_auth_and_escaping(self):
		self.assertEqual(self.get('/dashboard', auth=False)[0], 401)
		req = urllib.request.Request(self.base + '/dashboard',
		                             headers={'Authorization': 'Basic ' + base64.b64encode(b'admin:wrong').decode()})
		with self.assertRaises(urllib.error.HTTPError) as caught:
			urllib.request.urlopen(req)
		self.assertEqual(caught.exception.code, 401)
		evil = report(kind='error', frames=[{'module': 'haumea', 'offset': '0x77'}])
		evil['error']['message'] = '<script>alert(1)</script>'
		self.assertEqual(self.post('/haumea/reports', evil)[0], 204)
		code, page = self.get('/dashboard')
		self.assertEqual(code, 200)
		self.assertNotIn('<script>alert(1)</script>', page)
		self.assertIn('&lt;script&gt;', page)
		gid = self.db('SELECT group_id FROM reports WHERE id = ?', (evil['id'],))[0][0]
		code, page = self.get(f'/dashboard/group/{gid}')
		self.assertEqual(code, 200)
		self.assertNotIn('<script>alert(1)</script>', page)
		code, page = self.get(f"/dashboard/report/{evil['id']}")
		self.assertEqual(code, 200)
		self.assertIn('Object Info', page)
		self.assertEqual(self.get('/dashboard/report/../../etc/passwd')[0], 404)

	def test_privacy_page(self):
		code, page = self.get('/privacy', auth=False)
		self.assertEqual(code, 200)
		self.assertIn('IP addresses', page)

	# ---- retention ----

	def test_retention(self):
		old_day, old = '2020-01-01', '2020-01-01T00:00:00Z'
		self.db("INSERT INTO installs VALUES ('d' || hex(randomblob(15)), '1.0.0.1', 'production', 'linux', ?, ?)",
		        (old, old))
		self.db("INSERT INTO active_days VALUES (?, 'e' || hex(randomblob(15)), '1.0.0.1', 'production', 'linux')",
		        (old_day,))
		r = report()
		self.post('/haumea/reports', r)
		self.db('UPDATE reports SET received = ? WHERE id = ?', (old, r['id']))
		self.service.cleanup()
		self.assertEqual(self.db('SELECT COUNT(*) FROM installs WHERE last_seen = ?', (old,))[0][0], 0)
		self.assertEqual(self.db('SELECT COUNT(*) FROM active_days WHERE day = ?', (old_day,))[0][0], 0)
		self.assertEqual(self.db('SELECT COUNT(*) FROM reports WHERE id = ?', (r['id'],))[0][0], 0)

	# ---- alerts ----

	def test_alerts_batched_with_429(self):
		self.service.alerter.flush()
		FakeWebhook.posts.clear()
		frames = [{'module': 'haumea', 'offset': '0x4242'}]
		for _ in range(6):	# a new group, then a spike (limit 5)
			self.assertEqual(self.post('/haumea/reports', report(version='1.0.0.30', frames=frames))[0], 204)
		FakeWebhook.busy = 1
		self.assertTrue(self.service.alerter.flush())
		self.assertEqual(len(FakeWebhook.posts), 1)	# one message for the storm
		titles = [e['title'] for e in FakeWebhook.posts[0]['embeds']]
		self.assertTrue(any(t.startswith('New crash group') for t in titles), titles)
		self.assertTrue(any(t.startswith('Crash spike') for t in titles), titles)
		text = json.dumps(FakeWebhook.posts[0])
		self.assertNotIn('<WORKSPACE>', text)	# no log lines in Discord
		self.assertTrue(self.service.alerter.flush())	# nothing new: no message
		self.assertEqual(len(FakeWebhook.posts), 1)

	@unittest.skipUnless(shutil.which('cc') and shutil.which('addr2line'), 'needs cc and addr2line')
	def test_issue_key_and_regression(self):
		self.service.alerter.flush()
		FakeWebhook.posts.clear()
		f = self.functions
		frames = [{'module': 'haumea', 'offset': f['crash_here']}, {'module': 'haumea', 'offset': f['caller']}]
		first = report(version='1.0.0.20', frames=frames)
		self.post('/haumea/reports', first)
		self.service.linking.join()
		gid, key = self.db('SELECT id, issue_key FROM crash_groups WHERE version = ? AND signature = ?',
		                   ('1.0.0.20', first['signature']))[0]
		self.assertTrue(key)
		# Mark it fixed in 1.0.0.21 through the dashboard form.
		req = urllib.request.Request(self.base + f'/dashboard/group/{gid}/fixed', data=b'version=1.0.0.21',
		                             method='POST', headers={
		                                 'Authorization': 'Basic ' + base64.b64encode(b'admin:s3cret').decode(),
		                                 'Content-Type': 'application/x-www-form-urlencoded'})
		try:
			urllib.request.urlopen(req)
		except urllib.error.HTTPError as e:
			self.assertIn(e.code, (303, 200))
		self.assertEqual(self.db('SELECT fixed_in FROM crash_groups WHERE id = ?', (gid,))[0][0], '1.0.0.21')
		# The same functions crash again in 1.0.0.22: linked, and a regression.
		again = report(version='1.0.0.22', frames=frames)
		self.post('/haumea/reports', again)
		self.service.linking.join()
		later = self.db('SELECT issue_key FROM crash_groups WHERE version = ?', ('1.0.0.22',))[0][0]
		self.assertEqual(later, key)
		kinds = [k for (k,) in self.db("SELECT kind FROM alerts WHERE sent = 0")]
		self.assertIn('regression', kinds)
		self.service.alerter.flush()
		self.assertTrue(any(e['title'].startswith('Regression') for p in FakeWebhook.posts for e in p['embeds']))
		code, page = self.get(f'/dashboard/group/{gid}')
		self.assertIn('1.0.0.22', page)	# the same issue in another version

	@unittest.skipUnless(shutil.which('cc') and shutil.which('addr2line'), 'needs cc and addr2line')
	def test_names_only_for_the_release_binary(self):
		# Names come only for the binary BUILD_IDS.txt lists; another build
		# (a dev build, the companion's MinGW engine, no build_id, a zip
		# without the file) keeps its offsets rather than get wrong names.
		f = self.functions
		frames = [{'module': 'haumea', 'offset': f['crash_here']}, {'module': 'haumea', 'offset': f['caller']}]
		cache = self.service.symbols
		good = haumea_server.validate_report(report(version='1.0.0.20', frames=frames))
		self.assertIn('crash_here', cache.symbolize(good)[0])
		self.assertEqual(cache.function_names(good)[:1], ['crash_here'])
		for other in (dict(build_id='f' * 40), dict(build_id=''), dict(os='windows')):
			r = haumea_server.validate_report(report(version='1.0.0.20', frames=frames, **other))
			self.assertIsNone(cache.symbolize(r), other)
			self.assertEqual(cache.function_names(r), [], other)
		unlisted = haumea_server.validate_report(report(version='1.0.0.23', frames=frames))
		self.assertIsNone(cache.symbolize(unlisted))

	@unittest.skipUnless(shutil.which('cc') and shutil.which('addr2line'), 'needs cc and addr2line')
	def test_symbolize_cli(self):
		f = self.functions
		r = report(version='1.0.0.20', frames=[{'module': 'haumea', 'offset': f['crash_here']},
		                                       {'module': 'libc.so.6', 'offset': '0x2a1f0'}])
		path = self.tmp / 'report.json'
		path.write_text(json.dumps(r))
		out = subprocess.run([sys.executable, str(HERE / 'symbolize.py'), str(path),
		                      str(self.tmp / 'haumea-symbols-1.0.0.20.zip')], capture_output=True, text=True)
		self.assertEqual(out.returncode, 0, out.stderr)
		self.assertIn('crash_here', out.stdout)
		self.assertIn('fake.c:1', out.stdout)
		# Another build's report is refused unless asked.
		path.write_text(json.dumps(dict(r, build_id='f' * 40)))
		args = [sys.executable, str(HERE / 'symbolize.py'), str(path), str(self.tmp / 'haumea-symbols-1.0.0.20.zip')]
		out = subprocess.run(args, capture_output=True, text=True)
		self.assertEqual(out.returncode, 1)
		self.assertIn('--any-build', out.stderr)
		out = subprocess.run(args + ['--any-build'], capture_output=True, text=True)
		self.assertEqual(out.returncode, 0, out.stderr)
		self.assertIn('crash_here', out.stdout)


# ---- Cloudflare Access ----------------------------------------------------------------

def _probable_prime(bits, rng):
	while True:
		n = rng.getrandbits(bits) | (1 << (bits - 1)) | 1
		d, r = n - 1, 0
		while d % 2 == 0:
			d, r = d // 2, r + 1
		for _ in range(24):
			x = pow(rng.randrange(2, n - 2), d, n)
			if x in (1, n - 1):
				continue
			for _ in range(r - 1):
				x = pow(x, 2, n)
				if x == n - 1:
					break
			else:
				break
		else:
			return n


def rsa_key(bits=1024, seed=7):
	"""An RSA key for signing test tokens (no crypto library needed)."""
	import random
	rng = random.Random(seed)
	e = 65537
	while True:
		p, q = _probable_prime(bits // 2, rng), _probable_prime(bits // 2, rng)
		phi = (p - 1) * (q - 1)
		if p != q and phi % e:
			return p * q, e, pow(e, -1, phi)


def b64url(data):
	return base64.urlsafe_b64encode(data).rstrip(b'=').decode()


def token(key, claims, kid='k1', alg='RS256'):
	n, e, d = key
	head = b64url(json.dumps({'alg': alg, 'kid': kid}).encode())
	body = b64url(json.dumps(claims).encode())
	size = (n.bit_length() + 7) // 8
	digest = bytes.fromhex('3031300d060960864801650304020105000420') + \
		hashlib.sha256(f'{head}.{body}'.encode()).digest()
	em = b'\x00\x01' + b'\xff' * (size - len(digest) - 3) + b'\x00' + digest
	sig = pow(int.from_bytes(em, 'big'), d, n).to_bytes(size, 'big')
	return f'{head}.{body}.{b64url(sig)}'


class AccessTests(unittest.TestCase):
	TEAM, AUD = 'haumea-test', 'a' * 64

	@classmethod
	def setUpClass(cls):
		cls.tmp = Path(tempfile.mkdtemp(prefix='haumea-access-'))
		cls.key = rsa_key()
		n, e, _ = cls.key
		cls.jwks = {'keys': [{'kty': 'RSA', 'kid': 'k1', 'alg': 'RS256',
		                      'n': b64url(n.to_bytes((n.bit_length() + 7) // 8, 'big')),
		                      'e': b64url(e.to_bytes(3, 'big'))}]}
		port = free_port()
		config = dict(haumea_server.DEFAULTS, HAUMEA_LISTEN=f'127.0.0.1:{port}', HAUMEA_DB=str(cls.tmp / 'db.sqlite'),
		              HAUMEA_DASH_PASSWORD='s3cret', HAUMEA_SYMBOLS_DIR=str(cls.tmp / 'symbols'),
		              HAUMEA_FETCH_SYMBOLS='0', HAUMEA_ACCESS_TEAM=cls.TEAM, HAUMEA_ACCESS_AUD=cls.AUD)
		cls.httpd, cls.service, cls.stop = haumea_server.serve(config)
		cls.service.access.fetch = lambda: cls.jwks	# never Cloudflare's real keys
		threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()
		cls.base = f'http://127.0.0.1:{port}'

	@classmethod
	def tearDownClass(cls):
		cls.stop.set()
		cls.httpd.shutdown()
		shutil.rmtree(cls.tmp, ignore_errors=True)

	def claims(self, **change):
		t = int(time.time())
		return dict({'iss': f'https://{self.TEAM}.cloudflareaccess.com', 'aud': [self.AUD], 'email': 'dev@example.com',
		             'iat': t - 5, 'nbf': t - 5, 'exp': t + 600}, **change)

	def get(self, path, headers=None):
		try:
			with urllib.request.urlopen(urllib.request.Request(self.base + path, headers=headers or {})) as r:
				return r.status, r.read().decode()
		except urllib.error.HTTPError as e:
			return e.code, e.read().decode()

	def test_a_signed_request_gets_in(self):
		code, page = self.get('/dashboard', {'Cf-Access-Jwt-Assertion': token(self.key, self.claims())})
		self.assertEqual(code, 200)
		self.assertIn('dev@example.com', page)
		self.assertIn('Active installs per day', page)
		# The browser's cookie works as well as Cloudflare's header.
		code, _ = self.get('/dashboard?days=7&show=all', {'Cookie': 'CF_Authorization=' + token(self.key, self.claims())})
		self.assertEqual(code, 200)

	def test_everything_else_is_refused(self):
		good = token(self.key, self.claims())
		head, body, sig = good.split('.')
		forged = rsa_key(seed=8)
		refused = {
			'no token': {},
			'the password': {'Authorization': 'Basic ' + base64.b64encode(b'admin:s3cret').decode()},
			'another key': {'Cf-Access-Jwt-Assertion': token(forged, self.claims())},
			'changed claims': {'Cf-Access-Jwt-Assertion': '.'.join(
				(head, b64url(json.dumps(self.claims(email='evil@example.com')).encode()), sig))},
			'wrong audience': {'Cf-Access-Jwt-Assertion': token(self.key, self.claims(aud=['b' * 64]))},
			'wrong team': {'Cf-Access-Jwt-Assertion': token(self.key, self.claims(iss='https://other.cloudflareaccess.com'))},
			'expired': {'Cf-Access-Jwt-Assertion': token(self.key, self.claims(exp=int(time.time()) - 3600))},
			'not yet valid': {'Cf-Access-Jwt-Assertion': token(self.key, self.claims(nbf=int(time.time()) + 3600))},
			'unknown key': {'Cf-Access-Jwt-Assertion': token(self.key, self.claims(), kid='k2')},
			'alg none': {'Cf-Access-Jwt-Assertion': token(self.key, self.claims(), alg='none')},
			'garbage': {'Cf-Access-Jwt-Assertion': 'a.b.c'},
		}
		for what, headers in refused.items():
			self.assertEqual(self.get('/dashboard', headers)[0], 403, what)
		# The public routes need no login.
		self.assertEqual(self.get('/privacy')[0], 200)

	def test_team_names(self):
		for team in ('itsneufox', 'itsneufox.cloudflareaccess.com', 'https://itsneufox.cloudflareaccess.com/'):
			self.assertEqual(haumea_server.access.team_issuer(team), 'https://itsneufox.cloudflareaccess.com')


if __name__ == '__main__':
	unittest.main()
