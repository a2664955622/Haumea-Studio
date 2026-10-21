#!/bin/sh
# Downloads Namaka's dependency wheels for setup-agent's offline install.
#
#   tools/release/fetch_wheelhouse.sh linux|windows <dest>
#
# Binary wheels (pydantic-core, Pillow, pywin32...) for CPython 3.14 only,
# the one version that installs fully offline; pure-Python wheels for every
# version 3.10-3.14, so another version downloads only its binary wheels.
# uv resolves pyproject.toml's [mcp] dependencies for each target, because
# pip download evaluates environment markers against the Python running it:
# from Linux it leaves out pywin32, and from 3.12 it leaves out
# exceptiongroup, which 3.10 needs. pip then fetches exactly those pins.
# Needs uv and python3 with pip.
set -eu

case "${1:-}" in
linux)
	UV_PLATFORM=x86_64-manylinux_2_28
	PIP_PLATFORMS="--platform manylinux_2_28_x86_64 --platform manylinux_2_17_x86_64 --platform manylinux2014_x86_64"
	;;
windows)
	UV_PLATFORM=x86_64-pc-windows-msvc
	PIP_PLATFORMS="--platform win_amd64"
	;;
*) echo "usage: $0 linux|windows <dest>" >&2; exit 2 ;;
esac
DEST=${2:?usage: $0 linux|windows <dest>}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$DEST"
REQS=$(mktemp)

OFFLINE_MINOR=14	# the CPython 3.x whose binary wheels are bundled
PURE=$(mktemp -d)
trap 'rm -f "$REQS"; rm -rf "$PURE"' EXIT
for minor in 10 11 12 13 14; do
	echo "== CPython 3.$minor ($1)$( [ "$minor" = "$OFFLINE_MINOR" ] && echo ', with binary wheels')"
	uv pip compile -q --no-header --no-annotate --extra mcp \
		--python-version "3.$minor" --python-platform "$UV_PLATFORM" \
		"$ROOT/pyproject.toml" -o "$REQS"
	# This version's exact pins: setup-agent installs with them, so another
	# version takes the bundled wheels and downloads only the binary ones.
	cp "$REQS" "$DEST/constraints-3.$minor.txt"
	target=$DEST
	[ "$minor" = "$OFFLINE_MINOR" ] || target=$PURE
	# shellcheck disable=SC2086
	python3 -m pip download -q --no-deps --only-binary=:all: --dest "$target" \
		--implementation cp --python-version "3.$minor" --abi "cp3$minor" \
		$PIP_PLATFORMS -r "$REQS"
done
# From the other versions, only what any Python can use.
find "$PURE" -name '*-none-any.whl' -exec cp -n {} "$DEST"/ \;
ls "$DEST"
