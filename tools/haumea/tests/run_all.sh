#!/bin/sh
# run_all.sh [--fixture] [--update-expected] [--windows]: every Haumea test on
# this machine.
#
#  1. The unit tests in this folder (python3 -m unittest). Tests that need the
#     editor binary, the game, a display or a built librw skip with a reason.
#  2. The Pawn exporter: each map in pawn/maps is opened and saved by the
#     editor (--pawn-roundtrip). The output must match pawn/expected and save
#     again unchanged. Needs the game folder; skipped without it.
#  3. Every generated map compiles as a gamemode with 0 errors and 0
#     warnings against open.mp's and SA-MP 0.3.7's includes
#     (pawn/check_pawn.sh; the compiler and includes are downloaded once).
#  4. Every generated map and the showcase run on open.mp's Linux server with
#     an NPC as the player: the per-player code (gang zones, map icons,
#     removed buildings, textdraws, sounds) succeeds for it and the map's NPCs
#     exist and move (pawn/server_check.py). Skipped without the server.
#  5. With --windows: the Windows build (gl3 and d3d9) cross-compiled with
#     MinGW-w64 and run under Wine in a fresh prefix must save every test map
#     exactly like the Linux editor (windows_check.py). Skipped, with the
#     reason, without MinGW-w64 or Wine.
#
# The game folder is $HAUMEA_GAME_DIR, else the one Haumea has saved, else
# (or with --fixture) a synthetic one made by fixture/make_game.py with no
# game files, as in CI. The editor is $HAUMEA_BINARY, else
# bin/*/Release/haumea; the run fails at once without it, or when it is older
# than a source file in tools/haumea. --update-expected replaces pawn/expected with the
# editor's output after an intended change.
set -u
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH='' cd -- "$HERE/../../.." && pwd)
UPDATE=0
FIXTURE=0
WINDOWS=0
for arg in "$@"; do
	case $arg in
	--update-expected) UPDATE=1 ;;
	--fixture) FIXTURE=1 ;;
	--windows) WINDOWS=1 ;;
	*) echo "usage: run_all.sh [--fixture] [--update-expected] [--windows]" >&2; exit 2 ;;
	esac
done
# The editors these tests start (and Wine with --windows) open windows: on
# Linux they only ever go to an Xvfb of this run's own, never the user's
# display, where they would take focus and the user's keys would reach them.
if [ "$(uname -s)" = Linux ] && [ "${HAUMEA_TEST_XVFB:-}" != 1 ]; then
	if ! command -v xvfb-run > /dev/null; then
		echo "FAIL: xvfb-run is needed: the tests never open the editor on the real display"
		exit 1
	fi
	# Unsetting WAYLAND_DISPLAY isn't enough: libwayland then tries the
	# session's default socket. Point it nowhere and say this is X11.
	RUNTIME=$(mktemp -d)	# 0700: no desktop sockets in the session's runtime folder
	exec env -u DISPLAY -u XAUTHORITY -u WAYLAND_SOCKET WAYLAND_DISPLAY=/nonexistent/haumea-test \
		XDG_SESSION_TYPE=x11 XDG_RUNTIME_DIR="$RUNTIME" HAUMEA_TEST_XVFB=1 \
		xvfb-run -a -s "-screen 0 1600x1000x24" sh "$0" "$@"
fi
status=0
SYNTHETIC=0

[ "$FIXTURE" = 1 ] && HAUMEA_GAME_DIR=
if [ -z "${HAUMEA_GAME_DIR:-}" ] && [ "$FIXTURE" = 0 ]; then
	CFG=${XDG_CONFIG_HOME:-$HOME/.config}/haumea/workspace.cfg
	[ -f "$CFG" ] && HAUMEA_GAME_DIR=$(sed -n 's/^game //p' "$CFG" | head -n 1)
fi
if [ -z "${HAUMEA_BINARY:-}" ]; then
	for b in "$ROOT"/bin/*/Release/haumea; do [ -x "$b" ] && HAUMEA_BINARY=$b && break; done
fi
# Without the editor, or with one built before the latest source change, the
# tests that need it skip or test old code, and the run would still pass.
if [ -z "${HAUMEA_BINARY:-}" ] || [ ! -x "$HAUMEA_BINARY" ]; then
	echo "FAIL: no editor binary: build bin/*/Release/haumea or set HAUMEA_BINARY"
	exit 1
fi
NEWER=$(find "$ROOT/tools/haumea" \( -name '*.cpp' -o -name '*.h' \) -newer "$HAUMEA_BINARY" | head -n 1)
if [ -n "$NEWER" ]; then
	echo "FAIL: $HAUMEA_BINARY is older than $NEWER: rebuild the editor"
	exit 1
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
if [ -z "${HAUMEA_GAME_DIR:-}" ] || [ ! -d "$HAUMEA_GAME_DIR" ]; then
	echo "== synthetic game folder (no GTA files)"
	if python3 "$HERE/fixture/make_game.py" "$WORK/game" > /dev/null; then
		HAUMEA_GAME_DIR=$WORK/game
		SYNTHETIC=1
	else
		echo "could not make the synthetic game folder"
		status=1
	fi
fi
export HAUMEA_GAME_DIR HAUMEA_BINARY
# Nothing started from here may touch the user's own settings
# (~/.config/haumea, %APPDATA%\Haumea): everything gets a private config.
mkdir -p "$WORK/config"
XDG_CONFIG_HOME=$WORK/config
APPDATA=$WORK/config
HAUMEA_NO_MIGRATE=1
export XDG_CONFIG_HOME APPDATA HAUMEA_NO_MIGRATE

# The Pawn compiler and includes, also for the tests that compile Pawn.
PAWN_TOOLCHAIN=${PAWN_TOOLCHAIN:-$HERE/pawn/.toolchain}
export PAWN_TOOLCHAIN
if "$HERE/pawn/fetch_toolchain.sh" "$PAWN_TOOLCHAIN"; then
	PAWNCC=${PAWNCC:-$PAWN_TOOLCHAIN/bin/pawncc}
	OMP_INCLUDE=${OMP_INCLUDE:-$PAWN_TOOLCHAIN/omp/Server/qawno/include}
	SAMP037_INCLUDE=${SAMP037_INCLUDE:-$PAWN_TOOLCHAIN/samp037}
	export PAWNCC OMP_INCLUDE SAMP037_INCLUDE
fi

# Namaka's installer tests (test_namaka_install, namaka.uis) need its wheels:
# fetched once into a cache, like the toolchain, so they don't skip unseen.
if [ -z "${NAMAKA_WHEELHOUSE:-}" ]; then
	NAMAKA_WHEELHOUSE=$HERE/.wheelhouse
	if [ ! -f "$NAMAKA_WHEELHOUSE/.complete" ]; then
		rm -rf "$NAMAKA_WHEELHOUSE"
		if sh "$ROOT/tools/release/fetch_wheelhouse.sh" linux "$NAMAKA_WHEELHOUSE" > "$WORK/wheelhouse.log" 2>&1; then
			touch "$NAMAKA_WHEELHOUSE/.complete"
		else
			echo "WARNING: could not fetch Namaka's wheelhouse (needs uv and the network); its tests will skip:"
			tail -n 3 "$WORK/wheelhouse.log"
			NAMAKA_WHEELHOUSE=
		fi
	fi
fi
export NAMAKA_WHEELHOUSE

echo "== unit tests"
{ (cd "$HERE" && python3 -m unittest discover -v -p "test_*.py") 2>&1; echo $? > "$WORK/unit.status"; } |
	tee "$WORK/unit.log"
[ "$(cat "$WORK/unit.status")" = 0 ] || status=1
# Skips this mode expects: scripts that need the real game, the showcase's
# real-game models, and the rotation test's own librw build. Anything else
# (no display, no toolchain, no editor) means the run tested less than it says.
KNOWN="set LIBRW to a built librw"
[ "$SYNTHETIC" = 1 ] && KNOWN="$KNOWN|needs the real game|synthetic fixture"
SKIPPED=$(grep -c " skipped '" "$WORK/unit.log")
echo "$SKIPPED test(s) skipped"
UNEXPECTED=$(grep " skipped '" "$WORK/unit.log" | grep -v -E "$KNOWN")
if [ -n "$UNEXPECTED" ]; then
	echo "WARNING: $(echo "$UNEXPECTED" | wc -l) test(s) skipped for a reason this mode doesn't expect:"
	echo "$UNEXPECTED"
fi

echo "== Pawn exporter round trip"
GENERATED=$HERE/pawn/expected
if [ -z "${HAUMEA_GAME_DIR:-}" ] || [ ! -d "$HAUMEA_GAME_DIR" ] || [ -z "${HAUMEA_BINARY:-}" ]; then
	echo "skipped: needs the game folder (HAUMEA_GAME_DIR) and a built editor; compiling pawn/expected instead"
else
	mkdir -p "$WORK/v1" "$WORK/v2"
	for map in "$HERE"/pawn/maps/*.pwn; do
		name=$(basename "$map")
		for pass in v1 v2; do
			src=$map
			[ "$pass" = v2 ] && src=$WORK/v1/$name
			"$HAUMEA_BINARY" --game "$HAUMEA_GAME_DIR" \
				--pawn-roundtrip "$src" "$WORK/$pass/$name" > "$WORK/$pass-$name.log" 2>&1
			if [ ! -f "$WORK/$pass/$name" ]; then
				echo "FAIL $name: the editor wrote no output ($pass)"
				tail -5 "$WORK/$pass-$name.log"
				status=1
			fi
		done
		[ -f "$WORK/v2/$name" ] || continue
		if ! cmp -s "$WORK/v1/$name" "$WORK/v2/$name"; then
			echo "FAIL $name: saving the saved map changes it"
			diff "$WORK/v1/$name" "$WORK/v2/$name" | head -10
			status=1
		elif [ "$UPDATE" = 1 ]; then
			cp "$WORK/v1/$name" "$HERE/pawn/expected/$name"
			echo "updated expected/$name"
		elif ! cmp -s "$WORK/v1/$name" "$HERE/pawn/expected/$name"; then
			echo "FAIL $name: the output differs from pawn/expected (run with --update-expected if intended)"
			diff "$HERE/pawn/expected/$name" "$WORK/v1/$name" | head -20
			status=1
		else
			echo "ok   $name"
		fi
	done
	GENERATED=$WORK/v1
fi

echo "== Pawn compile"
"$HERE/pawn/check_pawn.sh" "$GENERATED" || status=1

echo "== Pawn on the server (an NPC as the player)"
python3 "$HERE/pawn/server_check.py" "$GENERATED" "$HERE/showcase"
case $? in
	0) ;;
	2) ;;	# no server package: the script said why
	*) status=1 ;;
esac

if [ "$WINDOWS" = 1 ]; then
	echo "== Windows build under Wine"
	python3 "$HERE/windows_check.py"
	case $? in
		0) ;;
		2) ;;	# no MinGW-w64 or Wine: the script said why
		*) status=1 ;;
	esac
fi

[ "$status" = 0 ] && echo "All tests passed." || echo "Some tests failed."
exit $status
