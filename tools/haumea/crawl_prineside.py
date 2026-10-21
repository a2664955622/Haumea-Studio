#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# crawl_prineside.py
# Crawls the category pages of Prineside's GTA SA / SA-MP model ID list
# (https://dev.prineside.com/en/gtasa_samp_model_id/) and writes
# prineside_categories.json for gen_categories.js.
#
# Usage: python3 crawl_prineside.py [output.json]
#

import html
import json
import os
import re
import sys
import time
import urllib.request

BASE = 'https://dev.prineside.com'
ROOT = '/en/gtasa_samp_model_id/'
DELAY = 0.35

def fetch(path):
	for attempt in range(5):
		try:
			req = urllib.request.Request(BASE + path, headers={
				'User-Agent': 'Haumea map editor category import (+https://github.com/itsneufox/Haumea)'})
			with urllib.request.urlopen(req, timeout=30) as r:
				data = r.read().decode('utf-8', 'replace')
			time.sleep(DELAY)
			return data
		except Exception as e:
			print('  retry %s: %s' % (path, e), file=sys.stderr)
			time.sleep(2 + attempt * 3)
	raise RuntimeError('failed to fetch ' + path)

def text(s):
	return html.unescape(re.sub(r'<[^>]+>', '', s)).strip()

ITEM_RE = re.compile(r'<a href="[^"]*/model/[^"]*" class="mp-item"(.*?)>', re.S)
ATTR_RE = re.compile(r'data-([a-z]+)="([^"]*)"')

def parse_models(page):
	models = []
	for m in ITEM_RE.finditer(page):
		attrs = dict(ATTR_RE.findall(m.group(1)))
		if 'model' not in attrs:
			continue
		models.append({
			'id': int(attrs['model']),
			'name': html.unescape(attrs.get('name', '')),
			'title': html.unescape(attrs.get('title', '')),
			'samp': attrs.get('samp') == '1',
		})
	return models

def main():
	out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'prineside_categories.json')

	index = fetch(ROOT + 'categories/')
	slugs = []
	for slug in re.findall(r'href="' + re.escape(ROOT) + r'category/([a-z0-9-]+)/"', index):
		if slug != 'all' and slug not in slugs:
			slugs.append(slug)
	print('%d categories' % len(slugs))

	categories = {}
	for slug in slugs:
		path = ROOT + 'category/' + slug + '/'
		first = fetch(path)
		main_html = first[first.find('dt-main'):]
		title = text(re.search(r'<h1[^>]*>(.*?)</h1>', main_html, re.S).group(1)) if re.search(r'<h1[^>]*>(.*?)</h1>', main_html, re.S) else slug
		children = []
		sub = main_html.find('Subcategories')
		if sub >= 0:
			block = main_html[sub:main_html.find('Sorting', sub)]
			# The block also holds this category's own page links.
			children = [c for c in dict.fromkeys(re.findall(r'category/([a-z0-9-]+)/', block)) if c != slug]
		pages = [int(p) for p in re.findall(r'category/' + re.escape(slug) + r'/\?page=(\d+)', first)]
		numPages = max(pages) if pages else 1
		models = parse_models(first)
		for p in range(2, numPages + 1):
			models += parse_models(fetch(path + '?page=%d' % p))
		categories[slug] = {'title': title, 'children': children, 'models': models}
		print('%-40s %-36s %3d pages %5d models' % (slug, title, numPages, len(models)))

	parents = {}
	for slug, c in categories.items():
		for child in c['children']:
			parents[child] = slug

	sampIds = set()
	for c in categories.values():
		for m in c['models']:
			if m['samp']:
				sampIds.add(m['id'])
	result = {
		'source': BASE + ROOT,
		'crawled': time.strftime('%Y-%m-%d'),
		'categories': [{
			'slug': slug,
			'title': c['title'],
			'parent': parents.get(slug),
			'models': sorted({m['id'] for m in c['models']}),
		} for slug, c in categories.items()],
		'samp': sorted(sampIds),
	}
	with open(out, 'w', encoding='utf-8') as f:
		json.dump(result, f, separators=(',', ':'))
		f.write('\n')
	print('wrote %s (%d SA-MP models)' % (out, len(sampIds)))

if __name__ == '__main__':
	main()
