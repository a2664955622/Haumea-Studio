## What and why

<!-- What this changes and why. One topic per PR; link the issue if there is one ("Fixes #123"). -->

## How it was tested

- [ ] `tools/haumea/tests/run_all.sh` passes (with `--fixture` if you have no game folder)
- [ ] The Pawn output compiles with 0 errors and 0 warnings against the open.mp includes and the SA-MP 0.3.7 includes (`run_all.sh` does both; open.mp-only output is marked as such)
- [ ] Windows checked, if the change touches Windows code, file paths or the build
- [ ] Not applicable: docs or CI only

<!-- Anything you tested by hand: which maps, which server, what you looked at. -->

## UI changes

- [ ] Screenshots or a short clip of the change
- [ ] New UI text is marked: `_()`, `L()` for labels that are ImGui IDs, `LI()`/`TI()` with icons, `N_()` in static tables
- [ ] pt-BR translations added for the new text (`tools/i18n/extract.py --skeleton`), and `test_i18n` passes
- [ ] Not applicable: no UI change

<!-- See CONTRIBUTING.md for the rules. -->
