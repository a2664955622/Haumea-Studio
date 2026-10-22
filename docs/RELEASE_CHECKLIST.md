# Release checklist

How a Haumea Studio release goes out, and what to watch the first time CI runs the
newer jobs. Nothing here is automatic: pushing and releasing are your call.

## How releases work

- `main` is where Haumea Studio is developed. A push to any branch (or a pull
  request) runs CI: the Windows, Linux and macOS builds, the Linux tests, the
  Pawn compile check and the Windows-under-Wine check. It doesn't release
  anything; those builds call themselves channel `dev` (`pr` for pull
  requests).
- **A release comes from a manual run or a pushed tag.** The two channels
  are **production** (what everyone gets) and **beta** (opt-in in the
  editor; published as a GitHub prerelease). A manual run (Actions → Build
  Haumea Studio → Run workflow) takes three inputs: **Channel** (beta or
  production), **Release** (build only, draft or publish) and **Notes**
  (optional text above the CHANGELOG section). A pushed tag
  `v1.0.0.<build>-<channel>` publishes straight away; CI's `check-tag` job
  checks it first (`tools/release/check_tag.py`: version.h's 1.0.0, the
  tagged commit's commit count, channel production or beta).
  `tools/release/release_plan.py` decides what a run may release: only beta
  and production, production only from main, never over an existing tag.
  The release has the update bundles, the Namaka bundles and `SHA256SUMS`
  (see `.github/workflows/build-haumea.yml`).
- **Version numbers work as open.mp's do.** `tools/haumea/version.h` holds
  MAJOR.MINOR.PATCH (`HAUMEA_VERSION_MAJOR/MINOR/PATCH`, 1.0.0 now); the
  build number is the commit count (`git rev-list --count HEAD`), and the
  commit its short hash (`-dirty` with uncommitted changes), both written by
  `tools/release/gen_buildinfo.py` into `haumea_buildinfo.h` at build time
  (0 and `unknown` without git). The full version, `1.0.0.<build>`, shows in
  About, the title bar, `haumea --version`, telemetry, the exe's version
  info, `BUILD_INFO.json` and Namaka's wheel (`1.0.0.<build>`, PEP 440). CI
  checks out the full history for the count (`fetch-depth: 0`).
- The build number only goes up as long as main's history is never
  rewritten again (main was squashed into one root commit on 2026-10-21).
  Don't squash or rewrite main; if it ever happens, bump PATCH so the next
  release still sorts above the last.
- The updater offers the newest release of the channel it follows with a
  higher version, all four parts compared.

## Before releasing

1. `tools/haumea/version.h`: the version is right for this release (bump
   MINOR or PATCH as the changes deserve). Keep `tools/agent/_version.py`'s
   `1.0.0.0` in step (test_version checks it).
2. `CHANGELOG.md` has a section for this version.
3. The local test matrix is green (see "Local checks" below), and so is CI
   on `main`.

## Release (the usual way: a manual run, as a draft)

1. Push main and wait for its CI run to be green (Actions tab). That run is
   channel `dev` and releases nothing.

   ```sh
   cd /mnt/devfast/Projects/Other/ariane
   git status                      # clean, on main
   git push origin main
   ```

2. Actions → **Build Haumea Studio** → **Run workflow**, from the **main**
   branch: Channel **beta** (or production), Release **draft**, Notes empty.
   The run builds main's commit as that channel and creates a **draft**
   release `v1.0.0.<build>-<channel>` (a prerelease for beta). Nothing is
   public yet, and no one is offered the update.
3. Check the draft on the Releases page:
   - the name and tag say the right version and channel;
   - the assets: `haumea-windows-gl3.tar.gz`, `haumea-linux-amd64.tar.gz`,
     `haumea-macos.zip`, `haumea-namaka-windows-x86_64.zip`,
     `haumea-namaka-linux-x86_64.zip`, `SHA256SUMS` (and, once crash reports
     are on main, `haumea-symbols-<version>.zip`, which the report server
     fetches);
   - download one bundle and check it against `SHA256SUMS`.
4. Edit the draft's text: the body is the run's notes, this version's
   CHANGELOG section and the license line. For the first beta, paste the
   reviewed release notes over it.
5. **Publish release.** Publishing creates the tag on GitHub; `git fetch
   --tags` brings it here.
6. Start a build of that channel from before the release and confirm it
   offers the update, installs it at the next start, and **Help → Roll
   back** returns.

Release **publish** in step 2 skips the draft; use it only once the draft
way has worked.

## Release by tag (the alternative)

```sh
tools/release/tag_release.sh --create beta       # v1.0.0.<build>-beta, local only
git push origin <the tag it printed>             # this publishes it
```

`tag_release.sh` refuses a dirty tree, a channel other than production or
beta, and a version that isn't newer than that channel's newest tag. The tag
run publishes at once, with no draft to check, so prefer the manual run.

## What to watch in the first CI run

These jobs or steps have never run on GitHub; everything below was checked
locally only.

**windows-wine** (new; not required for releases yet)
- (a) Compiler: CI uses Ubuntu 24.04's MinGW-w64 (g++ 13, posix threads);
  locally it was g++ 16.2. Look for compile errors in the "Check the Windows
  build under Wine" step's build output.
- (b) Wine: CI pins WineHQ `winehq-stable` 11.0.0.0~noble-1; locally it was
  Wine 11.20. A difference shows as "Windows output differs from Linux" or a
  crash.
- (c) Rendering: the capture check draws with Mesa's software renderer under
  Xvfb. If only the "capture" lines fail, the round trips still prove the
  build; run the check with `--no-capture` and look at the uploaded
  `windows-check` artifact (it has the frames and outputs).
- (d) Wine install: the WineHQ key and `winehq-noble.sources` URLs, and the
  `.deb` cache (`winehq-11.0.0.0~noble-1`). If the install step fails, check
  those URLs first.

**macOS** (`build-macos`): it has been built locally only on Linux, never on
a Mac runner since the workspace, test server, textdraw and Windows changes.
Watch for missing headers or frameworks; the release job needs it.

**build-windows** (MSVC): now also compiles `tools/haumea/haumea.exe.manifest`
into the exe (UTF-8 paths). If the link step complains about a duplicate
manifest, that's the cause.

**test-linux**: runs the UI scripts under Xvfb; they need the ImGui test
hooks (`-DIMGUI_ENABLE_TEST_ENGINE`, in `build/haumea.make`), which a fresh
CI build always has.

## Local checks

```sh
tools/haumea/tests/run_all.sh                 # with the real game (HAUMEA_GAME_DIR)
tools/haumea/tests/run_all.sh --fixture       # as CI: synthetic game folder
tools/haumea/tests/run_all.sh --windows       # adds the MinGW + Wine check
```

After pulling a commit that changes `build/haumea.make`'s defines, delete
`build/obj/*/Release/haumea/imgui*.o` before building: make doesn't rebuild
on flag changes.

## Windows check (2026-10-22, main a5765cb)

`windows_check.py --gfx gl3,d3d9` (Wine 11.20, fresh prefix, Xvfb,
synthetic game): green. Both exes built; all 13 maps round-trip byte for
byte like Linux; capture_screen real; Namaka listens on loopback TCP and a
second start leaves it alone. The look-button cursor lock isn't exercised by
it (no mouse held): try it by hand on Windows once.

## Last local matrix (2026-10-21, main f07bff3)

| Check | Result |
|---|---|
| `run_all.sh` with the real game | green: 26 unit tests (1 skipped), round trips, Pawn compile, server check |
| `run_all.sh --fixture` (as CI) | green: 26 unit tests (3 skipped), round trips, Pawn compile, server check incl. showcase |
| UI scripts (in the unit tests) | green in both modes |
| `windows_check.py` (gl3 and d3d9, Wine 11.20, xvfb) | green: 7/7 maps identical to Linux for each (hv_text included), captures real |
| ASan build: `--pawn-roundtrip` on all 7 test maps | no reports; hv_text's Windows-1252 text unchanged |
| ASan build: `--undo-selftest` on every_element.pwn | 30/30, no reports |

The two `run_all.sh` rows were run on the commits just before the squash
into f07bff3 (the same tree apart from the text-encoding change and four
small edits); the Windows and ASan rows ran on f07bff3 itself, and the unit
tests passed there too (27, 3 skipped). One failure on the way,
`test_outliner` in `--fixture` mode (models missing from the synthetic
game), was fixed by its owner before these runs.
