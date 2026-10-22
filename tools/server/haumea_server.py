#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""haumea_server.py: receives Haumea Studio's telemetry pings and crash
reports, keeps them in SQLite, alerts on Discord and shows a private
dashboard. Python 3 standard library only.

    haumea_server.py [--config /etc/haumea-server/env]

It listens on 127.0.0.1 behind Caddy, which serves telemetry.itsneufox.xyz
and reports.itsneufox.xyz over HTTPS. Routes:

    POST /haumea/telemetry/ping   a ping (JSON, under 1 KB): 204
    POST /haumea/reports          a crash or error report (JSON, up to 256 KB): 204
    GET  /privacy                 what is collected and kept
    GET  /dashboard/...           the dashboard, behind HTTP basic auth

No IP address is ever stored: rate limits per IP live in memory only. Every
field of a ping or report is treated as untrusted text: validated, capped and
escaped when shown. Configuration comes from the environment or an env file
(KEY=value lines); see DEPLOY.md.
"""
import argparse
import base64
import collections
import datetime as dt
import hashlib
import hmac
import html
import http.server
import json
import os
import queue
import re
import socketserver
import sqlite3
import sys
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import symbols  # noqa: E402

PING_MAX = 1024
REPORT_MAX = 256 * 1024
UID = re.compile(r'[0-9a-f]{32}')
HEX40 = re.compile(r'[0-9a-f]{40}')
VERSION = re.compile(r'\d{1,4}\.\d{1,4}\.\d{1,4}\.\d{1,9}')
CHANNELS = ('production', 'beta', 'dev', 'pr')
OSES = ('linux', 'windows', 'macos')
SIGNALS = ('SIGSEGV', 'SIGABRT', 'SIGBUS', 'SIGFPE', 'SIGILL')
DEFAULTS = {
	'HAUMEA_LISTEN': '127.0.0.1:8787',
	'HAUMEA_DB': '/var/lib/haumea-server/haumea.db',
	'HAUMEA_DASH_USER': 'admin',
	'HAUMEA_DASH_PASSWORD': '',
	'HAUMEA_PUBLIC_URL': 'https://reports.itsneufox.xyz',
	'HAUMEA_SYMBOLS_DIR': '/var/lib/haumea-server/symbols',
	'HAUMEA_RELEASES': 'https://github.com/itsneufox/Haumea-Studio/releases/download',
	'HAUMEA_FETCH_SYMBOLS': '1',
	'HAUMEA_RATE_PINGS': '30',	# per IP per minute
	'HAUMEA_RATE_REPORTS': '10',
	'HAUMEA_TRUST_PROXY': '1',	# X-Forwarded-For from 127.0.0.1 (Caddy)
	'DISCORD_WEBHOOK_URL': '',
	'HAUMEA_ALERT_SPIKE': '10',	# reports of one group in an hour
	'HAUMEA_ALERT_EVERY': '60',	# seconds between alert messages
	'HAUMEA_DAILY_SUMMARY': '0',	# 1: a summary every day at 09:00 UTC
	'HAUMEA_PING_DAYS': '365',
	'HAUMEA_REPORT_DAYS': '183',
}


def load_config(path=None):
	config = dict(DEFAULTS)
	if path and Path(path).is_file():
		for line in Path(path).read_text().splitlines():
			line = line.strip()
			if line and not line.startswith('#') and '=' in line:
				key, value = line.split('=', 1)
				config[key.strip()] = value.strip().strip('"').strip("'")
	for key in DEFAULTS:
		if key in os.environ:
			config[key] = os.environ[key]
	return config


def now():
	return dt.datetime.now(dt.timezone.utc)


def iso(t):
	return t.strftime('%Y-%m-%dT%H:%M:%SZ')


def version_key(v):
	try:
		return tuple(int(x) for x in v.split('.'))
	except ValueError:
		return (0,)


# ---- storage ---------------------------------------------------------------

SCHEMA = '''
CREATE TABLE IF NOT EXISTS installs (
	uid TEXT PRIMARY KEY, version TEXT, channel TEXT, os TEXT,
	first_seen TEXT, last_seen TEXT);
CREATE TABLE IF NOT EXISTS active_days (
	day TEXT, uid TEXT, version TEXT, channel TEXT, os TEXT, PRIMARY KEY (day, uid));
CREATE TABLE IF NOT EXISTS crash_groups (
	id INTEGER PRIMARY KEY, version TEXT, signature TEXT, kind TEXT, title TEXT,
	top_frames TEXT, issue_key TEXT, fixed_in TEXT, count INTEGER DEFAULT 0,
	first_seen TEXT, last_seen TEXT, UNIQUE (version, signature));
CREATE TABLE IF NOT EXISTS reports (
	id TEXT PRIMARY KEY, group_id INTEGER, received TEXT, created TEXT, kind TEXT,
	version TEXT, commit_id TEXT, channel TEXT, os TEXT, os_version TEXT, arch TEXT,
	gl_renderer TEXT, gl_version TEXT, build_id TEXT, signature TEXT, body TEXT);
CREATE INDEX IF NOT EXISTS reports_group ON reports (group_id, received);
CREATE INDEX IF NOT EXISTS groups_issue ON crash_groups (issue_key);
CREATE TABLE IF NOT EXISTS alerts (
	id INTEGER PRIMARY KEY, created TEXT, kind TEXT, group_id INTEGER, detail TEXT,
	sent INTEGER DEFAULT 0);
CREATE TABLE IF NOT EXISTS state (key TEXT PRIMARY KEY, value TEXT);
'''


class Store:
	def __init__(self, path):
		Path(path).parent.mkdir(parents=True, exist_ok=True)
		self.db = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
		self.db.execute('PRAGMA journal_mode=WAL')
		self.db.executescript(SCHEMA)
		self.lock = threading.Lock()

	def q(self, sql, args=()):
		with self.lock:
			return self.db.execute(sql, args).fetchall()

	def tx(self, fn):
		with self.lock:
			self.db.execute('BEGIN IMMEDIATE')
			try:
				result = fn(self.db)
				self.db.execute('COMMIT')
				return result
			except BaseException:
				self.db.execute('ROLLBACK')
				raise


# ---- validation ----------------------------------------------------------------

class Bad(Exception):
	pass


def text(value, limit, name, required=True):
	if value is None and not required:
		return ''
	if not isinstance(value, str):
		raise Bad(name)
	value = value.replace('\x00', '')
	return value[:limit]


def choice(value, allowed, name):
	if value not in allowed:
		raise Bad(name)
	return value


def match(value, pattern, name):
	if not isinstance(value, str) or not pattern.fullmatch(value):
		raise Bad(name)
	return value


def parse_json(raw):
	try:
		data = json.loads(raw.decode('utf-8'))
	except (UnicodeDecodeError, ValueError):
		raise Bad('json')
	if not isinstance(data, dict):
		raise Bad('json')
	return data


def validate_ping(data):
	if set(data) - {'uid', 'version', 'channel', 'os'}:
		raise Bad('fields')
	return {'uid': match(data.get('uid'), UID, 'uid'),
	        'version': match(data.get('version'), VERSION, 'version'),
	        'channel': choice(data.get('channel'), CHANNELS, 'channel'),
	        'os': choice(data.get('os'), OSES, 'os')}


def validate_report(data):
	if data.get('schema') != 1:
		raise Bad('schema')
	r = {'id': match(data.get('id'), UID, 'id'),
	     'kind': choice(data.get('kind'), ('crash', 'error'), 'kind'),
	     'created': text(data.get('created'), 32, 'created'),
	     'version': match(data.get('version'), VERSION, 'version'),
	     'commit': text(data.get('commit'), 48, 'commit'),
	     'channel': choice(data.get('channel'), CHANNELS, 'channel'),
	     'os': choice(data.get('os'), OSES, 'os'),
	     'os_version': text(data.get('os_version'), 120, 'os_version'),
	     'arch': choice(data.get('arch'), ('x86_64', 'arm64'), 'arch'),
	     'gl_renderer': text(data.get('gl_renderer'), 200, 'gl_renderer'),
	     'gl_version': text(data.get('gl_version'), 200, 'gl_version'),
	     'signature': match(data.get('signature'), HEX40, 'signature'),
	     'build_id': text(data.get('build_id'), 128, 'build_id', required=False)}
	if r['kind'] == 'crash':
		c = data.get('crash')
		if not isinstance(c, dict):
			raise Bad('crash')
		if 'signal' in c:
			r['reason'] = choice(c['signal'], SIGNALS, 'signal')
		else:
			r['reason'] = match(c.get('code'), re.compile(r'0x[0-9A-Fa-f]{1,8}'), 'code')
		r['address'] = match(c.get('address', '0x0'), re.compile(r'0x[0-9A-Fa-f]{1,16}'), 'address')
	else:
		e = data.get('error')
		if not isinstance(e, dict):
			raise Bad('error')
		r['message'] = text(e.get('message'), 1000, 'message')
		r['reason'] = text(e.get('where'), 200, 'where')
		count = e.get('count', 1)
		if not isinstance(count, int) or isinstance(count, bool) or count < 1:
			raise Bad('count')
		r['count'] = min(count, 10**9)
	frames = data.get('frames')
	if not isinstance(frames, list) or not frames or len(frames) > 64:
		raise Bad('frames')
	r['frames'] = []
	for f in frames:
		if not isinstance(f, dict):
			raise Bad('frame')
		r['frames'].append({'module': text(f.get('module'), 128, 'module'),
		                    'offset': match(f.get('offset'), re.compile(r'0x[0-9a-fA-F]{1,16}'), 'offset'),
		                    'symbol': text(f.get('symbol'), 256, 'symbol', required=False)})
	log = data.get('log', [])
	if not isinstance(log, list) or len(log) > 200:
		raise Bad('log')
	r['log'] = [text(line, 300, 'log') for line in log]
	ctx = data.get('context', {})
	if not isinstance(ctx, dict):
		raise Bad('context')
	r['context'] = {'mode': text(ctx.get('mode', ''), 16, 'mode', required=False),
	                'window': text(ctx.get('window', ''), 120, 'window', required=False)}
	if 'uid' in data:
		r['uid'] = match(data['uid'], UID, 'uid')
	return r


def signature_of(r):
	"""The client's signature, recomputed: sha1 of kind, reason and the first
	five haumea frames as module+offset."""
	own = [f for f in r['frames'] if f['module'] in ('haumea', 'haumea.exe')][:5]
	text_ = r['kind'] + '\n' + r['reason'] + '\n' + ''.join(f"{f['module']}+{f['offset']}\n" for f in own)
	return hashlib.sha1(text_.encode()).hexdigest()


def group_title(r):
	if r['kind'] == 'crash':
		own = next((f for f in r['frames'] if f['module'] in ('haumea', 'haumea.exe')), r['frames'][0])
		where = own['symbol'] or f"{own['module']}+{own['offset']}"
		return f"{r['reason']} in {where}"
	return f"{r['reason']}: {r.get('message', '')[:80]}"


# ---- ingest ----------------------------------------------------------------------

class Service:
	def __init__(self, config):
		self.config = config
		self.store = Store(config['HAUMEA_DB'])
		self.limits = collections.defaultdict(collections.deque)
		self.limits_lock = threading.Lock()
		self.symbols = symbols.SymbolCache(config['HAUMEA_SYMBOLS_DIR'], config['HAUMEA_RELEASES'],
		                                   fetch=config['HAUMEA_FETCH_SYMBOLS'] == '1')
		self.alerter = Alerter(self)
		# Symbols may need a download, so issues are linked off the request.
		self.linking = queue.Queue()

	def allowed(self, ip, kind):
		"""Per-IP rate limit, in memory only: a sliding minute."""
		limit = int(self.config['HAUMEA_RATE_PINGS' if kind == 'ping' else 'HAUMEA_RATE_REPORTS'])
		key = (kind, ip)
		t = time.monotonic()
		with self.limits_lock:
			q = self.limits[key]
			while q and t - q[0] > 60:
				q.popleft()
			if len(q) >= limit:
				return False
			q.append(t)
			if len(self.limits) > 100000:	# keep memory bounded
				for k in [k for k, v in self.limits.items() if not v][:50000]:
					del self.limits[k]
			return True

	def ping(self, p):
		t = now()
		stamp, day = iso(t), t.strftime('%Y-%m-%d')

		def write(db):
			db.execute('INSERT INTO installs (uid, version, channel, os, first_seen, last_seen) '
			           'VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT (uid) DO UPDATE SET version = excluded.version, '
			           'channel = excluded.channel, os = excluded.os, last_seen = excluded.last_seen',
			           (p['uid'], p['version'], p['channel'], p['os'], stamp, stamp))
			db.execute('INSERT OR IGNORE INTO active_days (day, uid, version, channel, os) VALUES (?, ?, ?, ?, ?)',
			           (day, p['uid'], p['version'], p['channel'], p['os']))
		self.store.tx(write)

	def report(self, r):
		if signature_of(r) != r['signature']:
			raise Bad('signature')
		t = iso(now())
		body = json.dumps(r, separators=(',', ':'))

		def write(db):
			if db.execute('SELECT 1 FROM reports WHERE id = ?', (r['id'],)).fetchone():
				return None	# a resend
			row = db.execute('SELECT id, count FROM crash_groups WHERE version = ? AND signature = ?',
			                 (r['version'], r['signature'])).fetchone()
			new = row is None
			if new:
				top = [f"{f['module']}+{f['offset']}" + (f" {f['symbol']}" if f['symbol'] else '')
				       for f in r['frames'][:3]]
				cur = db.execute('INSERT INTO crash_groups (version, signature, kind, title, top_frames, count, '
				                 'first_seen, last_seen) VALUES (?, ?, ?, ?, ?, 1, ?, ?)',
				                 (r['version'], r['signature'], r['kind'], group_title(r), json.dumps(top), t, t))
				group = cur.lastrowid
			else:
				group = row[0]
				db.execute('UPDATE crash_groups SET count = count + 1, last_seen = ? WHERE id = ?', (t, group))
			db.execute('INSERT INTO reports (id, group_id, received, created, kind, version, commit_id, channel, '
			           'os, os_version, arch, gl_renderer, gl_version, build_id, signature, body) '
			           'VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)',
			           (r['id'], group, t, r['created'], r['kind'], r['version'], r['commit'], r['channel'],
			            r['os'], r['os_version'], r['arch'], r['gl_renderer'], r['gl_version'],
			            r['build_id'], r['signature'], body))
			return group, new
		result = self.store.tx(write)
		if result is None:
			return
		group, new = result
		self.linking.put((group, r))
		if new:
			self.alerter.queue('new', group)
		self.alerter.check_spike(group)

	def link_worker(self, stop):
		while not stop.is_set():
			try:
				group, r = self.linking.get(timeout=1)
			except queue.Empty:
				continue
			try:
				self.link_issue(group, r)
			except Exception as e:	# a missing or bad symbols zip: the group stays alone
				print(f'symbols: {type(e).__name__}: {e}', file=sys.stderr)
			finally:
				self.linking.task_done()

	def link_issue(self, group, r):
		"""The cross-version issue key: the first five haumea frames'
		function names, once the release's symbols are there."""
		names = self.symbols.function_names(r)
		if not names:
			return
		key = hashlib.sha1((r['kind'] + '\n' + r['reason'] + '\n' + '\n'.join(names[:5])).encode()).hexdigest()

		def write(db):
			old = db.execute('SELECT issue_key FROM crash_groups WHERE id = ?', (group,)).fetchone()
			if old and old[0]:
				return None
			db.execute('UPDATE crash_groups SET issue_key = ? WHERE id = ?', (key, group))
			fixed = db.execute('SELECT fixed_in FROM crash_groups WHERE issue_key = ? AND fixed_in IS NOT NULL '
			                   'AND id != ?', (key, group)).fetchall()
			return [f[0] for f in fixed]
		fixed = self.store.tx(write)
		if fixed and any(version_key(r['version']) >= version_key(v) for v in fixed):
			self.alerter.queue('regression', group, 'fixed in ' + ', '.join(sorted(set(fixed))))

	def cleanup(self):
		t = now()
		pings = iso(t - dt.timedelta(days=int(self.config['HAUMEA_PING_DAYS'])))
		days = (t - dt.timedelta(days=int(self.config['HAUMEA_PING_DAYS']))).strftime('%Y-%m-%d')
		reports = iso(t - dt.timedelta(days=int(self.config['HAUMEA_REPORT_DAYS'])))

		def write(db):
			db.execute('DELETE FROM installs WHERE last_seen < ?', (pings,))
			db.execute('DELETE FROM active_days WHERE day < ?', (days,))
			db.execute('DELETE FROM reports WHERE received < ?', (reports,))
			db.execute('DELETE FROM crash_groups WHERE id NOT IN (SELECT DISTINCT group_id FROM reports) '
			           'AND fixed_in IS NULL')
			db.execute('DELETE FROM alerts WHERE sent = 1 AND created < ?', (reports,))
		self.store.tx(write)


# ---- Discord alerts --------------------------------------------------------------

class Alerter:
	"""Alerts go into a table and are sent in batches: at most one Discord
	message every HAUMEA_ALERT_EVERY seconds, so a crash storm is one message."""

	def __init__(self, service):
		self.s = service
		self.url = service.config['DISCORD_WEBHOOK_URL']

	def queue(self, kind, group, detail=''):
		self.s.store.q('INSERT INTO alerts (created, kind, group_id, detail) VALUES (?, ?, ?, ?)',
		               (iso(now()), kind, group, detail))

	def check_spike(self, group):
		limit = int(self.s.config['HAUMEA_ALERT_SPIKE'])
		hour = iso(now() - dt.timedelta(hours=1))
		n = self.s.store.q('SELECT COUNT(*) FROM reports WHERE group_id = ? AND received >= ?', (group, hour))[0][0]
		if n < limit:
			return
		# One spike alert per group per hour.
		recent = self.s.store.q("SELECT 1 FROM alerts WHERE kind = 'spike' AND group_id = ? AND created >= ?",
		                        (group, hour))
		if not recent:
			self.queue('spike', group, f'{n} reports in the last hour')

	def embed(self, kind, group, detail):
		row = self.s.store.q('SELECT version, signature, title, top_frames, count FROM crash_groups WHERE id = ?',
		                     (group,))
		if not row:
			return None
		version, signature, title, top, count = row[0]
		oses = self.s.store.q('SELECT os, gl_renderer, COUNT(*) FROM reports WHERE group_id = ? '
		                      'GROUP BY os, gl_renderer ORDER BY 3 DESC LIMIT 3', (group,))
		heading = {'new': 'New crash group', 'spike': 'Crash spike', 'regression': 'Regression'}[kind]
		colour = {'new': 0xE67E22, 'spike': 0xE74C3C, 'regression': 0x9B59B6}[kind]
		fields = [{'name': 'Version', 'value': version, 'inline': True},
		          {'name': 'Reports', 'value': str(count), 'inline': True},
		          {'name': 'Signature', 'value': signature[:12], 'inline': True},
		          {'name': 'Top frames', 'value': '```\n' + '\n'.join(json.loads(top))[:900] + '\n```'},
		          {'name': 'OS / GPU', 'value': '\n'.join(f'{o} / {g or "?"} ({n})' for o, g, n in oses)[:900] or '?'}]
		if detail:
			fields.append({'name': 'Note', 'value': detail[:900]})
		return {'title': f'{heading}: {title}'[:250], 'color': colour, 'fields': fields,
		        'url': f"{self.s.config['HAUMEA_PUBLIC_URL']}/dashboard/group/{group}"}

	def flush(self):
		"""Sends what is queued as one message (Discord takes ten embeds; the
		rest is summed up). False when Discord asked to wait."""
		pending = self.s.store.q('SELECT id, kind, group_id, detail FROM alerts WHERE sent = 0 ORDER BY id')
		if not pending or not self.url:
			return True
		embeds = [e for e in (self.embed(k, g, d) for _, k, g, d in pending[:9]) if e]
		if len(pending) > 9:
			embeds.append({'title': f'and {len(pending) - 9} more alerts', 'color': 0x95A5A6,
			               'url': f"{self.s.config['HAUMEA_PUBLIC_URL']}/dashboard"})
		ok = self.post({'username': 'Haumea Studio', 'embeds': embeds})
		if ok:
			ids = [p[0] for p in pending]
			self.s.store.q(f'UPDATE alerts SET sent = 1 WHERE id IN ({",".join("?" * len(ids))})', ids)
		return ok

	def post(self, payload, attempts=3):
		data = json.dumps(payload).encode()
		for _ in range(attempts):
			req = urllib.request.Request(self.url, data=data, headers={'Content-Type': 'application/json',
			                                                            'User-Agent': 'haumea-server'})
			try:
				with urllib.request.urlopen(req, timeout=15):
					return True
			except urllib.error.HTTPError as e:
				if e.code == 429:
					try:
						wait = float(json.loads(e.read() or b'{}').get('retry_after', 1))
					except (ValueError, AttributeError):
						wait = float(e.headers.get('Retry-After', 1) or 1)
					time.sleep(min(max(wait, 0.1), 60))
					continue
				print(f'discord: HTTP {e.code}', file=sys.stderr)	# never the URL
				return False
			except OSError as e:
				print(f'discord: {type(e).__name__}', file=sys.stderr)
				return False
		return False

	def daily_summary(self):
		day = (now() - dt.timedelta(days=1)).strftime('%Y-%m-%d')
		users = self.s.store.q('SELECT COUNT(*) FROM active_days WHERE day = ?', (day,))[0][0]
		top = self.s.store.q('SELECT g.title, g.version, COUNT(r.id) FROM reports r JOIN crash_groups g '
		                     'ON g.id = r.group_id WHERE r.received >= ? GROUP BY g.id ORDER BY 3 DESC LIMIT 5',
		                     (day + 'T00:00:00Z',))
		lines = '\n'.join(f'{n}× {t} ({v})' for t, v, n in top) or 'No reports.'
		return self.post({'username': 'Haumea Studio', 'embeds': [{
			'title': f'Daily summary for {day}', 'color': 0x3498DB,
			'fields': [{'name': 'Active installs', 'value': str(users), 'inline': True},
			           {'name': 'Top crashes', 'value': lines[:1000]}]}]})


# ---- dashboard -------------------------------------------------------------------

PAGE = '''<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>{title}</title>
<style>body{{font:14px system-ui,sans-serif;margin:24px;max-width:1100px;color:#222;background:#fafafa}}
table{{border-collapse:collapse;width:100%;margin:8px 0 20px}}th,td{{text-align:left;padding:4px 8px;
border-bottom:1px solid #ddd;vertical-align:top}}th{{background:#eee}}pre{{background:#f0f0f0;padding:8px;
overflow-x:auto;white-space:pre-wrap}}a{{color:#2563eb}}.muted{{color:#777}}h1{{font-size:20px}}
h2{{font-size:16px;margin-top:24px}}</style></head><body>{body}</body></html>'''


def esc(v):
	return html.escape(str(v), quote=True)


def table(headers, rows):
	out = '<table><tr>' + ''.join(f'<th>{esc(h)}</th>' for h in headers) + '</tr>'
	for row in rows:
		out += '<tr>' + ''.join(f'<td>{c}</td>' for c in row) + '</tr>'
	return out + '</table>'


def dashboard_home(s):
	t = now()
	today = t.strftime('%Y-%m-%d')
	week = (t - dt.timedelta(days=6)).strftime('%Y-%m-%d')
	daily = s.store.q('SELECT COUNT(*) FROM active_days WHERE day = ?', (today,))[0][0]
	weekly = s.store.q('SELECT COUNT(DISTINCT uid) FROM active_days WHERE day >= ?', (week,))[0][0]
	days = s.store.q('SELECT day, COUNT(*) FROM active_days WHERE day >= ? GROUP BY day ORDER BY day DESC LIMIT 30',
	                 ((t - dt.timedelta(days=29)).strftime('%Y-%m-%d'),))
	breakdown = lambda col: s.store.q(f'SELECT {col}, COUNT(*) FROM installs WHERE last_seen >= ? '
	                                  f'GROUP BY {col} ORDER BY 2 DESC', (iso(t - dt.timedelta(days=30)),))
	groups = s.store.q('SELECT id, title, version, kind, count, last_seen, fixed_in FROM crash_groups '
	                   'ORDER BY count DESC, last_seen DESC LIMIT 100')
	body = '<h1>Haumea Studio</h1>'
	body += f'<p><b>{daily}</b> active installs today, <b>{weekly}</b> in the last 7 days.</p>'
	body += '<h2>Active installs per day</h2>' + table(['Day', 'Installs'], [(esc(d), esc(n)) for d, n in days])
	for col, name in (('version', 'Versions'), ('channel', 'Channels'), ('os', 'Systems')):
		body += f'<h2>{name} (last 30 days)</h2>' + table([name[:-1], 'Installs'],
		                                                  [(esc(v), esc(n)) for v, n in breakdown(col)])
	body += '<h2>Crash and error groups</h2>' + table(
		['Group', 'Version', 'Kind', 'Reports', 'Last seen', 'Fixed in'],
		[(f'<a href="/dashboard/group/{gid}">{esc(title)}</a>', esc(v), esc(k), esc(c), esc(last), esc(fixed or ''))
		 for gid, title, v, k, c, last, fixed in groups])
	return PAGE.format(title='Haumea Studio dashboard', body=body)


def dashboard_group(s, gid):
	row = s.store.q('SELECT id, title, version, signature, kind, top_frames, issue_key, fixed_in, count, '
	                'first_seen, last_seen FROM crash_groups WHERE id = ?', (gid,))
	if not row:
		return None
	gid, title, version, sig, kind, top, issue, fixed, count, first, last = row[0]
	split = lambda col: s.store.q(f'SELECT {col}, COUNT(*) FROM reports WHERE group_id = ? GROUP BY {col} '
	                              'ORDER BY 2 DESC LIMIT 20', (gid,))
	related = s.store.q('SELECT id, version, count, fixed_in FROM crash_groups WHERE issue_key = ? AND id != ? '
	                    'ORDER BY id', (issue, gid)) if issue else []
	reports = s.store.q('SELECT id, received, os, os_version, gl_renderer FROM reports WHERE group_id = ? '
	                    'ORDER BY received DESC LIMIT 50', (gid,))
	body = f'<p><a href="/dashboard">Dashboard</a></p><h1>{esc(title)}</h1>'
	body += (f'<p>{esc(kind)} in {esc(version)}, {esc(count)} reports, first {esc(first)}, last {esc(last)}.<br>'
	         f'<span class="muted">Signature {esc(sig)}</span></p>')
	body += '<h2>Top frames</h2><pre>' + esc('\n'.join(json.loads(top))) + '</pre>'
	body += ('<h2>Fixed in</h2><form method="post" action="/dashboard/group/' + str(gid) + '/fixed">'
	         f'<input name="version" placeholder="1.0.1.80" value="{esc(fixed or "")}"> '
	         '<button>Mark fixed</button> <span class="muted">A later version reporting the same issue '
	         'alerts as a regression. Empty clears it.</span></form>')
	if related:
		body += '<h2>The same issue in other versions</h2>' + table(['Group', 'Version', 'Reports', 'Fixed in'], [
			(f'<a href="/dashboard/group/{r}">#{r}</a>', esc(v), esc(c), esc(f or '')) for r, v, c, f in related])
	for col, name in (('version', 'Version'), ('os', 'System'), ('os_version', 'System version'),
	                  ('gl_renderer', 'GPU')):
		body += f'<h2>By {name.lower()}</h2>' + table([name, 'Reports'], [(esc(v), esc(n)) for v, n in split(col)])
	body += '<h2>Latest reports</h2>' + table(['Report', 'Received', 'System', 'GPU'], [
		(f'<a href="/dashboard/report/{rid}">{esc(rid[:12])}</a>', esc(rec), esc(f'{o} {ov}'), esc(gl))
		for rid, rec, o, ov, gl in reports])
	return PAGE.format(title=f'Group {gid}', body=body)


def dashboard_report(s, rid):
	if not UID.fullmatch(rid):
		return None
	row = s.store.q('SELECT group_id, received, body FROM reports WHERE id = ?', (rid,))
	if not row:
		return None
	gid, received, body_json = row[0]
	r = json.loads(body_json)
	names = s.symbols.symbolize(r) or []
	frames = []
	for i, f in enumerate(r['frames']):
		sym = names[i] if i < len(names) and names[i] else f['symbol']
		frames.append((esc(i), esc(f['module']), esc(f['offset']), esc(sym)))
	fields = [(esc(k), esc(r.get(k, ''))) for k in ('kind', 'reason', 'address', 'message', 'version', 'commit',
	                                                'channel', 'os', 'os_version', 'arch', 'gl_renderer',
	                                                'gl_version', 'build_id', 'created')]
	fields.append(('context', esc(json.dumps(r.get('context', {})))))
	body = f'<p><a href="/dashboard">Dashboard</a> · <a href="/dashboard/group/{gid}">Group {gid}</a></p>'
	body += f'<h1>Report {esc(rid)}</h1><p class="muted">Received {esc(received)}</p>'
	body += table(['Field', 'Value'], fields)
	body += '<h2>Stack</h2>' + table(['#', 'Module', 'Offset', 'Function'], frames)
	body += '<h2>Log</h2><pre>' + esc('\n'.join(r.get('log', []))) + '</pre>'
	return PAGE.format(title=f'Report {rid[:12]}', body=body)


PRIVACY = (HERE / 'privacy.html')


# ---- HTTP ------------------------------------------------------------------------

class Handler(http.server.BaseHTTPRequestHandler):
	server_version = 'haumea-server'
	sys_version = ''
	service = None	# set by serve()

	def log_message(self, fmt, *args):
		pass	# no access log: it would hold IP addresses

	def client_ip(self):
		ip = self.client_address[0]
		if self.service.config['HAUMEA_TRUST_PROXY'] == '1' and ip in ('127.0.0.1', '::1'):
			forwarded = self.headers.get('X-Forwarded-For', '')
			if forwarded:
				ip = forwarded.split(',')[-1].strip()
		return ip

	def reply(self, code, body=b'', content_type='text/plain; charset=utf-8', extra=()):
		self.send_response(code)
		for k, v in extra:
			self.send_header(k, v)
		if body:
			self.send_header('Content-Type', content_type)
		self.send_header('Content-Length', str(len(body)))
		self.send_header('X-Content-Type-Options', 'nosniff')
		self.end_headers()
		if body and self.command != 'HEAD':
			self.wfile.write(body)

	def read_body(self, limit):
		try:
			n = int(self.headers.get('Content-Length', ''))
		except ValueError:
			return None, 411
		if n < 0:
			return None, 400
		if n > limit:
			return None, 413
		return self.rfile.read(n), None

	def do_POST(self):
		path = urllib.parse.urlsplit(self.path).path
		if path == '/haumea/telemetry/ping':
			return self.ingest('ping', PING_MAX, lambda d: self.service.ping(validate_ping(d)))
		if path == '/haumea/reports':
			return self.ingest('report', REPORT_MAX, lambda d: self.service.report(validate_report(d)))
		m = re.fullmatch(r'/dashboard/group/(\d+)/fixed', path)
		if m:
			if not self.authorised():
				return
			# Forms only from the dashboard's own pages.
			origin = self.headers.get('Origin') or self.headers.get('Referer') or ''
			host = self.headers.get('Host', '')
			if origin and urllib.parse.urlsplit(origin).netloc != host:
				return self.reply(403)
			raw, err = self.read_body(4096)
			if err:
				return self.reply(err)
			version = urllib.parse.parse_qs(raw.decode('utf-8', 'replace')).get('version', [''])[0].strip()
			if version and not VERSION.fullmatch(version):
				return self.reply(400, b'The version is like 1.0.1.80.')
			self.service.store.q('UPDATE crash_groups SET fixed_in = ? WHERE id = ?', (version or None, int(m[1])))
			return self.reply(303, extra=[('Location', f'/dashboard/group/{m[1]}')])
		self.reply(404)

	def ingest(self, kind, limit, handle):
		if not self.service.allowed(self.client_ip(), kind):
			return self.reply(429)
		if not (self.headers.get('Content-Type', '').split(';')[0].strip() == 'application/json'):
			return self.reply(415)
		raw, err = self.read_body(limit)
		if err:
			return self.reply(err)
		try:
			handle(parse_json(raw))
		except Bad:
			return self.reply(400)	# never echo the input
		self.reply(204)

	def authorised(self):
		password = self.service.config['HAUMEA_DASH_PASSWORD']
		header = self.headers.get('Authorization', '')
		ok = False
		if password and header.startswith('Basic '):
			try:
				user, _, given = base64.b64decode(header[6:]).decode('utf-8').partition(':')
				ok = hmac.compare_digest(user.encode(), self.service.config['HAUMEA_DASH_USER'].encode()) & \
					hmac.compare_digest(given.encode(), password.encode())
			except (ValueError, UnicodeDecodeError):
				ok = False
		if not ok:
			self.reply(401, b'Authentication required.',
			           extra=[('WWW-Authenticate', 'Basic realm="Haumea Studio", charset="UTF-8"')])
		return ok

	def do_GET(self):
		path = urllib.parse.urlsplit(self.path).path
		if path == '/privacy':
			return self.reply(200, PRIVACY.read_bytes(), 'text/html; charset=utf-8')
		if path == '/healthz':
			return self.reply(200, b'ok')
		if path == '/dashboard' or path.startswith('/dashboard/'):
			if not self.authorised():
				return
			page = None
			if path in ('/dashboard', '/dashboard/'):
				page = dashboard_home(self.service)
			elif re.fullmatch(r'/dashboard/group/\d+', path):
				page = dashboard_group(self.service, int(path.rsplit('/', 1)[1]))
			elif re.fullmatch(r'/dashboard/report/[0-9a-f]{32}', path):
				page = dashboard_report(self.service, path.rsplit('/', 1)[1])
			if page is None:
				return self.reply(404)
			return self.reply(200, page.encode(), 'text/html; charset=utf-8',
			                  extra=[('Cache-Control', 'no-store'), ('X-Frame-Options', 'DENY'),
			                         ('Content-Security-Policy', "default-src 'self'; style-src 'unsafe-inline'")])
		self.reply(404)

	do_HEAD = do_GET


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
	daemon_threads = True
	allow_reuse_address = True


def background(service, stop):
	"""Alerts every HAUMEA_ALERT_EVERY seconds, the cleanup and the daily
	summary once a day."""
	last_cleanup = last_summary = ''
	every = max(5, int(service.config['HAUMEA_ALERT_EVERY']))
	while not stop.wait(every):
		try:
			service.alerter.flush()
			day = now().strftime('%Y-%m-%d')
			if day != last_cleanup:
				service.cleanup()
				last_cleanup = day
			if service.config['HAUMEA_DAILY_SUMMARY'] == '1' and day != last_summary and now().hour >= 9:
				if service.alerter.url and service.alerter.daily_summary():
					last_summary = day
		except Exception as e:	# keep the service up; the next round retries
			print(f'background: {type(e).__name__}: {e}', file=sys.stderr)


def serve(config):
	service = Service(config)
	host, _, port = config['HAUMEA_LISTEN'].rpartition(':')
	Handler.service = service
	httpd = Server((host or '127.0.0.1', int(port)), Handler)
	stop = threading.Event()
	threading.Thread(target=background, args=(service, stop), daemon=True).start()
	threading.Thread(target=service.link_worker, args=(stop,), daemon=True).start()
	return httpd, service, stop


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
	ap.add_argument('--config', default='/etc/haumea-server/env')
	a = ap.parse_args(argv)
	config = load_config(a.config)
	if not config['HAUMEA_DASH_PASSWORD']:
		print('warning: no HAUMEA_DASH_PASSWORD: the dashboard is closed', file=sys.stderr)
	httpd, _, stop = serve(config)
	print(f'listening on {config["HAUMEA_LISTEN"]}', file=sys.stderr)
	try:
		httpd.serve_forever()
	except KeyboardInterrupt:
		pass
	finally:
		stop.set()
	return 0


if __name__ == '__main__':
	sys.exit(main())
