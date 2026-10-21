#!/bin/sh
# cc.sh gm.pwn include-dir...: compile with the toolchain's pawncc (output to a temp dir)
set -eu
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
DIR=${PAWN_TOOLCHAIN:-$HERE/.toolchain}
src=$(realpath "$1"); shift
# -i<dir> for each include folder, keeping folders with spaces whole.
n=$#
while [ "$n" -gt 0 ]; do
	set -- "$@" "-i$1"
	shift
	n=$((n - 1))
done
set -- "$src" -o"$(dirname "$src")/$(basename "$src" .pwn).amx" -d3 "-;+" "-(+" "$@"
cd "$(dirname "$src")"
"$DIR/bin/pawncc" "$@" 2>&1 || true
