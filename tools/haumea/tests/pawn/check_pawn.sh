#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# check_pawn.sh [maps-dir]: compile every generated map as a gamemode with
# 0 errors and 0 warnings: against open.mp's includes, and against SA-MP
# 0.3.7's for maps without open.mp-only calls (no <open.mp> include).
# The default maps are the expected exporter output in expected/.
# Compiles run several at once (TEST_JOBS, default CPUs/8, at least 1); results
# print in map order.
set -eu
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
DIR=${PAWN_TOOLCHAIN:-$HERE/.toolchain}

# check_pawn.sh --one work map target: one compile, its result in work/<name>_<target>.result
if [ "${1:-}" = --one ]; then
	WORK=$2; map=$3; target=$4
	name=$(basename "$map" .pwn)
	gm=$WORK/${name}_$target.pwn
	"$HERE/mkgm.sh" "$map" "$gm" "$target"
	if [ "$target" = openmp ]; then
		out=$("$HERE/cc.sh" "$gm" "$DIR/omp/Server/qawno/include" "$DIR/streamer" || true)
	else
		out=$("$HERE/cc.sh" "$gm" "$DIR/samp037" "$DIR/streamer" || true)
	fi
	# Count diagnostics, not the "N Warnings." summary line.
	errors=$(printf '%s\n' "$out" | grep -cE ': (fatal )?error [0-9]+' || true)
	warnings=$(printf '%s\n' "$out" | grep -cE ': warning [0-9]+' || true)
	if [ "$errors" -eq 0 ] && [ "$warnings" -eq 0 ] && [ -f "$WORK/${name}_$target.amx" ]; then
		echo "ok   $name ($target)" > "$WORK/${name}_$target.result"
	else
		{
			echo "FAIL $name ($target): $errors error(s), $warnings warning(s)"
			printf '%s\n' "$out" | grep -E 'error|warning' | head -20
		} > "$WORK/${name}_$target.result"
	fi
	exit 0
fi

MAPS=${1:-$HERE/expected}
"$HERE/fetch_toolchain.sh" "$DIR"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
JOBS=${TEST_JOBS:-$(( $(nproc 2>/dev/null || echo 8) / 8 ))}
[ "$JOBS" -ge 1 ] || JOBS=1
for map in "$MAPS"/*.pwn; do
	printf '%s\n%s\n' "$map" openmp
	grep -q '#include <open.mp>' "$map" || printf '%s\n%s\n' "$map" samp037
done > "$WORK/jobs"
tr '\n' '\0' < "$WORK/jobs" | xargs -0 -n 2 -P "$JOBS" sh "$0" --one "$WORK"
failed=0
while read -r map && read -r target; do
	result=$WORK/$(basename "$map" .pwn)_$target.result
	if [ -f "$result" ]; then
		cat "$result"
		head -n 1 "$result" | grep -q '^ok ' || failed=1
	else
		echo "FAIL $(basename "$map" .pwn) ($target): no result"
		failed=1
	fi
done < "$WORK/jobs"
exit $failed
