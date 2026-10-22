#!/usr/bin/env python3
"""Collect the UI strings marked in tools/haumea for translation.

  extract.py            write tools/haumea/lang/haumea.pot
  extract.py --check    fail if the .pot is out of date
  extract.py --catalogs fail if a lang/*.po or staging file has a translation
                        whose printf conversions differ from the English text
                        (the editor ignores those at run time; this finds them)
  extract.py --skeleton FILE...
                        a .po skeleton of the strings marked in those source
                        files that pt-BR (lang/pt-BR.po and tools/i18n/pt-BR/*.po)
                        does not translate yet; redirect it into your staging
                        file tools/i18n/pt-BR/<session>.po

Marks: _("Text"), N_("Text"), L("Text"), LI(ICON, "Text"), TI(ICON, "Text").
"Text##id" is collected as "Text". English is the key.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SRC = os.path.join(ROOT, 'tools', 'haumea')
LANG = os.path.join(SRC, 'lang')
POT = os.path.join(LANG, 'haumea.pot')
STAGING = os.path.join(ROOT, 'tools', 'i18n', 'pt-BR')

MARK = re.compile(r'(?<![A-Za-z0-9_])(_|N_|L|LI|TI)\s*\(')


def skip_ws_comments(s, i):
    while i < len(s):
        if s[i].isspace():
            i += 1
        elif s.startswith('//', i):
            i = s.find('\n', i)
            if i < 0:
                return len(s)
        elif s.startswith('/*', i):
            j = s.find('*/', i + 2)
            i = len(s) if j < 0 else j + 2
        else:
            break
    return i


def decode_escape(s, i, out):
    """The escape at s[i] == '\\' appended to out (bytes) as the compiler reads it; the index after it."""
    i += 1
    c = s[i] if i < len(s) else ''
    simple = {'n': 10, 't': 9, 'r': 13, 'a': 7, 'b': 8, 'f': 12, 'v': 11, '\\': 92, '"': 34, "'": 39, '?': 63}
    if c in simple:
        out.append(simple[c])
        return i + 1
    if c == 'x':
        j = i + 1
        while j < len(s) and s[j] in '0123456789abcdefABCDEF':
            j += 1
        if j > i + 1:
            out.append(int(s[i + 1:j], 16) & 0xFF)
        return j
    if c in '01234567':
        j = i
        while j < len(s) and j < i + 3 and s[j] in '01234567':
            j += 1
        out.append(int(s[i:j], 8) & 0xFF)
        return j
    out += ('\\' + c).encode('utf-8')
    return i + 1


def read_literal(s, i):
    """Adjacent string literals at s[i] as one value; (value, end) or None.
    Escapes make bytes, read as UTF-8, so "\\xC2\\xB1" is the plus-minus sign."""
    out = bytearray()
    got = False
    while True:
        i = skip_ws_comments(s, i)
        if i >= len(s) or s[i] != '"':
            break
        got = True
        i += 1
        while i < len(s) and s[i] != '"':
            if s[i] == '\\' and i + 1 < len(s):
                i = decode_escape(s, i, out)
            else:
                out += s[i].encode('utf-8')
                i += 1
        i += 1
    return (out.decode('utf-8', errors='replace'), i) if got else None


def skip_argument(s, i):
    """From the start of an argument to its top-level comma or the closing paren."""
    depth = 0
    while i < len(s):
        c = s[i]
        if c == '"':
            i += 1
            while i < len(s) and s[i] != '"':
                i += 2 if s[i] == '\\' else 1
        elif c in '([{':
            depth += 1
        elif c in ')]}':
            if depth == 0:
                return i
            depth -= 1
        elif c == ',' and depth == 0:
            return i
        i += 1
    return i


def line_of(s, pos):
    return s.count('\n', 0, pos) + 1


def scan(path, found):
    with open(path, encoding='utf-8', errors='replace') as f:
        s = f.read()
    rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
    for m in MARK.finditer(s):
        kind = m.group(1)
        i = m.end()
        if kind in ('LI', 'TI'):
            i = skip_argument(s, i)
            if i >= len(s) or s[i] != ',':
                continue
            i += 1
        lit = read_literal(s, i)
        if lit is None:
            continue
        text = lit[0].split('##')[0]
        if text:
            found.setdefault(text, []).append('%s:%d' % (rel, line_of(s, m.start())))


def collect():
    found = {}
    for name in sorted(os.listdir(SRC)):
        if name.endswith(('.cpp', '.h')) and name != 'i18n.h':
            scan(os.path.join(SRC, name), found)
    return found


def po_quote(text):
    parts = text.split('\n')
    out = []
    for n, p in enumerate(parts):
        p = p.replace('\\', '\\\\').replace('"', '\\"').replace('\t', '\\t').replace('\r', '\\r')
        if n < len(parts) - 1:
            p += '\\n'
        if p or not out and n == len(parts) - 1:
            out.append('"%s"' % p)
    if len(out) == 1:
        return out[0]
    return '""\n' + '\n'.join(out)


def make_pot(found):
    lines = [
        '# Haumea Studio UI strings. Made by tools/i18n/extract.py; do not edit.',
        '# English is the key: copy this to <code>.po and fill in the msgstr lines.',
        'msgid ""',
        'msgstr ""',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        '"Language-Name: English\\n"',
        '',
    ]
    for text in sorted(found):
        refs = found[text]
        lines.append('#: ' + ' '.join(refs[:4]) + (' ...' if len(refs) > 4 else ''))
        if '%' in text:
            lines.append('#, c-format')
        lines.append('msgid ' + po_quote(text))
        lines.append('msgstr ""')
        lines.append('')
    return '\n'.join(lines)


def conversions(fmt):
    out = []
    i = 0
    while i < len(fmt):
        if fmt[i] != '%':
            i += 1
            continue
        i += 1
        if i < len(fmt) and fmt[i] == '%':
            i += 1
            continue
        while i < len(fmt) and fmt[i] in '-+ #0123456789.*':
            i += 1
        while i < len(fmt) and fmt[i] in 'hlLzjtq':
            i += 1
        if i < len(fmt):
            out.append(fmt[i])
            i += 1
    return out


def parse_po(text):
    """[(msgid, msgstr, fuzzy)] in file order."""
    entries = []
    cur = None
    target = None
    fuzzy = False
    for raw in text.split('\n'):
        line = raw.strip()
        if not line:
            if cur is not None:
                entries.append(cur)
            cur, target = None, None
            continue
        if line.startswith('#'):
            if line.startswith('#,') and 'fuzzy' in line:
                fuzzy = True
            continue
        if line.startswith('msgid') and not line.startswith('msgid_plural'):
            if cur is not None:
                entries.append(cur)
            cur = ['', '', fuzzy]
            fuzzy = False
            target = 0
            line = line[5:].strip()
        elif line.startswith('msgstr'):
            target = 1 if cur is not None else None
            line = line.split(None, 1)[1] if ' ' in line else ''
        elif not line.startswith('"'):
            target = None
            continue
        if target is not None and cur is not None and line.startswith('"'):
            lit = read_literal(line, 0)
            if lit:
                cur[target] += lit[0]
    if cur is not None:
        entries.append(cur)
    return entries


def po_files():
    """Every catalog and staging file: [(label, path)]."""
    out = []
    for folder in (LANG, STAGING):
        if os.path.isdir(folder):
            for name in sorted(os.listdir(folder)):
                if name.endswith('.po'):
                    out.append((os.path.relpath(os.path.join(folder, name), ROOT).replace(os.sep, '/'),
                                os.path.join(folder, name)))
    return out


def translated(paths):
    """{msgid: msgstr} of the non-empty, non-fuzzy entries of the files."""
    out = {}
    for path in paths:
        with open(path, encoding='utf-8') as f:
            for msgid, msgstr, fuzzy in parse_po(f.read()):
                if msgid and msgstr and not fuzzy:
                    out[msgid] = msgstr
    return out


def pt_br_paths():
    paths = []
    main_po = os.path.join(LANG, 'pt-BR.po')
    if os.path.isfile(main_po):
        paths.append(main_po)
    if os.path.isdir(STAGING):
        paths += [os.path.join(STAGING, n) for n in sorted(os.listdir(STAGING)) if n.endswith('.po')]
    return paths


def check_catalogs():
    bad = 0
    for label, path in po_files():
        with open(path, encoding='utf-8') as f:
            entries = parse_po(f.read())
        for msgid, msgstr, fuzzy in entries:
            if msgid and msgstr and conversions(msgid) != conversions(msgstr):
                print('%s: placeholders differ: %r -> %r' % (label, msgid, msgstr))
                bad += 1
    return bad


def skeleton(files):
    wanted = {os.path.basename(f) for f in files}
    done = translated(pt_br_paths())
    found = collect()
    lines = ['# pt-BR translations of the strings in: ' + ' '.join(sorted(wanted)),
             'msgid ""', 'msgstr ""', '"Content-Type: text/plain; charset=UTF-8\\n"', '']
    count = 0
    for text in sorted(found):
        if text in done:
            continue
        if not any(os.path.basename(r.rsplit(':', 1)[0]) in wanted for r in found[text]):
            continue
        lines.append('#: ' + ' '.join(found[text][:4]))
        if '%' in text:
            lines.append('#, c-format')
        lines.append('msgid ' + po_quote(text))
        lines.append('msgstr ""')
        lines.append('')
        count += 1
    sys.stderr.write('%d string(s) to translate\n' % count)
    return '\n'.join(lines)


def main(argv):
    if '--catalogs' in argv:
        bad = check_catalogs()
        print('%d mismatched translation(s)' % bad if bad else 'catalogs ok')
        return 1 if bad else 0
    if '--skeleton' in argv:
        print(skeleton([a for a in argv if not a.startswith('--')]))
        return 0
    pot = make_pot(collect())
    if '--check' in argv:
        try:
            with open(POT, encoding='utf-8') as f:
                same = f.read() == pot
        except OSError:
            same = False
        if not same:
            print('lang/haumea.pot is out of date: run tools/i18n/extract.py')
        return 0 if same else 1
    os.makedirs(LANG, exist_ok=True)
    with open(POT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(pot)
    print('wrote %s (%d strings)' % (os.path.relpath(POT, ROOT), pot.count('\nmsgid ') ))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
