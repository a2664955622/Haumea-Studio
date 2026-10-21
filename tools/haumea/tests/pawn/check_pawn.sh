#!/bin/sh
# check_pawn.sh [maps-dir]: compile every generated map as a gamemode with
# 0 errors and 0 warnings: against open.mp's includes, and against SA-MP
# 0.3.7's for maps without open.mp-only calls (no <open.mp> include).
# The default maps are the expected exporter output in expected/.
set -eu
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
MAPS=${1:-$HERE/expected}
DIR=${PAWN_TOOLCHAIN:-$HERE/.toolchain}
"$HERE/fetch_toolchain.sh" "$DIR"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
failed=0
for map in "$MAPS"/*.pwn; do
	name=$(basename "$map" .pwn)
	targets=openmp
	grep -q '#include <open.mp>' "$map" || targets="openmp samp037"
	for target in $targets; do
		gm=$WORK/${name}_$target.pwn
		"$HERE/mkgm.sh" "$map" "$gm" "$target"
		if [ "$target" = openmp ]; then
			out=$("$HERE/cc.sh" "$gm" "$DIR/omp/Server/qawno/include" "$DIR/streamer")
		else
			out=$("$HERE/cc.sh" "$gm" "$DIR/samp037" "$DIR/streamer")
		fi
		# Count diagnostics, not the "N Warnings." summary line.
		errors=$(printf '%s\n' "$out" | grep -cE ': (fatal )?error [0-9]+' || true)
		warnings=$(printf '%s\n' "$out" | grep -cE ': warning [0-9]+' || true)
		if [ "$errors" -eq 0 ] && [ "$warnings" -eq 0 ] && [ -f "$WORK/${name}_$target.amx" ]; then
			echo "ok   $name ($target)"
		else
			echo "FAIL $name ($target): $errors error(s), $warnings warning(s)"
			printf '%s\n' "$out" | grep -E 'error|warning' | head -20
			failed=1
		fi
	done
done
exit $failed
