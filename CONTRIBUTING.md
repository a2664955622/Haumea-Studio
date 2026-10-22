# Contributing to Haumea Studio

Thanks for helping. Bug reports, ideas, translations and code are all welcome.

- **Bugs and ideas:** open an issue with the bug report or feature request form. One problem or
  idea per issue.
- **Questions:** ask on the [Haumea Discord](https://discord.gg/yTF8DWQqpP).
- **Security problems:** never in a public issue; see [SECURITY.md](SECURITY.md).

Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).

## Building and testing

[docs/BUILDING.md](docs/BUILDING.md) has the build steps for Linux, macOS and Windows and how to
run the tests (`tools/haumea/tests/run_all.sh`, or `run_all.sh --fixture` without a game folder).
Run the tests before opening a pull request; CI runs them too.

## Rules for changes

- **open.mp and SA-MP.** Generated Pawn must work on open.mp and on SA-MP (0.3.7, and 0.3.DL for
  custom models), and compile with 0 errors and 0 warnings against both the open.mp and the SA-MP
  0.3.7 includes. A feature that only exists on open.mp is labelled open.mp-only in the UI, and
  Map Check warns when a map uses it.
- **Mark UI text for translation.** Every string shown in the UI goes through the i18n macros in
  `tools/haumea/i18n.h`:
  - `_("Text")` for text;
  - `L("Text")` for a label that is also an ImGui ID (windows, tabs, headers), so the ID stays
    the same in every language;
  - `LI(ICON_FA_..., "Text")` and `TI(ICON_FA_..., "Text")` with an icon;
  - `N_("Text")` in static tables, then `_(entry)` where it is drawn.

  Translate whole sentences: `_("%d objects selected")`, never pieces glued together. Pawn code,
  native and model names, file names, log lines and text the agent (Namaka) reads stay English.
- **One topic per pull request.** A bug fix and an unrelated cleanup are two pull requests.
- **Commits:** a short title that says what changed (often `Area: what changed`), and a body that
  says why when it isn't obvious.
- **UI changes:** add screenshots to the pull request.

The [pull request template](.github/PULL_REQUEST_TEMPLATE.md) has the checklist.

## License

Haumea Studio is licensed under the GNU General Public License, version 2 or later. By sending a
contribution you agree that it is released under the same license (GPL-2.0-or-later). Code taken
from elsewhere must have a compatible license and be listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Adding or improving a language

Translations are plain `.po` files in `tools/haumea/lang/`, one per language (`pt-BR.po` is
Brazilian Portuguese). Every `lang/<code>.po` the editor finds shows up in Preferences >
Language, and updates deliver new catalogs.

1. Generate the strings to translate:
   `python3 tools/i18n/extract.py --skeleton tools/haumea/*.cpp > my-language.po`
   (`extract.py` with no arguments writes the template, `tools/haumea/lang/haumea.pot`).
2. Translate each `msgstr`. Keep every `%d`, `%s` and similar placeholder, in the same order,
   and keep `\n` line breaks.
3. For a new language, save it as `tools/haumea/lang/<code>.po` (for example `es.po`) with a
   `Language-Name` header in its own language (`"Language-Name: Español\n"`).
4. Check it: `python3 tools/i18n/extract.py --catalogs` reports wrong placeholders, and
   `tools/haumea/tests/test_i18n.py` runs the catalog tests.

Use the terms in the glossary ([docs/i18n/GLOSSARY.pt-BR.md](docs/i18n/GLOSSARY.pt-BR.md) for
pt-BR): SA-MP and open.mp terms such as gang zone, pickup, checkpoint, textdraw and NPC stay as
players know them.
