# UI fonts

Inter and Font Awesome are embedded in the editor. Noto Sans SC Regular is
loaded from this folder for Chinese UI text, so it also works on systems
without a CJK font installed. Release packaging includes this folder; when
copying the executable manually, copy `fonts/` beside it too.

`NotoSansSC-Regular.otf` is the unmodified Simplified Chinese subset from
[Noto CJK](https://github.com/notofonts/noto-cjk/blob/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf),
version 2.004, upstream commit `f8d157532fbfaeda587e826d4cd5b21a49186f7c`.
SHA-256: `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9`.
Copyright (c) 2014-2021 Adobe. Its SIL Open Font License 1.1 is in
`LICENSE-NotoSansSC.txt` and Help > About > Licenses.

The fonts in `material/` are used for map material text; see
`THIRD_PARTY_NOTICES.md` for their notices.
