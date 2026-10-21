#!/bin/sh
# rc_check.sh: the release-candidate pass on this checkout. It ends with one
# summary table, and exits 0 only when every row passed.
#
#   HAUMEA_GAME_DIR=/path/to/GTA LIBRW=/path/to/librw tools/release/rc_check.sh
#
# Steps:
#  1. run_all.sh with the real game, plus --windows (gl3 and d3d9 builds
#     under Wine in a fresh prefix, saving every test map like Linux)
#  2. run_all.sh --fixture (the synthetic game, as CI)
#  3. an AddressSanitizer build (its own obj/ and bin/ folders): the Pawn round
#     trip of every test map and --undo-selftest on every_element.pwn, with no
#     AddressSanitizer report
#  4. a fresh-config smoke test (tests/ui/rc_smoke.uis), real game and
#     synthetic: the start screen makes a new workspace, an object is placed,
#     the map saved and reopened
#  5. Namaka installed offline from the wheelhouse (test_namaka_install,
#     namaka.uis)
#  6. the updater against the fake release server, production and beta
#     (test_updater.py; "not on this commit" until the updater is merged)
#
# Steps 1 and 2 go through HANDOFF/run_tests.sh when it exists (the shared
# slots), else run_all.sh directly. Every editor runs in an Xvfb of its own,
# never on the user's display. The editor is $HAUMEA_BINARY, else
# bin/linux-amd64-gl3/Release/haumea, built from this checkout.
set -u
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
TESTS=$ROOT/tools/haumea/tests
RUN_TESTS=/mnt/devfast/Projects/Other/ariane-wt/HANDOFF/run_tests.sh

# Steps 3-6 start editors: only ever in an Xvfb of this run's own, with
# Wayland pointed nowhere (as run_all.sh does), never on the user's desktop,
# and two software render threads each (Mesa starts one per CPU otherwise).
if [ "$(uname -s)" = Linux ] && [ "${HAUMEA_RC_XVFB:-}" != 1 ]; then
	command -v xvfb-run > /dev/null || { echo "FAIL: xvfb-run is needed" >&2; exit 1; }
	RUNTIME=$(mktemp -d)
	exec env -u DISPLAY -u XAUTHORITY -u WAYLAND_SOCKET WAYLAND_DISPLAY=/nonexistent/haumea-test \
		XDG_SESSION_TYPE=x11 XDG_RUNTIME_DIR="$RUNTIME" HAUMEA_RC_XVFB=1 \
		LP_NUM_THREADS="${LP_NUM_THREADS:-2}" MESA_GLTHREAD=false GALLIUM_DRIVER=llvmpipe \
		xvfb-run -a -s "-screen 0 1600x1000x24" sh "$0" "$@"
fi

if [ -z "${HAUMEA_GAME_DIR:-}" ] || [ ! -d "$HAUMEA_GAME_DIR" ]; then
	echo "FAIL: set HAUMEA_GAME_DIR to the real GTA San Andreas folder" >&2
	exit 1
fi
HAUMEA_BINARY=${HAUMEA_BINARY:-$ROOT/bin/linux-amd64-gl3/Release/haumea}
if [ ! -x "$HAUMEA_BINARY" ]; then
	echo "FAIL: no editor at $HAUMEA_BINARY: build it first" >&2
	exit 1
fi
export HAUMEA_BINARY
WORK=$(mktemp -d)
LOGS=$ROOT/rc_check_logs
rm -rf "$LOGS"
mkdir -p "$LOGS"
: > "$WORK/rows"
echo "rc_check on $(git -C "$ROOT" rev-parse --short HEAD) ($(git -C "$ROOT" log -1 --format=%s)); logs in $LOGS"

# row name status seconds note
row() { printf '%s|%s|%s|%s\n' "$1" "$2" "$3" "$4" >> "$WORK/rows"; }

# step name log command...: run it, record PASS or FAIL by its exit status.
step() {
	name=$1; log=$LOGS/$2; shift 2
	echo "== $name"
	start=$(date +%s)
	"$@" > "$log" 2>&1
	rc=$?
	secs=$(( $(date +%s) - start ))
	if [ $rc = 0 ]; then row "$name" PASS "$secs" "$(basename "$log")"; else row "$name" FAIL "$secs" "$(basename "$log") (exit $rc)"; fi
	return $rc
}

suite() {	# suite [run_all args]: through the shared slots when there are any
	if [ -f "$RUN_TESTS" ]; then
		sh "$RUN_TESTS" "$ROOT" "$@"
	else
		HAUMEA_TEST_XVFB=1 sh "$TESTS/run_all.sh" "$@"
	fi
}

# 1-2. The full runs.
step "run_all, real game + Windows (gl3, d3d9)" run_all_real.log suite --windows
step "run_all --fixture" run_all_fixture.log suite --fixture

# 3. AddressSanitizer build and runs.
asan() {
	LIBRW=${LIBRW:-/opt/librw}
	sed -e "s#/opt/librw#$LIBRW#g" \
	    -e 's#\.\./bin/linux-amd64-gl3/Release#../bin/linux-amd64-gl3/ASan#' \
	    -e 's#obj/linux-amd64-gl3/Release/haumea#obj/linux-amd64-gl3/ASan/haumea#' \
	    -e 's#^\(ALL_LDFLAGS += .* -m64\) -s$#\1#' \
	    "$ROOT/build/haumea.make" > "$ROOT/build/.rc-asan.make"
	make -C "$ROOT/build" -f .rc-asan.make config=release_linux-amd64-gl3 -j"$(nproc)" \
		CXXFLAGS="-fsanitize=address -fno-omit-frame-pointer -g" CFLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
		LDFLAGS="-fsanitize=address" || return 1
	BIN=$ROOT/bin/linux-amd64-gl3/ASan/haumea
	[ -x "$BIN" ] || return 1
	export ASAN_OPTIONS=detect_leaks=0:abort_on_error=0	# GL drivers keep memory to the end
	mkdir -p "$WORK/asan/config"
	failed=0
	for map in "$TESTS"/pawn/maps/*.pwn; do
		name=$(basename "$map")
		XDG_CONFIG_HOME=$WORK/asan/config "$BIN" --game "$HAUMEA_GAME_DIR" \
			--pawn-roundtrip "$map" "$WORK/asan/$name" > "$WORK/asan/$name.log" 2>&1
		if grep -q 'ERROR: AddressSanitizer' "$WORK/asan/$name.log" || [ ! -f "$WORK/asan/$name" ]; then
			echo "FAIL $name"; cat "$WORK/asan/$name.log" | grep -A20 'AddressSanitizer' | head -40; failed=1
		elif ! cmp -s "$WORK/asan/$name" "$TESTS/pawn/expected/$name"; then
			echo "FAIL $name: output differs from pawn/expected"; failed=1
		else
			echo "ok   $name"
		fi
	done
	XDG_CONFIG_HOME=$WORK/asan/config "$BIN" --game "$HAUMEA_GAME_DIR" --undo-selftest \
		"$TESTS/fixtures/every_element.pwn" "$WORK/asan/undo.txt" > "$WORK/asan/undo.log" 2>&1
	cat "$WORK/asan/undo.txt" 2>/dev/null | tail -3
	if grep -q 'ERROR: AddressSanitizer' "$WORK/asan/undo.log" || ! grep -q ' 0 failed' "$WORK/asan/undo.txt" 2>/dev/null; then
		echo "FAIL undo self-test"; grep -A20 'AddressSanitizer' "$WORK/asan/undo.log" | head -40; failed=1
	fi
	return $failed
}
step "ASan: round trip of every test map + undo self-test" asan.log asan

# 4-6. Single tests, each editor in its own Xvfb (test_ui_scripts) or this run's.
unit() {	# unit game-dir unittest-args...: from the tests folder
	game=$1; shift
	(cd "$TESTS" && HAUMEA_TEST_XVFB=1 HAUMEA_GAME_DIR=$game python3 -m unittest -v "$@")
}
needs_ok() {	# needs_ok log: the run passed and skipped nothing
	! grep -q " skipped '" "$1"
}
fresh() {
	unit "$HAUMEA_GAME_DIR" test_ui_scripts.UiScriptTests.test_rc_smoke || return 1
	mkdir -p "$WORK/fixture"
	python3 "$TESTS/fixture/make_game.py" "$WORK/fixture/game" > /dev/null || return 1
	unit "$WORK/fixture/game" test_ui_scripts.UiScriptTests.test_rc_smoke
}
step "Fresh config: start screen, new workspace, place, save, reopen" smoke.log fresh &&
	{ needs_ok "$LOGS/smoke.log" || sed -i 's/^\(Fresh config[^|]*\)|PASS|/\1|FAIL|/' "$WORK/rows"; }

# Namaka needs its wheelhouse: run_all.sh fetched it into tests/.wheelhouse.
NAMAKA_WHEELHOUSE=${NAMAKA_WHEELHOUSE:-$TESTS/.wheelhouse}
export NAMAKA_WHEELHOUSE
namaka() {
	unit "$HAUMEA_GAME_DIR" test_namaka_install test_ui_scripts.UiScriptTests.test_namaka
}
step "Namaka installed offline" namaka.log namaka &&
	{ needs_ok "$LOGS/namaka.log" || sed -i 's/^\(Namaka[^|]*\)|PASS|/\1|FAIL|/' "$WORK/rows"; }

if [ -f "$TESTS/test_updater.py" ]; then
	step "Updater vs fake server (production, beta)" updater.log unit "$HAUMEA_GAME_DIR" test_updater
else
	row "Updater vs fake server (production, beta)" SKIP 0 "not on this commit"
fi

# The table.
echo
printf '%-62s %-5s %6s  %s\n' "Check" "Result" "Time" "Log"
printf '%-62s %-5s %6s  %s\n' "-----" "------" "----" "---"
failed=0
while IFS='|' read -r name status secs note; do
	printf '%-62s %-6s %5ss  %s\n' "$name" "$status" "$secs" "$note"
	[ "$status" = FAIL ] && failed=1
done < "$WORK/rows"
rm -rf "$WORK"
echo
[ "$failed" = 0 ] && echo "RC check passed." || echo "RC check FAILED."
exit $failed
