#!/usr/bin/env python3
"""Merge the pt-BR staging files (tools/i18n/pt-BR/*.po) into lang/pt-BR.po.

  merge.py            write tools/haumea/lang/pt-BR.po
  merge.py --check    fail if it is out of date or a staging file has a msgid
                      no source file marks any more (remove it)

Entries come out in the order of lang/haumea.pot; a msgid translated two ways
in different files is reported and fails. The Language-Name header is kept.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract  # noqa: E402

OUT = os.path.join(extract.LANG, 'pt-BR.po')


def build():
    found = extract.collect()
    seen = {}
    conflicts = []
    # The staging files are the source; lang/pt-BR.po is only read when there are none.
    paths = [p for p in extract.pt_br_paths() if os.path.dirname(p) == extract.STAGING] or extract.pt_br_paths()
    for path in paths:
        with open(path, encoding='utf-8') as f:
            for msgid, msgstr, fuzzy in extract.parse_po(f.read()):
                if not msgid or not msgstr or fuzzy:
                    continue
                if msgid in seen and seen[msgid][0] != msgstr:
                    conflicts.append((msgid, seen[msgid], (msgstr, path)))
                else:
                    seen.setdefault(msgid, (msgstr, path))
    lines = [
        '# Haumea Studio, Portuguese (Brazil). Made by tools/i18n/merge.py from',
        '# tools/i18n/pt-BR/*.po; see docs/i18n/GLOSSARY.pt-BR.md.',
        'msgid ""',
        'msgstr ""',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        '"Language-Name: Português (Brasil)\\n"',
        '',
    ]
    for text in sorted(found):
        if text not in seen:
            continue
        if '%' in text:
            lines.append('#, c-format')
        lines.append('msgid ' + extract.po_quote(text))
        lines.append('msgstr ' + extract.po_quote(seen[text][0]))
        lines.append('')
    unused = sorted(t for t in seen if t not in found)
    return '\n'.join(lines), conflicts, unused


def seen_in(msgid):
    for path in extract.pt_br_paths():
        if os.path.dirname(path) == extract.STAGING:
            with open(path, encoding='utf-8') as f:
                if any(m == msgid for m, _, _ in extract.parse_po(f.read())):
                    return path
    return ''


def main(argv):
    text, conflicts, unused = build()
    for msgid in unused:
        print('unused (no source file marks it; left out of lang/pt-BR.po): %r in %s' % (msgid, os.path.basename(seen_in(msgid))))
    for msgid, a, b in conflicts:
        print('conflict: %r -> %r (%s) and %r (%s)' % (msgid, a[0], os.path.basename(a[1]), b[0], os.path.basename(b[1])))
    if conflicts or (unused and '--check' in argv):
        return 1
    if '--check' in argv:
        try:
            with open(OUT, encoding='utf-8') as f:
                same = f.read() == text
        except OSError:
            same = False
        if not same:
            print('lang/pt-BR.po is out of date: run tools/i18n/merge.py')
        return 0 if same else 1
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print('wrote %s' % os.path.relpath(OUT, extract.ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
