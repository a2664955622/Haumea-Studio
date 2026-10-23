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
    GET  /stats                   what it all adds up to, for anyone (totals only)
    GET  /dashboard/...           the dashboard (dashboard.py), behind Cloudflare Access
                                  (access.py) or, without it set up, HTTP basic auth

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
import http.cookies
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
import access  # noqa: E402
import dashboard  # noqa: E402
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
	# Cloudflare Access in front of /dashboard: the team ("itsneufox", as in
	# itsneufox.cloudflareaccess.com) and the application's AUD tag. Both set:
	# only Access's signed requests get in, the password is not asked.
	'HAUMEA_ACCESS_TEAM': '',
	'HAUMEA_ACCESS_AUD': '',
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
	'HAUMEA_DAILY_SUMMARY': '1',	# 1: a summary every day at 09:00 UTC
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
		self.access = None
		if config['HAUMEA_ACCESS_TEAM'] and config['HAUMEA_ACCESS_AUD']:
			self.access = access.AccessVerifier(config['HAUMEA_ACCESS_TEAM'], config['HAUMEA_ACCESS_AUD'])
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
		return {'title': f'{heading}: {title}'[:250],
		        'description': 'Full dashboard details are attached as a standalone HTML report.',
		        'color': colour, 'fields': fields,
		        'url': f"{self.s.config['HAUMEA_PUBLIC_URL']}/dashboard/group/{group}"}

	def flush(self):
		"""Send at most 20 queued alerts in one message. Discord takes ten
		embeds; the HTML attachment includes details for the whole batch. False
		when Discord asked to wait."""
		pending = self.s.store.q('SELECT id, kind, group_id, detail FROM alerts WHERE sent = 0 ORDER BY id')
		if not pending or not self.url:
			return True
		batch = pending[:20]
		embeds = [e for e in (self.embed(k, g, d) for _, k, g, d in batch[:9]) if e]
		if len(pending) > 9:
			remaining = max(0, len(pending) - len(batch))
			description = 'Dashboard details for these alerts are in the attached HTML.'
			if remaining:
				description += f' {remaining} alerts remain queued for later messages.'
			embeds.append({'title': f'{len(batch) - 9} additional alerts',
			               'description': description,
			               'color': 0x95A5A6,
			               'url': f"{self.s.config['HAUMEA_PUBLIC_URL']}/dashboard"})
		group_ids = list(dict.fromkeys(p[2] for p in batch if p[2] is not None))
		attachment = self.report_attachment(group_ids) if group_ids else None
		ok = self.post({'username': 'Haumea Studio', 'embeds': embeds,
		                'allowed_mentions': {'parse': []}}, attachment=attachment)
		if ok:
			ids = [p[0] for p in batch]
			self.s.store.q(f'UPDATE alerts SET sent = 1 WHERE id IN ({",".join("?" * len(ids))})', ids)
		return ok

	def report_attachment(self, group_ids):
		"""Render an offline snapshot of the dashboard details for alert groups."""
		esc = lambda value: html.escape(str(value if value is not None else ''), quote=True)
		t = dashboard.utc_now()
		statuses = dashboard.statuses(self.s, t)
		sections = []
		for gid in group_ids:
			row = self.s.store.q('SELECT title, version, signature, kind, issue_key, count, '
			                    'first_seen, last_seen FROM crash_groups WHERE id = ?', (gid,))
			if not row:
				continue
			title, version, signature, kind, issue, count, first, last = row[0]
			latest = self.s.store.q('SELECT received, body FROM reports WHERE group_id = ? ORDER BY received DESC LIMIT 1',
			                        (gid,))
			r = json.loads(latest[0][1]) if latest else {}
			received = latest[0][0] if latest else ''
			state, state_text = statuses.get(gid, ('open', 'Open'))
			try:
				installs = self.s.store.q("SELECT COUNT(DISTINCT json_extract(body, '$.uid')) FROM reports "
				                         'WHERE group_id = ?', (gid,))[0][0]
			except sqlite3.OperationalError:
				installs = 0
			stats = [('Reports', f'{count:,}'),
			         ('Installs with daily ping', f'{installs:,}' if installs else '–'),
			         ('First seen', first), ('Last seen', last)]
			kpis = ''.join(f'<div class="kpi"><span>{esc(label)}</span><strong>{esc(value)}</strong></div>'
			                for label, value in stats)
			frames, named = dashboard.symbolized_frames(self.s, r) if r else ('', False)
			counts = dict(self.s.store.q('SELECT substr(received, 1, 10), COUNT(*) FROM reports WHERE group_id = ? '
			                            'AND received >= ? GROUP BY 1',
			                            (gid, dashboard.stamp(t - dt.timedelta(days=dashboard.SPARK_DAYS)))))
			day = lambda n: (t - dt.timedelta(days=n)).strftime('%Y-%m-%d')
			series = [(day(i), counts.get(day(i), 0)) for i in range(dashboard.SPARK_DAYS - 1, -1, -1)]
			peak = max((n for _, n in series), default=1) or 1
			bars = ''.join(f'<div class="bar"><i title="{esc(date)}: {n}" style="height:{max(2, round(n * 100 / peak))}%"></i>'
			               f'<span>{esc(date[5:])}</span></div>' for date, n in series)
			splits = []
			for column, label in (("CASE WHEN os_version != '' THEN os_version ELSE os END", 'System'),
			                      ('gl_renderer', 'GPU'), ('channel', 'Channel')):
				rows = self.s.store.q(f'SELECT {column}, COUNT(*) FROM reports WHERE group_id = ? GROUP BY 1 '
				                      'ORDER BY 2 DESC LIMIT 5', (gid,))
				values = ''.join(f'<li><span>{esc(value or "?")}</span><b>{n:,}</b></li>' for value, n in rows)
				splits.append(f'<div class="card"><h3>{label}</h3><ul>{values or "<li>–</li>"}</ul></div>')
			related = self.s.store.q('SELECT id, version, count, fixed_in FROM crash_groups WHERE issue_key = ? AND id != ? '
			                        'ORDER BY id', (issue, gid)) if issue else []
			related_rows = ''.join(f'<tr><td>Group {other_id}</td><td>{esc(other_version)}</td><td>{other_count:,}</td>'
			                      f'<td>{"Fixed in " + esc(other_fixed) if other_fixed else "Open"}</td></tr>'
			                      for other_id, other_version, other_count, other_fixed in related)
			related_html = (f'<div class="scroll"><table><thead><tr><th>Group</th><th>Version</th><th>Reports</th>'
			                f'<th>Status</th></tr></thead><tbody>{related_rows}</tbody></table></div>' if related
			                else '<p class="muted">No linked groups in other versions.</p>')
			reports = self.s.store.q('SELECT received, os, os_version, gl_renderer, body FROM reports WHERE group_id = ? '
			                        'ORDER BY received DESC LIMIT 50', (gid,))
			report_rows = []
			for report_received, os_, os_version, renderer, report_body in reports:
				try:
					context = json.loads(report_body).get('context', {})
				except (TypeError, ValueError):
					context = {}
				place = ' · '.join(x for x in (context.get('window', ''),
					                            (context.get('mode', '') + ' mode') if context.get('mode') else '') if x)
				report_rows.append(f'<tr><td>{esc(report_received)}</td><td>{esc(dashboard.system(os_, os_version))}</td>'
				                   f'<td>{esc(renderer or "?")}</td><td>{esc(place or "–")}</td></tr>')
			history = (f'<div class="scroll"><table><thead><tr><th>Received (UTC)</th><th>System</th><th>GPU</th>'
			           f'<th>Focused window</th></tr></thead><tbody>{"".join(report_rows)}</tbody></table></div>'
			           if report_rows else '<p class="muted">No reports are retained.</p>')
			fields = ('kind', 'reason', 'address', 'message', 'version', 'commit', 'channel', 'os', 'os_version', 'arch',
			          'gl_renderer', 'gl_version', 'build_id', 'created')
			meta = ''.join(f'<div><span>{esc(key.replace("_", " "))}</span><strong>{esc(r.get(key) or "–")}</strong></div>'
			               for key in fields)
			context = r.get('context', {})
			meta += ''.join(f'<div><span>{label}</span><strong>{esc(context.get(key) or "–")}</strong></div>'
			                for key, label in (('mode', 'mode'), ('window', 'window')))
			log_text = '\n'.join(r.get('log', [])) if r else ''
			for private_id in (r.get('uid'), r.get('id')):
				if private_id:
					log_text = log_text.replace(private_id, '[redacted id]')
			logs = esc(log_text)
			log_section = f'<section class="card"><h3>Latest report log</h3><pre>{logs or "(empty)"}</pre></section>'
			alert = self.s.store.q('SELECT kind, detail FROM alerts WHERE group_id = ? ORDER BY id DESC LIMIT 1', (gid,))
			trigger = (alert[0][0] + (': ' + alert[0][1] if alert[0][1] else '')) if alert else 'crash group alert'
			sections.append(f'''<article>
			<header><p class="badge {esc(state)}">{esc(state_text)} · {esc(kind)}</p>
			<h2>{esc(title)}</h2><p class="muted">Version {esc(version)} · signature {esc(signature)} · group {gid}</p>
			<p class="muted">Alert trigger: {esc(trigger)}</p></header>
			<div class="kpis">{kpis}</div>
			<div class="row"><section class="card grow"><h3>Latest stack</h3>
			<p class="muted">{('Names from build symbols' if named else 'Offsets only; no symbols available for this build')}</p>
			<div class="frames">{frames or '<p>No retained report.</p>'}</div></section>
			<section class="card grow"><h3>Reports per day · last {dashboard.SPARK_DAYS} days</h3><div class="chart">{bars}</div></section></div>
			<div class="row">{''.join(splits)}</div>
			<section class="card"><h3>Latest report details</h3><p class="muted">Received {esc(received)} UTC</p>
			<div class="fields">{meta}</div></section>{log_section}
			<section class="card"><h3>Same issue in other versions</h3>{related_html}</section>
			<section class="card"><h3>Latest reports · up to 50</h3>{history}</section></article>''')
		if not sections:
			return None
		stamp = now().strftime('%Y-%m-%d %H:%M:%S UTC')
		document = f'''<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline';">
<title>Haumea Studio crash and error details</title><style>
:root{{color-scheme:dark}}body{{margin:0;background:#0f1115;color:#e6e8ec;font:14px/1.5 system-ui,sans-serif}}
main{{max-width:1100px;margin:auto;padding:28px;display:grid;gap:24px}}h1,h2,h3,p{{margin:0}}h1{{font-size:24px}}h2{{font-size:20px;overflow-wrap:anywhere}}
.muted{{color:#9aa1ad}}article{{display:grid;gap:14px;padding-bottom:28px;border-bottom:1px solid #30343c}}article>header{{display:grid;gap:8px}}
.badge{{color:#ffbd82;background:#38291e;border-radius:6px;padding:4px 8px;width:max-content}}.regression{{color:#ff9c8f}}
.kpis{{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}}.kpi,.card{{background:#171a20;border:1px solid #292d35;border-radius:10px;padding:16px}}
.kpi{{display:grid;gap:4px;color:#9aa1ad}}.kpi strong{{font-size:18px;color:#e6e8ec}}.row{{display:flex;flex-wrap:wrap;gap:12px}}.grow{{flex:1 1 420px;min-width:0}}
.frames{{font-family:ui-monospace,monospace;display:grid;gap:5px;overflow-wrap:anywhere}}.frame{{display:flex;gap:10px}}.frame .i{{color:#707784;min-width:24px}}.crash{{color:#ff9d7a}}
.fields{{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px}}.fields>div{{display:grid;gap:3px;overflow-wrap:anywhere}}.fields span{{color:#9aa1ad;text-transform:capitalize;font-size:12px}}
pre{{white-space:pre-wrap;overflow-wrap:anywhere;background:#101216;border-radius:7px;padding:12px;max-height:800px;overflow:auto}}
.chart{{display:flex;align-items:flex-end;gap:3px;min-height:135px;overflow-x:auto}}.bar{{height:130px;min-width:23px;display:flex;flex-direction:column;justify-content:flex-end;gap:4px;text-align:center;font-size:10px;color:#9aa1ad}}
.bar i{{display:block;min-height:2px;background:#f08a3c;border-radius:3px 3px 0 0}}ul{{margin:0;padding:0;list-style:none;display:grid;gap:6px}}li{{display:flex;justify-content:space-between;gap:12px;overflow-wrap:anywhere}}
.scroll{{overflow-x:auto}}table{{border-collapse:collapse;width:100%;font-size:13px}}th,td{{text-align:left;padding:9px;border-bottom:1px solid #292d35;vertical-align:top}}th{{color:#9aa1ad;white-space:nowrap}}
footer{{color:#9aa1ad;font-size:12px}}@media(max-width:600px){{main{{padding:16px}}}}
</style></head><body><main><header><h1>Haumea Studio · crash and error report details</h1>
<p class="muted">Offline dashboard snapshot generated {esc(stamp)}. Install IDs and report IDs are excluded.</p></header>
{''.join(sections)}<footer>Diagnostic information is from reports voluntarily sent by the editor. Keep this file in the private developer channel.</footer>
</main></body></html>'''
		return ('haumea-crash-details.html', document.encode('utf-8', 'replace'))

	def post(self, payload, attachment=None, attempts=3):
		wire_payload = dict(payload)
		if attachment:
			filename, content = attachment
			wire_payload['attachments'] = [{'id': 0, 'filename': filename,
			                                'description': 'Self-contained dashboard snapshot; no login required'}]
			boundary = '----haumea-' + os.urandom(16).hex()
			encoded = json.dumps(wire_payload).encode('utf-8')
			data = (b'--' + boundary.encode() + b'\r\n'
			        b'Content-Disposition: form-data; name="payload_json"\r\n'
			        b'Content-Type: application/json\r\n\r\n' + encoded + b'\r\n'
			        b'--' + boundary.encode() + b'\r\n'
			        + f'Content-Disposition: form-data; name="files[0]"; filename="{filename}"\r\n'.encode()
			        + b'Content-Type: text/html; charset=utf-8\r\n\r\n' + content + b'\r\n'
			        + b'--' + boundary.encode() + b'--\r\n')
			content_type = f'multipart/form-data; boundary={boundary}'
		else:
			data = json.dumps(wire_payload).encode('utf-8')
			content_type = 'application/json'
		for _ in range(attempts):
			req = urllib.request.Request(self.url, data=data, headers={'Content-Type': content_type,
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
		day = (now().date() - dt.timedelta(days=1)).strftime('%Y-%m-%d')
		start = day + 'T00:00:00Z'
		end = (dt.datetime.strptime(day, '%Y-%m-%d').replace(tzinfo=dt.timezone.utc) + dt.timedelta(days=1))
		end = iso(end)
		users = self.s.store.q('SELECT COUNT(*) FROM active_days WHERE day = ?', (day,))[0][0]
		new_installs = self.s.store.q('SELECT COUNT(*) FROM installs WHERE first_seen >= ? AND first_seen < ?',
		                             (start, end))[0][0]
		report_counts = dict((kind, count) for kind, count in self.s.store.q(
			'SELECT kind, COUNT(*) FROM reports WHERE received >= ? AND received < ? GROUP BY kind', (start, end)))
		groups = self.s.store.q('SELECT COUNT(DISTINCT group_id) FROM reports WHERE received >= ? AND received < ?',
		                        (start, end))[0][0]
		os_counts = self.s.store.q('SELECT os, COUNT(*) FROM active_days WHERE day = ? GROUP BY os ORDER BY 2 DESC',
		                           (day,))
		channel_counts = self.s.store.q('SELECT channel, COUNT(*) FROM active_days WHERE day = ? '
		                                'GROUP BY channel ORDER BY 2 DESC', (day,))
		top = self.s.store.q('SELECT g.title, g.version, COUNT(r.id) FROM reports r JOIN crash_groups g '
		                     'ON g.id = r.group_id WHERE r.received >= ? AND r.received < ? '
		                     'GROUP BY g.id ORDER BY 3 DESC LIMIT 5', (start, end))
		lines = '\n'.join(f'{n}× {title} ({version})' for title, version, n in top) or 'No reports.'
		os_line = ' · '.join(f'{name}: {n}' for name, n in os_counts) or 'No pings'
		channel_line = ' · '.join(f'{name}: {n}' for name, n in channel_counts) or 'No pings'
		return self.post({'username': 'Haumea Studio', 'allowed_mentions': {'parse': []}, 'embeds': [{
			'title': f'Daily summary for {day}', 'color': 0x3498DB,
			'fields': [{'name': 'Installs', 'value': f'Active: {users:,}\nNew: {new_installs:,}', 'inline': True},
			           {'name': 'Reports', 'value': f'Crashes: {report_counts.get("crash", 0):,}\n'
			            f'Errors: {report_counts.get("error", 0):,}\nGroups: {groups:,}', 'inline': True},
			           {'name': 'Active by system', 'value': os_line[:1000], 'inline': True},
			           {'name': 'Active by channel', 'value': channel_line[:1000], 'inline': True},
			           {'name': 'Top reported groups', 'value': lines[:1000]}]}]})


# ---- dashboard -------------------------------------------------------------------

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
		"""Who is signed in (an email with Cloudflare Access, else the
		dashboard's user name), or None after answering 401/403."""
		verifier = self.service.access
		if verifier:
			token = self.headers.get('Cf-Access-Jwt-Assertion', '')
			if not token:
				cookies = http.cookies.SimpleCookie()
				try:
					cookies.load(self.headers.get('Cookie', ''))
				except http.cookies.CookieError:
					pass
				token = cookies['CF_Authorization'].value if 'CF_Authorization' in cookies else ''
			try:
				claims = verifier.verify(token)
			except access.Denied:
				self.reply(403, b'Sign in through Cloudflare Access: https://reports.itsneufox.xyz/dashboard')
				return None
			return str(claims.get('email') or claims.get('common_name') or 'Cloudflare Access')
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
			return None
		return self.service.config['HAUMEA_DASH_USER']

	def do_GET(self):
		url = urllib.parse.urlsplit(self.path)
		path = url.path
		if path == '/privacy':
			return self.reply(200, PRIVACY.read_bytes(), 'text/html; charset=utf-8')
		if path == '/stats':
			return self.reply(200, dashboard.public(self.service, url.query).encode(), 'text/html; charset=utf-8',
			                  extra=[('Cache-Control', 'public, max-age=300'), ('X-Frame-Options', 'DENY'),
			                         ('Content-Security-Policy', "default-src 'self'; style-src 'unsafe-inline'")])
		if path == '/healthz':
			return self.reply(200, b'ok')
		if path == '/dashboard' or path.startswith('/dashboard/'):
			user = self.authorised()
			if not user:
				return
			page = None
			if path in ('/dashboard', '/dashboard/'):
				page = dashboard.home(self.service, url.query, user)
			elif re.fullmatch(r'/dashboard/group/\d+', path):
				page = dashboard.group(self.service, int(path.rsplit('/', 1)[1]), user)
			elif re.fullmatch(r'/dashboard/report/[0-9a-f]{32}', path):
				page = dashboard.report(self.service, path.rsplit('/', 1)[1], user, UID)
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
	last_cleanup = ''
	every = max(5, int(service.config['HAUMEA_ALERT_EVERY']))
	while not stop.wait(every):
		try:
			service.alerter.flush()
			day = now().strftime('%Y-%m-%d')
			if day != last_cleanup:
				service.cleanup()
				last_cleanup = day
			if service.config['HAUMEA_DAILY_SUMMARY'] == '1' and now().hour >= 9:
				sent_day = service.store.q("SELECT value FROM state WHERE key = 'daily_summary_sent_day'")
				if sent_day and sent_day[0][0] == day:
					continue
				if service.alerter.url and service.alerter.daily_summary():
					service.store.q("INSERT INTO state (key, value) VALUES ('daily_summary_sent_day', ?) "
					                "ON CONFLICT (key) DO UPDATE SET value = excluded.value", (day,))
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
