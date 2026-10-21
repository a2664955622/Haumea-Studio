#!/usr/bin/env python3
#
# gen_readme_art.py
# Draws the README banner (docs/images/banner.png) and GitHub's social preview
# (docs/images/social-preview.png) in the style of the startup splash:
# night sky, the ringed dwarf planet and the Haumea Studio wordmark.
# Needs rsvg-convert; uses fonts/Inter-Regular.ttf from this repository.
#

import math
import os
import random
import subprocess
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
OUT = os.path.join(ROOT, 'docs', 'images')

def stars(w, h, count, seed, keepClear):
	rnd = random.Random(seed)
	out = []
	for _ in range(count):
		depth = 0.25 + 0.75 * rnd.random()
		x, y = rnd.random() * w, rnd.random() * h
		x0, y0, x1, y1 = keepClear
		if x0 <= x <= x1 and y0 <= y <= y1:
			continue	# keep the wordmark readable
		r = 0.5 + 1.4 * depth * depth
		a = 0.2 + 0.65 * depth * rnd.uniform(0.6, 1.0)
		out.append('<circle cx="%.1f" cy="%.1f" r="%.2f" fill="#dfe4ff" fill-opacity="%.2f"/>' % (x, y, r, a))
		if depth > 0.93:
			out.append('<circle cx="%.1f" cy="%.1f" r="%.2f" fill="#7cc4ff" fill-opacity=".10"/>' % (x, y, r * 4))
	return '\n'.join(out)

def planet(cx, cy, r):
	# Same shapes as assets/haumea-mark.svg, scaled: body 33x21, ring 50x13.
	k = r / 33.0
	return '''
<g transform="translate(%.1f %.1f) rotate(-18) scale(%.4f)">
  <ellipse cx="0" cy="0" rx="62" ry="58" fill="url(#glow)"/>
  <path d="M-50 0 A50 13 0 0 1 50 0" fill="none" stroke="#7cc4ff" stroke-opacity=".45" stroke-width="2.2" stroke-linecap="round"/>
  <ellipse cx="0" cy="0" rx="33" ry="21" fill="url(#body)"/>
  <ellipse cx="11" cy="-4" rx="6.5" ry="4.2" fill="#b4506a" fill-opacity=".8"/>
  <path d="M-50 0 A50 13 0 0 0 50 0" fill="none" stroke="#a9dcff" stroke-width="2.2" stroke-linecap="round"/>
  <ellipse cx="0" cy="0" rx="72" ry="24" fill="none" stroke="#7cc4ff" stroke-opacity=".14" stroke-width=".8"/>
  <circle cx="62" cy="13" r="3.4" fill="#e7ecff"/>
  <circle cx="-44" cy="12" r="2.2" fill="#c3cbe6"/>
</g>''' % (cx, cy, k)

def defs():
	return '''
<defs>
  <linearGradient id="sky" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#07070f"/>
    <stop offset=".55" stop-color="#0c0b1d"/>
    <stop offset="1" stop-color="#170f26"/>
  </linearGradient>
  <radialGradient id="nebula" cx="50%%" cy="50%%" r="50%%">
    <stop offset="0" stop-color="#3a5bd6" stop-opacity=".22"/>
    <stop offset=".6" stop-color="#5a2a9a" stop-opacity=".10"/>
    <stop offset="1" stop-color="#5a2a9a" stop-opacity="0"/>
  </radialGradient>
  <radialGradient id="glow" cx="50%%" cy="50%%" r="50%%">
    <stop offset="0" stop-color="#4f8dff" stop-opacity=".30"/>
    <stop offset="1" stop-color="#4f8dff" stop-opacity="0"/>
  </radialGradient>
  <radialGradient id="body" cx="36%%" cy="30%%" r="80%%">
    <stop offset="0" stop-color="#f6f8ff"/>
    <stop offset=".45" stop-color="#b9c2de"/>
    <stop offset="1" stop-color="#3a4370"/>
  </radialGradient>
</defs>'''

def art(w, h, seed, title_size, title_x, title_y, planet_x, planet_y, planet_r, tagline):
	by = title_size * 0.2
	return '''<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">
%s
<rect width="%d" height="%d" fill="url(#sky)"/>
<ellipse cx="%.1f" cy="%.1f" rx="%.1f" ry="%.1f" fill="url(#nebula)"/>
%s
%s
<g font-family="Inter" fill="#f5f6ff">
  <text x="%.1f" y="%.1f" font-size="%.1f" letter-spacing="-1">Haumea<tspan fill="#bec4e6" dx="0.26em">Studio</tspan></text>
  <text x="%.1f" y="%.1f" font-size="%.1f" fill="#4f9dff">by itsneufox</text>
  <text x="%.1f" y="%.1f" font-size="%.1f" fill="#aeb2cc">%s</text>
</g>
</svg>''' % (w, h, w, h, defs(), w, h,
		planet_x - planet_r * 0.4, planet_y + planet_r * 0.2, planet_r * 3.4, planet_r * 2.6,
		stars(w, h, int(w * h / 3200), seed,
			(title_x - 20, title_y - title_size, title_x + title_size * 6.9, title_y + title_size * 0.95)),
		planet(planet_x, planet_y, planet_r),
		title_x, title_y, title_size,
		title_x + 4, title_y + by + title_size * 0.22, title_size * 0.24,
		title_x + 4, title_y + by + title_size * 0.62, title_size * 0.2, tagline)

def render(svg, path, w, h):
	with tempfile.TemporaryDirectory() as tmp:
		conf = os.path.join(tmp, 'fonts.conf')
		with open(conf, 'w') as f:
			f.write('<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "fonts.dtd"><fontconfig>'
				'<include ignore_missing="yes">/etc/fonts/fonts.conf</include>'
				'<dir>%s</dir><cachedir>%s</cachedir></fontconfig>' % (os.path.join(ROOT, 'fonts'), tmp))
		src = os.path.join(tmp, 'art.svg')
		with open(src, 'w') as f:
			f.write(svg)
		env = dict(os.environ, FONTCONFIG_FILE=conf)
		subprocess.run(['rsvg-convert', '-w', str(w), '-h', str(h), '-o', path, src], check=True, env=env)
	print('wrote', os.path.relpath(path, ROOT))

def main():
	os.makedirs(OUT, exist_ok=True)
	tagline = 'The all-in-one editor for open.mp and SA-MP'
	render(art(1600, 520, 7, 112, 110, 270, 1170, 260, 150, tagline),
		os.path.join(OUT, 'banner.png'), 1600, 520)
	# GitHub social preview: 1280x640, important content away from the edges.
	render(art(1280, 640, 11, 86, 80, 320, 960, 330, 140, tagline),
		os.path.join(OUT, 'social-preview.png'), 1280, 640)

if __name__ == '__main__':
	main()
