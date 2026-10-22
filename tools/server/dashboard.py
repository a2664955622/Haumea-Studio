# SPDX-License-Identifier: GPL-2.0-or-later
"""dashboard.py: the dashboard's pages, drawn on the server.

Overview (installs, versions, systems, crash groups), a crash group and a
report. No script: the time range and the crash filter are links, and the
charts are boxes sized on the server, so the page's Content-Security-Policy
stays default-src 'self'. Every value from a ping or report is escaped.
"""
import datetime as dt
import html
import json
import re
import urllib.parse

RANGES = (7, 30, 90)
FILTERS = (('open', 'Open'), ('new', 'New'), ('regression', 'Regressions'), ('all', 'All'))
CHANNEL_COLOURS = (('production', '#5B9BFF'), ('beta', '#A98BFF'), ('dev', '#5C6370'), ('pr', '#5C6370'))
CHART_PX = 220
SPARK_DAYS = 14

CSS = '''
:root{color-scheme:dark}
body{margin:0;background:#0F1115;color:#E6E8EC;font:14px/1.45 Inter,system-ui,-apple-system,"Segoe UI",sans-serif}
a{color:#8AB6FF;text-decoration:none}a:hover{color:#B7D1FF;text-decoration:underline}
.page{max-width:1360px;margin:0 auto;padding:28px 32px 48px;display:flex;flex-direction:column;gap:24px}
.mono{font-family:"JetBrains Mono",ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
.muted{color:#9AA1AD}.soft{color:#B4BAC4}.small{font-size:12px}
h1{margin:0;font-size:22px;font-weight:600}h2{margin:0;font-size:16px;font-weight:600}
h3{margin:0;font-size:12px;font-weight:500;color:#9AA1AD;text-transform:uppercase;letter-spacing:.04em}
.top{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:16px}
.brand{display:flex;align-items:center;gap:14px}.brand a{color:inherit}
.logo{width:40px;height:40px;border-radius:10px;background:#1F2A3D;display:flex;align-items:center;justify-content:center}
.tools{display:flex;flex-wrap:wrap;align-items:center;gap:12px}
.seg{display:flex;gap:2px;background:#171A20;border:1px solid #262A33;border-radius:10px;padding:4px}
.seg a{padding:9px 14px;border-radius:7px;color:#B4BAC4;font-size:13px}
.seg a:hover{text-decoration:none;color:#E6E8EC}.seg a.on{background:#262D3A;color:#E6E8EC;font-weight:500}
.who{display:flex;align-items:center;gap:8px;padding:9px 12px;border:1px solid #262A33;border-radius:10px;font-size:13px;color:#B4BAC4}
.kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
.card{background:#171A20;border:1px solid #262A33;border-radius:12px;padding:20px;display:flex;flex-direction:column;gap:14px;min-width:0}
.kpi{padding:18px 18px 16px;gap:6px}.kpi .n{font-size:30px;font-weight:600;letter-spacing:-.01em}
.kpi .d{font-size:12px;color:#9AA1AD}.kpi .up{color:#4CC38A}.kpi .down{color:#FF8A7A}
.row{display:flex;flex-wrap:wrap;gap:12px}.wide{flex:2 1 640px}.narrow{flex:1 1 320px}.half{flex:1 1 420px}
.head{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:baseline;gap:8px}
.legend{display:flex;gap:14px;font-size:12px;color:#B4BAC4}.legend span{display:flex;align-items:center;gap:6px}
.sw{width:10px;height:10px;border-radius:3px;display:inline-block}
.chart{display:flex;gap:10px}
.axis{display:flex;flex-direction:column;justify-content:space-between;font-size:11px;color:#9AA1AD;height:220px;text-align:right;min-width:28px}
.bars{flex:1;min-width:0;height:220px;display:flex;align-items:flex-end;gap:3px;border-bottom:1px solid #2E333D;
background:linear-gradient(#20242C 1px,transparent 1px) 0 0/100% 110px}
.bars>div{flex:1;min-width:0;display:flex;flex-direction:column-reverse}
.bars.small{height:110px;gap:4px;background:none}
.under{display:flex;justify-content:space-between;font-size:11px;color:#9AA1AD}
.meter{height:8px;background:#232831;border-radius:4px;overflow:hidden}.meter>div{height:8px;border-radius:4px;background:#4A5568}
.meter.thin{height:6px}.meter.thin>div{height:6px;background:#F08A3C}.meter>div.first{background:#5B9BFF}
.split{display:flex;align-items:center;gap:10px;font-size:13px}
.split .name{flex:0 0 170px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.split .meter{flex:1}
.split .n{flex:0 0 32px;text-align:right;color:#B4BAC4;font-variant-numeric:tabular-nums}
.pair{display:flex;justify-content:space-between;font-size:13px}
.cols{display:flex;flex-wrap:wrap;gap:24px}.cols>div{flex:1 1 120px;display:flex;flex-direction:column;gap:8px}
.rule{height:1px;background:#262A33}
.scroll{overflow-x:auto}
table{border-collapse:collapse;width:100%}
th{text-align:left;font-weight:500;color:#9AA1AD;font-size:12px;letter-spacing:.04em;text-transform:uppercase;
padding:10px 12px;border-bottom:1px solid #262A33;white-space:nowrap}
td{padding:12px;border-bottom:1px solid #20242C;vertical-align:middle}
tr:hover td{background:#1C2027}td.r,th.r{text-align:right;font-variant-numeric:tabular-nums}td.nw{white-space:nowrap}
.badge{display:inline-block;white-space:nowrap;font-size:12px;font-weight:500;padding:4px 8px;border-radius:6px;
background:#262D3A;color:#C9CFD8}
.b-new,.b-warn{background:#3A2A1E;color:#FFB27A}.b-regression{background:#44231F;color:#FF9C8F}
.b-fixed{background:#1D3A2C;color:#7FDCAA}.tag{font-size:11px;padding:2px 6px;margin-left:6px;border-radius:4px;
background:#22304A;color:#A9C8FF;font-family:Inter,system-ui,sans-serif}.tag.warn{background:#3A2A1E;color:#FFB27A}
.spark{display:flex;align-items:flex-end;gap:2px;height:26px;width:112px}
.spark>div{flex:1;border-radius:1px;background:#2E333D}.spark>div.on{background:#F08A3C}
.filters{display:flex;flex-wrap:wrap;gap:6px}
.filters a{border:1px solid #2E333D;padding:8px 12px;border-radius:8px;font-size:13px;color:#B4BAC4}
.filters a:hover{text-decoration:none;color:#E6E8EC}.filters a.on{border-color:#5B9BFF;background:#1F2A3D;color:#E6E8EC}
.crumbs{font-size:13px;color:#9AA1AD;display:flex;flex-wrap:wrap;gap:8px}
.ghead{display:flex;flex-wrap:wrap;justify-content:space-between;gap:16px;align-items:flex-start}
.ghead>div{display:flex;flex-direction:column;gap:10px;min-width:0}.ghead h1{font-size:26px;overflow-wrap:anywhere}
.badges{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
form.fixed{display:flex;flex-wrap:wrap;gap:8px;align-items:flex-end;background:#171A20;border:1px solid #262A33;
border-radius:12px;padding:14px}
form.fixed label{display:flex;flex-direction:column;gap:6px;font-size:12px;color:#9AA1AD}
form.fixed input{background:#0F1115;border:1px solid #2E333D;border-radius:8px;padding:11px 12px;color:#E6E8EC;
font:14px "JetBrains Mono",ui-monospace,monospace;width:150px}
form.fixed button{border:0;cursor:pointer;padding:12px 16px;min-height:44px;border-radius:8px;background:#5B9BFF;
color:#0B1220;font:600 14px Inter,system-ui,sans-serif}
.frames{background:#0F1115;border:1px solid #22262E;border-radius:8px;overflow-x:auto}
.frame{display:flex;gap:14px;align-items:center;padding:9px 14px;font-size:13px;white-space:nowrap}
.frame+.frame{border-top:1px solid #1C2027}.frame.crash{background:#2A1D19}.frame .i{color:#6F7682;width:22px;
text-align:right;flex:none}.frame .m{flex:none;color:#9AA1AD;width:120px;overflow:hidden;text-overflow:ellipsis}
.frame .f.ours{color:#E6E8EC}.frame.crash .f.ours{color:#FFB27A;font-weight:500}.frame .f{color:#6F7682}
.fields{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px 24px}
.fields div{display:flex;flex-direction:column;gap:2px;min-width:0}.fields span{overflow-wrap:anywhere}
pre{margin:0;background:#0F1115;border:1px solid #22262E;border-radius:8px;padding:14px;overflow-x:auto;
white-space:pre-wrap;font:13px/1.5 "JetBrains Mono",ui-monospace,monospace;color:#C9CFD8}
.empty{color:#9AA1AD;padding:12px 0}
footer{font-size:12px;color:#9AA1AD;display:flex;flex-wrap:wrap;gap:16px;justify-content:space-between}
@media (max-width:640px){.page{padding:20px 16px 40px}.split .name{flex-basis:110px}}
'''

LOGO = ('<div class="logo"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#8AB6FF" '
        'stroke-width="2" stroke-linecap="round" aria-hidden="true"><circle cx="12" cy="12" r="9"></circle>'
        '<path d="M12 3v18M3 12h18"></path></svg></div>')
SHIELD = ('<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="#4CC38A" stroke-width="2" '
          'stroke-linecap="round" aria-hidden="true"><path d="M12 2l8 4v6c0 5-3.5 8.5-8 10-4.5-1.5-8-5-8-10V6z">'
          '</path><path d="M9 12l2 2 4-4"></path></svg>')


def esc(v):
	return html.escape(str(v), quote=True)


def version_key(v):
	try:
		return tuple(int(x) for x in v.split('.'))
	except (ValueError, AttributeError):
		return (0,)


def utc_now():
	return dt.datetime.now(dt.timezone.utc)


def stamp(t):
	return t.strftime('%Y-%m-%dT%H:%M:%SZ')


def parse(text):
	try:
		return dt.datetime.strptime(text, '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=dt.timezone.utc)
	except (TypeError, ValueError):
		return None


def ago(text, t):
	when = parse(text)
	if when is None:
		return ''
	s = (t - when).total_seconds()
	if s < 90:
		return 'just now'
	if s < 3600:
		return f'{int(s // 60)} min ago'
	if s < 86400:
		return f'{int(s // 3600)} h ago'
	if s < 2 * 86400:
		return 'yesterday'
	if s < 14 * 86400:
		return f'{int(s // 86400)} days ago'
	return when.strftime('%b %d, %Y')


SYSTEM_NAMES = {'windows': 'Windows', 'linux': 'Linux', 'macos': 'macOS'}


def system(os_, version=''):
	"""'Windows 11' when the version already names it, else 'Windows'."""
	return version or SYSTEM_NAMES.get(os_, os_)


def pct(n, total):
	return f'{round(n * 100 / total)}%' if total else '0%'


def change(now_n, before_n, what):
	"""'+9% on last week', in green or red; empty without anything to compare."""
	if not before_n:
		return ''
	d = round((now_n - before_n) * 100 / before_n)
	cls = 'up' if d > 0 else 'down' if d < 0 else ''
	return f'<span class="{cls}">{"+" if d > 0 else ""}{d}% on {esc(what)}</span>'


def page(title, body, user):
	who = (f'<div class="who">{SHIELD}<span>{esc(user)}</span></div>' if user else '')
	top = (f'<header class="top"><div class="brand">{LOGO}<div><h1><a href="/dashboard">Haumea Studio</a></h1>'
	       f'<div class="muted small">Telemetry and crash reports</div></div></div>'
	       f'<div class="tools">{{tools}}{who}</div></header>')
	return top, ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
	             '<meta name="viewport" content="width=device-width, initial-scale=1">'
	             f'<title>{esc(title)}</title><style>{CSS}</style></head><body><div class="page">'), \
		body + '</div></body></html>'


def render(title, body, user, tools=''):
	top, head, tail = page(title, body, user)
	return head + top.replace('{tools}', tools) + tail


# ---- crash groups' status ------------------------------------------------------------

def statuses(s, t):
	"""Each group's status: fixed, regression (its issue was marked fixed in
	a version no later than its own), new (first seen this week) or open."""
	fixed_by_issue = {}
	for key, version in s.store.q('SELECT issue_key, fixed_in FROM crash_groups '
	                              'WHERE issue_key IS NOT NULL AND fixed_in IS NOT NULL'):
		fixed_by_issue.setdefault(key, []).append(version)
	week = stamp(t - dt.timedelta(days=7))
	out = {}
	for gid, version, key, fixed, first in s.store.q('SELECT id, version, issue_key, fixed_in, first_seen '
	                                                 'FROM crash_groups'):
		if fixed:
			out[gid] = ('fixed', f'Fixed in {fixed}')
		elif key and any(version_key(version) >= version_key(f) for f in fixed_by_issue.get(key, [])):
			out[gid] = ('regression', 'Regression')
		elif (first or '') >= week:
			out[gid] = ('new', 'New')
		else:
			out[gid] = ('open', 'Open')
	return out


def badge(kind, text):
	return f'<span class="badge b-{esc(kind)}">{esc(text)}</span>'


def installs_per_group(s, ids):
	"""Installs that also send the daily ping (reports carry their uid only then)."""
	if not ids:
		return {}
	marks = ','.join('?' * len(ids))
	try:
		rows = s.store.q(f"SELECT group_id, COUNT(DISTINCT json_extract(body, '$.uid')) FROM reports "
		                 f'WHERE group_id IN ({marks}) GROUP BY group_id', tuple(ids))
	except Exception:	# an SQLite without JSON functions
		return {}
	return dict(rows)


def frame_text(entry):
	"""crash_groups.top_frames' 'module+offset name': the name, else all of it."""
	module, _, name = entry.partition(' ')
	return name or module


def spark(counts):
	top = max(counts) or 1
	bars = ''.join(f'<div class="on" style="height:{max(2, round(n * 26 / top))}px"></div>' if n
	               else '<div style="height:2px"></div>' for n in counts)
	return (f'<div class="spark" role="img" aria-label="{sum(counts)} reports in the last {len(counts)} days">'
	        f'{bars}</div>')


# ---- overview ----------------------------------------------------------------------

def home(s, query, user):
	t = utc_now()
	q = urllib.parse.parse_qs(query)
	days = int(q.get('days', ['30'])[0]) if q.get('days', ['30'])[0] in ('7', '30', '90') else 30
	show = q.get('show', ['open'])[0]
	show = show if show in dict(FILTERS) else 'open'
	day = lambda n: (t - dt.timedelta(days=n)).strftime('%Y-%m-%d')
	link = lambda **kw: '/dashboard?' + urllib.parse.urlencode(dict({'days': days, 'show': show}, **kw))

	active = lambda first, last: s.store.q('SELECT COUNT(DISTINCT uid) FROM active_days WHERE day >= ? AND day <= ?',
	                                       (day(first), day(last)))[0][0]
	today, week_ago = active(0, 0), active(7, 7)
	week, week_before = active(6, 0), active(13, 7)
	month, month_before = active(29, 0), active(59, 30)
	since = stamp(t - dt.timedelta(days=7))
	new_installs = s.store.q('SELECT COUNT(*), SUM(channel = ?) FROM installs WHERE first_seen >= ?', ('beta', since))[0]
	reports_week = s.store.q('SELECT COUNT(*) FROM reports WHERE received >= ?', (since,))[0][0]
	status = statuses(s, t)
	new_groups = sum(1 for k, _ in status.values() if k == 'new')
	regressions = sum(1 for k, _ in status.values() if k == 'regression')
	crash_note = ', '.join(x for x in (f'{new_groups} new group{"s" * (new_groups != 1)}' if new_groups else '',
	                                   f'{regressions} regression{"s" * (regressions != 1)}' if regressions else '')
	                       if x)
	kpis = [
		('Active today', today, change(today, week_ago, 'last week')),
		('Active, 7 days', week, change(week, week_before, 'the 7 before')),
		('Active, 30 days', month, change(month, month_before, 'the 30 before')),
		('New installs, 7 days', new_installs[0],
		 f'{pct(new_installs[1] or 0, new_installs[0])} on beta' if new_installs[0] else ''),
		('Crash reports, 7 days', reports_week,
		 f'<span class="down">{esc(crash_note)}</span>' if crash_note else ''),
	]
	body = '<section class="kpis" aria-label="Totals">' + ''.join(
		f'<div class="card kpi"><div class="muted">{esc(label)}</div><div class="n">{n:,}</div>'
		f'<div class="d">{note}</div></div>' for label, n, note in kpis) + '</section>'

	# Active installs per day, stacked by channel.
	per_day = {}
	for d, channel, n in s.store.q('SELECT day, channel, COUNT(*) FROM active_days WHERE day >= ? '
	                               'GROUP BY day, channel', (day(days - 1),)):
		per_day.setdefault(d, {})[channel] = n
	totals = [sum(per_day.get(day(i), {}).values()) for i in range(days - 1, -1, -1)]
	top = max(10, -(-max(totals or [0]) // 10) * 10)
	columns = ''
	for i in range(days - 1, -1, -1):
		counts = per_day.get(day(i), {})
		label = ('Today' if i == 0 else f'{day(i)}') + f': {sum(counts.values())} installs'
		segs = ''.join(f'<div style="height:{round(counts[c] * CHART_PX / top)}px;background:{colour}"></div>'
		               for c, colour in CHANNEL_COLOURS if counts.get(c))
		columns += f'<div title="{esc(label)}">{segs}</div>'
	legend = ''.join(f'<span><i class="sw" style="background:{colour}"></i>{c}</span>'
	                 for c, colour in CHANNEL_COLOURS[:3])
	chart = (f'<div class="card wide" id="installs"><div class="head"><h2>Active installs per day</h2>'
	         f'<div class="legend">{legend}</div></div>'
	         f'<div class="chart"><div class="axis"><span>{top}</span><span>{top // 2}</span><span>0</span></div>'
	         f'<div class="bars" role="img" aria-label="Active installs per day for the last {days} days, '
	         f'by channel">{columns}</div></div>'
	         f'<div class="under" style="padding-left:38px"><span>{days - 1} days ago</span><span>today</span></div>'
	         '</div>')

	# Versions, systems and channels of the installs seen in 30 days.
	month_since = stamp(t - dt.timedelta(days=30))
	split = lambda col: s.store.q(f'SELECT {col}, COUNT(*) FROM installs WHERE last_seen >= ? GROUP BY {col} '
	                              'ORDER BY 2 DESC', (month_since,))
	versions = sorted(split('version'), key=lambda r: version_key(r[0]), reverse=True)
	total = sum(n for _, n in versions)
	latest = {}
	for channel, version in s.store.q('SELECT channel, version FROM installs WHERE last_seen >= ?', (month_since,)):
		if channel in ('production', 'beta') and version_key(version) > version_key(latest.get(channel, '0')):
			latest[channel] = version
	shown, rest = versions[:4], sum(n for _, n in versions[4:])
	if rest:
		shown.append(('other', rest))
	vrows = ''
	for i, (version, n) in enumerate(shown):
		tags = [f'<span class="tag">latest {c}</span>' for c in ('beta', 'production') if latest.get(c) == version]
		if version.endswith('.0') and version != 'other':
			tags.append('<span class="tag warn">no build info</span>')
		vrows += (f'<div style="display:flex;flex-direction:column;gap:6px"><div class="pair">'
		          f'<span class="mono">{esc(version)}{"".join(tags)}</span>'
		          f'<span class="soft">{n:,} · {pct(n, total)}</span></div><div class="meter">'
		          f'<div class="{"first" if i == 0 else ""}" style="width:{pct(n, total)}"></div></div></div>')
	pairs = lambda rows: ''.join(f'<div class="pair"><span>{esc(v)}</span><span class="soft">'
	                             f'{pct(n, sum(m for _, m in rows))}</span></div>' for v, n in rows)
	side = (f'<div class="card narrow"><h2>Versions in use</h2>'
	        f'<div class="muted small" style="margin-top:-8px">Installs seen in the last 30 days</div>'
	        f'{vrows or "<div class=empty>No installs yet.</div>"}<div class="rule"></div>'
	        f'<div class="cols"><div><h3>Systems</h3>{pairs([(system(v), n) for v, n in split("os")])}</div>'
	        f'<div><h3>Channels</h3>{pairs(split("channel"))}</div></div></div>')
	body += f'<section class="row">{chart}{side}</section>'

	# Crash and error groups.
	order = {'regression': 0, 'new': 1, 'open': 2, 'fixed': 3}
	groups = s.store.q('SELECT id, title, version, top_frames, count, last_seen FROM crash_groups')
	groups = [g for g in groups if show == 'all' or (status[g[0]][0] != 'fixed' if show == 'open'
	                                                  else status[g[0]][0] == show)]
	groups.sort(key=lambda g: (order[status[g[0]][0]], -(parse(g[5]) or t).timestamp()))
	groups = groups[:100]
	ids = [g[0] for g in groups]
	sparks = {}
	if ids:
		for gid, d, n in s.store.q(f"SELECT group_id, substr(received, 1, 10), COUNT(*) FROM reports "
		                           f"WHERE received >= ? AND group_id IN ({','.join('?' * len(ids))}) GROUP BY 1, 2",
		                           (stamp(t - dt.timedelta(days=SPARK_DAYS)), *ids)):
			sparks.setdefault(gid, {})[d] = n
	people = installs_per_group(s, ids)
	rows = ''
	for gid, title, version, top_frames, count, last in groups:
		try:
			where = frame_text(json.loads(top_frames)[0])
		except (ValueError, IndexError, TypeError):
			where = ''
		counts = [sparks.get(gid, {}).get(day(i), 0) for i in range(SPARK_DAYS - 1, -1, -1)]
		rows += (f'<tr><td style="min-width:300px"><a href="/dashboard/group/{gid}" style="font-weight:500">'
		         f'{esc(title)}</a><div class="muted small mono" style="margin-top:3px">{esc(where)}</div></td>'
		         f'<td>{badge(*status[gid])}</td><td class="mono nw small">{esc(version)}</td>'
		         f'<td>{spark(counts)}</td><td class="r">{count:,}</td>'
		         f'<td class="r soft">{people.get(gid, "") or ""}</td><td class="soft nw">{esc(ago(last, t))}</td></tr>')
	filters = ''.join(f'<a href="{esc(link(show=k))}#crashes" class="{"on" if k == show else ""}">{label}</a>'
	                  for k, label in FILTERS)
	body += (f'<section class="card" id="crashes"><div class="head" style="align-items:center"><div><h2>'
	         f'Crash and error groups</h2><div class="muted small" style="margin-top:4px">The same crash in one '
	         f'version is one group; groups for the same issue across versions are linked</div></div>'
	         f'<nav class="filters" aria-label="Show">{filters}</nav></div>'
	         + (f'<div class="scroll"><table><thead><tr><th>Group</th><th>Status</th><th>Version</th>'
	            f'<th>Last {SPARK_DAYS} days</th><th class="r">Reports</th>'
	            f'<th class="r" title="Installs that also send the daily ping">Installs</th><th>Last seen</th></tr>'
	            f'</thead><tbody>{rows}</tbody></table></div>' if rows else
	            '<div class="empty">Nothing here.</div>') + '</section>')
	body += ('<footer><span>No IP addresses are stored. Pings are kept '
	         f'{esc(s.config["HAUMEA_PING_DAYS"])} days, reports {esc(s.config["HAUMEA_REPORT_DAYS"])}.</span>'
	         '<a href="https://telemetry.itsneufox.xyz/privacy">Privacy page</a></footer>')
	ranges = ''.join(f'<a href="{esc(link(days=r))}" class="{"on" if r == days else ""}">{r} days</a>'
	                 for r in RANGES)
	return render('Haumea Studio dashboard', body, user, f'<nav class="seg" aria-label="Time range">{ranges}</nav>')


# ---- a crash group -----------------------------------------------------------------

def symbolized_frames(s, r):
	names = s.symbols.symbolize(r) or []
	out, first_own = [], True
	for i, f in enumerate(r.get('frames', [])):
		ours = f['module'] in ('haumea', 'haumea.exe')
		name = (names[i] if i < len(names) and names[i] else '') or f.get('symbol') or f['offset']
		top = ours and first_own
		first_own = first_own and not ours
		out.append(f'<div class="frame mono{" crash" if top else ""}"><span class="i">{i}</span>'
		           f'<span class="m">{esc(f["module"])}</span><span class="f{" ours" if ours else ""}">'
		           f'{esc(name)}</span></div>')
	return ''.join(out), bool(names)


def group(s, gid, user):
	row = s.store.q('SELECT id, title, version, signature, kind, top_frames, issue_key, fixed_in, count, '
	                'first_seen, last_seen FROM crash_groups WHERE id = ?', (gid,))
	if not row:
		return None
	t = utc_now()
	gid, title, version, sig, kind, top, issue, fixed, count, first, last = row[0]
	state, state_text = statuses(s, t)[gid]
	latest = s.store.q('SELECT body FROM reports WHERE group_id = ? ORDER BY received DESC LIMIT 1', (gid,))
	r = json.loads(latest[0][0]) if latest else {}
	where = ''
	try:
		where = frame_text(json.loads(top)[0])
	except (ValueError, IndexError, TypeError):
		pass
	note = ''
	if state == 'regression':
		was = [v for v, in s.store.q('SELECT fixed_in FROM crash_groups WHERE issue_key = ? AND fixed_in IS NOT NULL',
		                             (issue,))]
		note = f'<span class="muted small">marked fixed in {esc(", ".join(sorted(set(was))))}, back in {esc(version)}</span>'
	people = installs_per_group(s, [gid]).get(gid, 0)
	stats = [('Reports', f'{count:,}'), ('Installs (ping on)', f'{people:,}' if people else '–'),
	         ('First seen', (parse(first) or t).strftime('%b %d, %H:%M')), ('Last seen', ago(last, t))]
	body = (f'<nav class="crumbs" aria-label="Breadcrumb"><a href="/dashboard">Overview</a><span aria-hidden="true">/'
	        f'</span><a href="/dashboard#crashes">Crashes</a><span aria-hidden="true">/</span>'
	        f'<span class="soft">Group {gid}</span></nav>'
	        f'<div class="ghead"><div><div class="badges">{badge(state, state_text)}'
	        f'{badge("open", r.get("reason") or kind)}{note}</div><h1>{esc(title)}</h1>'
	        f'<div class="mono small muted">{esc(where)} · signature {esc(sig[:16])} · {esc(version)}</div></div>'
	        f'<form class="fixed" method="post" action="/dashboard/group/{gid}/fixed"><label>Fixed in version'
	        f'<input name="version" placeholder="1.0.0.243" value="{esc(fixed or "")}"></label>'
	        f'<button>Mark fixed</button></form></div>'
	        '<section class="kpis" aria-label="Totals">' + ''.join(
	            f'<div class="card kpi"><div class="muted">{esc(k)}</div><div class="n" style="font-size:24px">'
	            f'{esc(v)}</div></div>' for k, v in stats) + '</section>')

	frames, named = symbolized_frames(s, r) if r else ('', False)
	counts = dict(s.store.q('SELECT substr(received, 1, 10), COUNT(*) FROM reports WHERE group_id = ? AND '
	                        'received >= ? GROUP BY 1', (gid, stamp(t - dt.timedelta(days=SPARK_DAYS)))))
	day = lambda n: (t - dt.timedelta(days=n)).strftime('%Y-%m-%d')
	series = [counts.get(day(i), 0) for i in range(SPARK_DAYS - 1, -1, -1)]
	peak = max(series) or 1
	bars = ''.join(f'<div title="{day(SPARK_DAYS - 1 - i)}: {n}" style="justify-content:flex-start">'
	               f'<div style="height:{max(2, round(n * 110 / peak))}px;border-radius:2px 2px 0 0;'
	               f'background:{"#F08A3C" if n else "#2E333D"}"></div></div>' for i, n in enumerate(series))
	splits = ''
	for col, name in (("CASE WHEN os_version != '' THEN os_version ELSE os END", 'System'), ('gl_renderer', 'GPU'),
	                  ('channel', 'Channel')):
		rows = s.store.q(f'SELECT {col}, COUNT(*) FROM reports WHERE group_id = ? GROUP BY 1 ORDER BY 2 DESC LIMIT 5',
		                 (gid,))
		most = max((n for _, n in rows), default=1)
		splits += f'<div style="display:flex;flex-direction:column;gap:8px"><h3>{name}</h3>' + ''.join(
			f'<div class="split"><span class="name" title="{esc(v)}">{esc(v)}</span><div class="meter thin">'
			f'<div style="width:{round(n * 100 / most)}%"></div></div><span class="n">{n}</span></div>'
			for v, n in rows) + '</div>'
	body += (f'<section class="row"><div class="card" style="flex:3 1 560px"><div class="head"><h2>Stack</h2>'
	         f'<span class="muted small">{"Names from haumea-symbols-" + esc(version) + ".zip" if named else "No symbols for this build: offsets only"}'
	         f'</span></div><div class="frames">{frames or "<div class=empty>No report kept.</div>"}</div></div>'
	         f'<div style="flex:2 1 340px;min-width:0;display:flex;flex-direction:column;gap:12px">'
	         f'<div class="card"><h2>Reports per day</h2><div class="bars small" role="img" aria-label="Reports per '
	         f'day over the last {SPARK_DAYS} days">{bars}</div><div class="under"><span>{SPARK_DAYS - 1} days ago'
	         f'</span><span>today</span></div></div><div class="card"><h2>Where it happens</h2>{splits}</div></div>'
	         '</section>')

	related = s.store.q('SELECT id, version, count, fixed_in FROM crash_groups WHERE issue_key = ? AND id != ? '
	                    'ORDER BY id', (issue, gid)) if issue else []
	reports = s.store.q('SELECT id, received, os, os_version, gl_renderer, body FROM reports WHERE group_id = ? '
	                    'ORDER BY received DESC LIMIT 50', (gid,))
	rel = ''.join(f'<tr><td><a href="/dashboard/group/{rid}">#{rid}</a></td><td class="mono small">{esc(v)}</td>'
	              f'<td class="r">{c:,}</td><td>{badge("fixed", "Fixed in " + f) if f else badge("open", "Open")}</td>'
	              f'</tr>' for rid, v, c, f in related)
	latest_rows = ''
	for rid, rec, os_, osv, gl, body_json in reports:
		try:
			ctx = json.loads(body_json).get('context', {})
		except ValueError:
			ctx = {}
		place = ' · '.join(x for x in (ctx.get('window', ''), (ctx.get('mode', '') + ' mode') if ctx.get('mode')
		                                else '') if x)
		latest_rows += (f'<tr><td class="mono small"><a href="/dashboard/report/{rid}">{esc(rid[:12])}</a></td>'
		                f'<td class="soft nw">{esc(ago(rec, t))}</td><td class="nw">{esc(system(os_, osv))}</td>'
		                f'<td class="soft" style="min-width:200px">{esc(gl)}</td>'
		                f'<td class="soft" style="min-width:180px">{esc(place)}</td></tr>')
	body += (f'<section class="row"><div class="card" style="flex:1 1 380px"><h2>The same issue in other versions</h2>'
	         + (f'<table><thead><tr><th>Group</th><th>Version</th><th class="r">Reports</th><th>Status</th></tr>'
	            f'</thead><tbody>{rel}</tbody></table>' if rel else
	            '<div class="empty">None yet: groups are linked once their release\'s symbols are in.</div>')
	         + f'</div><div class="card" style="flex:2 1 560px"><h2>Latest reports</h2><div class="scroll"><table>'
	         f'<thead><tr><th>Report</th><th>Received</th><th>System</th><th>GPU</th><th>Focused window</th></tr>'
	         f'</thead><tbody>{latest_rows}</tbody></table></div></div></section>')
	return render(f'Group {gid}: {title}', body, user)


# ---- a report ----------------------------------------------------------------------

def report(s, rid, user, uid_pattern):
	if not uid_pattern.fullmatch(rid):
		return None
	row = s.store.q('SELECT group_id, received, body FROM reports WHERE id = ?', (rid,))
	if not row:
		return None
	t = utc_now()
	gid, received, body_json = row[0]
	r = json.loads(body_json)
	frames, named = symbolized_frames(s, r)
	keys = ('kind', 'reason', 'address', 'message', 'version', 'commit', 'channel', 'os', 'os_version', 'arch',
	        'gl_renderer', 'gl_version', 'build_id', 'created')
	fields = ''.join(f'<div><span class="muted small">{esc(k.replace("_", " "))}</span>'
	                 f'<span class="{"mono" if k in ("address", "commit", "build_id", "version") else ""}">'
	                 f'{esc(r.get(k, "")) or "–"}</span></div>' for k in keys)
	ctx = r.get('context', {})
	fields += ''.join(f'<div><span class="muted small">{k}</span><span>{esc(ctx.get(k, "")) or "–"}</span></div>'
	                  for k in ('mode', 'window'))
	body = (f'<nav class="crumbs" aria-label="Breadcrumb"><a href="/dashboard">Overview</a><span aria-hidden="true">/'
	        f'</span><a href="/dashboard/group/{gid}">Group {gid}</a><span aria-hidden="true">/</span>'
	        f'<span class="soft mono">{esc(rid)}</span></nav>'
	        f'<div><h1>Report {esc(rid[:12])}</h1><div class="muted small">Received {esc(ago(received, t))} '
	        f'({esc(received)})</div></div>'
	        f'<section class="card"><h2>Details</h2><div class="fields">{fields}</div></section>'
	        f'<section class="card"><div class="head"><h2>Stack</h2><span class="muted small">'
	        f'{"Names from the release symbols" if named else "No symbols for this build: offsets only"}'
	        f'</span></div><div class="frames">{frames}</div></section>'
	        f'<section class="card"><h2>Log</h2><pre>{esc(chr(10).join(r.get("log", []))) or "(empty)"}</pre></section>')
	return render(f'Report {rid[:12]}', body, user)
