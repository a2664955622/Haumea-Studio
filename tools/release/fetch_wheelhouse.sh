#!/bin/sh
# Downloads Agent Alpha's dependency wheels for an offline setup-agent run.
#
#   tools/release/fetch_wheelhouse.sh linux|windows <dest>
#
# Covers CPython 3.10-3.14 on Linux x86_64 (manylinux) or Windows x64. uv
# resolves pyproject.toml's [mcp] dependencies for each target, because pip
# download evaluates environment markers against the Python running it: from
# Linux it leaves out pywin32, and from 3.12 it leaves out exceptiongroup,
# which 3.10 needs. pip then fetches exactly those pins as binary wheels.
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
trap 'rm -f "$REQS"' EXIT

for minor in 10 11 12 13 14; do
	echo "== CPython 3.$minor ($1)"
	uv pip compile -q --no-header --no-annotate --extra mcp \
		--python-version "3.$minor" --python-platform "$UV_PLATFORM" \
		"$ROOT/pyproject.toml" -o "$REQS"
	# shellcheck disable=SC2086
	python3 -m pip download -q --no-deps --only-binary=:all: --dest "$DEST" \
		--implementation cp --python-version "3.$minor" --abi "cp3$minor" \
		$PIP_PLATFORMS -r "$REQS"
done
ls "$DEST"
