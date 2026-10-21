# SPDX-License-Identifier: GPL-2.0-or-later
"""Function names for a report's haumea frames, from the release's
haumea-symbols-<version>.zip:

    linux-amd64/haumea.debug    addr2line (or llvm-symbolizer)
    windows-gl3/haumea.pdb      llvm-symbolizer (offsets are RVAs)
    macos-arm64/haumea.dSYM/    llvm-symbolizer on the DWARF, at 0x100000000 + offset

Frame offsets are from the module's load base, so they go to the tools as
they are (plus the Mach-O base on macOS). Zips are cached in a folder and,
when allowed, fetched once from the GitHub release v<version>-<channel>.
"""
import shutil
import subprocess
import threading
import urllib.request
import zipfile
from pathlib import Path

OWN = ('haumea', 'haumea.exe')
PLATFORM = {'linux': 'linux-amd64', 'windows': 'windows-gl3', 'macos': 'macos-arm64'}
MACHO_BASE = 0x100000000


def debug_file(folder, os_name):
	"""The file to give the symbolizer for this OS's build, or None."""
	base = Path(folder) / PLATFORM[os_name]
	if os_name == 'linux':
		f = base / 'haumea.debug'
	elif os_name == 'windows':
		f = base / 'haumea.pdb'
	else:
		f = base / 'haumea.dSYM' / 'Contents' / 'Resources' / 'DWARF' / 'haumea'
	return f if f.is_file() else None


def run_symbolizer(debug, os_name, offsets):
	"""Function and file:line for each offset, as (function, where) pairs."""
	addrs = [int(o, 16) + (MACHO_BASE if os_name == 'macos' else 0) for o in offsets]
	if os_name == 'linux' and shutil.which('addr2line'):
		cmd = ['addr2line', '-f', '-C', '-e', str(debug)] + [hex(a) for a in addrs]
		out = subprocess.run(cmd, capture_output=True, text=True, timeout=60).stdout.splitlines()
		return [(out[i * 2], out[i * 2 + 1]) if i * 2 + 1 < len(out) else ('??', '??:0') for i in range(len(addrs))]
	tool = shutil.which('llvm-symbolizer')
	if not tool:
		return [('??', '??:0')] * len(addrs)
	cmd = [tool, '--demangle', '--functions=linkage', f'--obj={debug}'] + [hex(a) for a in addrs]
	blocks = subprocess.run(cmd, capture_output=True, text=True, timeout=60).stdout.strip().split('\n\n')
	out = []
	for i in range(len(addrs)):
		lines = blocks[i].splitlines() if i < len(blocks) else []
		out.append((lines[0] if lines else '??', lines[1] if len(lines) > 1 else '??:0'))
	return out


class SymbolCache:
	def __init__(self, folder, releases_url, fetch=True):
		self.folder = Path(folder)
		self.releases = releases_url.rstrip('/')
		self.fetch = fetch
		self.lock = threading.Lock()
		self.missing = set()

	def symbols_dir(self, version, channel):
		"""The unpacked symbols for a version, fetched if allowed; None if
		there are none (dev and pr builds aren't released)."""
		target = self.folder / version
		if target.is_dir():
			return target
		zip_path = self.folder / f'haumea-symbols-{version}.zip'
		with self.lock:
			if target.is_dir():
				return target
			if not zip_path.is_file():
				if not self.fetch or channel not in ('production', 'beta') or version in self.missing:
					return None
				self.folder.mkdir(parents=True, exist_ok=True)
				url = f'{self.releases}/v{version}-{channel}/haumea-symbols-{version}.zip'
				tmp = zip_path.with_suffix('.part')
				try:
					with urllib.request.urlopen(url, timeout=60) as r, open(tmp, 'wb') as f:
						shutil.copyfileobj(r, f, 1 << 20)
					tmp.replace(zip_path)
				except OSError:
					tmp.unlink(missing_ok=True)
					self.missing.add(version)
					return None
			unpack(zip_path, target)
		return target

	def symbolize(self, report):
		"""Function names for every frame of the report ('' for frames
		outside haumea or without symbols); None without symbols."""
		folder = self.symbols_dir(report['version'], report['channel'])
		debug = debug_file(folder, report['os']) if folder else None
		if not debug:
			return None
		own = [i for i, f in enumerate(report['frames']) if f['module'] in OWN]
		results = run_symbolizer(debug, report['os'], [report['frames'][i]['offset'] for i in own])
		names = [''] * len(report['frames'])
		for i, (fn, where) in zip(own, results):
			names[i] = f'{fn} ({where})' if fn != '??' else ''
		return names

	def function_names(self, report):
		"""The function names of the haumea frames, innermost first (for the
		cross-version issue key); [] without symbols."""
		folder = self.symbols_dir(report['version'], report['channel'])
		debug = debug_file(folder, report['os']) if folder else None
		if not debug:
			return []
		offsets = [f['offset'] for f in report['frames'] if f['module'] in OWN]
		names = [fn for fn, _ in run_symbolizer(debug, report['os'], offsets)]
		return names if names and all(n and n != '??' for n in names[:1]) else []


def unpack(zip_path, target):
	"""Unpacks a symbols zip safely (no paths outside the target)."""
	tmp = target.with_name(target.name + '.tmp')
	shutil.rmtree(tmp, ignore_errors=True)
	with zipfile.ZipFile(zip_path) as z:
		for info in z.infolist():
			dest = (tmp / info.filename).resolve()
			if not str(dest).startswith(str(tmp.resolve()) + '/') and dest != tmp.resolve():
				raise ValueError(f'unsafe path in {zip_path.name}: {info.filename}')
		z.extractall(tmp)
	tmp.replace(target)
