# Release checklist

How a Haumea release goes out, and what to watch the first time CI runs the
newer jobs. Nothing here is automatic: pushing and releasing are your call.

## How releases work

- `main` is where Haumea is developed. A push to `main` (or a pull request)
  runs CI: the Windows, Linux and macOS builds, the Linux tests, the Pawn
  compile check and the Windows-under-Wine check. It doesn't release
  anything.
- A push to **`master`** (the stable channel) or **`PE`** (pre-release, shown
  as "FLA") also runs the `release` job, after the builds, tests and Pawn
  check pass. It creates the GitHub release and its tag itself, named
  `v<version>-<channel>` (for example `v1.41.0.57-master`), with the
  Windows, Linux and macOS binaries.
- **Version numbers work as open.mp's do.** `tools/haumea/version.h` holds
  MAJOR.MINOR.PATCH (`HAUMEA_VERSION_MAJOR/MINOR/PATCH`); the build number is
  the commit count (`git rev-list --count HEAD`), and the commit its short
  hash (`-dirty` with uncommitted changes), both written by
  `tools/release/gen_buildinfo.py` into `haumea_buildinfo.h` at build time
  (0 and `unknown` without git). The full version, `1.41.0.<build>`, shows in
  About, the title bar, `haumea --version`, telemetry, the exe's version
  info, `BUILD_INFO.json` and the agent wheel (`1.41.0.<build>`, PEP 440).
- The updater offers the newest release of its channel with a higher
  version, all four parts compared. Older tags (`v1.41.0-master-abc1234`)
  read as build 0 of their X.Y.Z.
- **Every release bumps PATCH at least.** Installs from before four-part
  versions compare only X.Y.Z, so a new build of the same 1.41.0 is not an
  update to them. Also, the build number restarts when history is
  rewritten: main was squashed into one root commit on 2026-10-21, so builds
  restarted near 15. They only go up as long as main is never squashed
  again; a PATCH bump keeps releases ordered even if it is.
- **Tags by hand** go through `tools/release/tag_release.sh [--create]
  [channel]`: it prints (or makes, locally) `v1.41.0.<build>-<channel>` for
  HEAD, refuses a dirty tree and an X.Y.Z that isn't newer than that
  channel's newest tag, and never pushes. CI checks every pushed `v*` tag
  (`check-tag`, `tools/release/check_tag.py`): its X.Y.Z must be
  version.h's and its build the tagged commit's count. CI needs the full
  history for that (`fetch-depth: 0`).
- Namaka is packaged from the same commit and version by
  `tools/release/package_agent.py` (release tag `v<version>-agent-alpha`).

## Before pushing

1. `tools/haumea/version.h`: `HAUMEA_VERSION_PATCH` (at least) is higher
   than the last release's (1.41.0 now; after 1.49 comes 1.50). Keep
   `tools/agent/_version.py`'s `1.41.0.0` in step (test_version checks it).
2. `CHANGELOG.md` has a section for this version.
3. The local test matrix is green (see "Local checks" below).

## Push and release

```sh
cd /mnt/devfast/Projects/Other/ariane
git status                      # clean, on main
git log --oneline origin/main..main | wc -l   # what is about to go out
git push origin main            # CI only, no release
```

Wait for the run on `main` to be green (Actions tab). Then release:

```sh
git push origin main:master     # stable: the release job publishes v1.41.0.<build>-master
# or, for a pre-release first:
git push origin main:PE         # publishes v1.41.0.<build>-PE, marked pre-release
```

Check the release page has three binaries (Windows exe, `haumea` for Linux,
`haumea-macos`), then start an older build and confirm it offers the update.

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

## Last local matrix (2026-10-21, 1.41.0, main f07bff3)

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
