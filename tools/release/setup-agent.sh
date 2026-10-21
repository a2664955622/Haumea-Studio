#!/bin/sh
set -eu

cd "$(dirname "$0")"
wheel=$(find . -maxdepth 1 -type f -name 'haumea_agent-*.whl' -print -quit)
if [ -z "$wheel" ]; then
	printf '%s\n' 'Haumea Agent Alpha wheel is missing from this bundle.' >&2
	exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
	printf '%s\n' 'Python 3.10 or newer is needed: install python3 (and python3-venv on Debian or Ubuntu).' >&2
	exit 1
fi
if ! python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))'; then
	printf 'Python 3.10 or newer is needed; this is %s.\n' "$(python3 --version 2>&1)" >&2
	exit 1
fi
# A failed earlier run can leave a venv without pip: make it again.
if [ -d .venv-agent ] && ! .venv-agent/bin/python -m pip --version >/dev/null 2>&1; then
	rm -rf .venv-agent
fi
if [ ! -d .venv-agent ]; then
	if ! python3 -m venv .venv-agent; then
		rm -rf .venv-agent
		printf '%s\n' 'Could not create the Python environment. On Debian or Ubuntu, install python3-venv (sudo apt install python3-venv) and run this again.' >&2
		exit 1
	fi
fi

pyver=$(.venv-agent/bin/python -c 'import sys; print("%d.%d" % sys.version_info[:2])')
# The wheelhouse holds every wheel for Python 3.14 and the pure-Python ones
# for 3.10-3.13; another version downloads only its binary wheels, pinned to
# the same versions (wheelhouse/constraints-<version>.txt).
if [ -d wheelhouse ] && [ "$pyver" = 3.14 ] &&
	.venv-agent/bin/python -m pip install --no-input --no-index --find-links wheelhouse "${wheel}[mcp]"; then
	:
elif [ -f "wheelhouse/constraints-$pyver.txt" ]; then
	# Download only the pins no bundled wheel fits (the binary ones), then
	# install from the two folders with no index.
	missing=$(.venv-agent/bin/python missing_wheels.py wheelhouse "wheelhouse/constraints-$pyver.txt")
	printf 'Python %s: downloading the %s parts Agent Alpha bundles only for Python 3.14...\n' \
		"$pyver" "$(printf '%s\n' "$missing" | grep -c .)" >&2
	rm -rf .wheels-download
	# shellcheck disable=SC2086
	if ! { [ -z "$missing" ] || .venv-agent/bin/python -m pip download --no-input --no-deps --only-binary=:all: \
			-d .wheels-download $missing; } ||
	   ! .venv-agent/bin/python -m pip install --no-input --no-index --find-links wheelhouse \
			--find-links .wheels-download -c "wheelhouse/constraints-$pyver.txt" "${wheel}[mcp]"; then
		rm -rf .wheels-download
		printf '%s\n' 'Agent Alpha needs Python 3.14 to install offline, or an internet connection.' >&2
		exit 1
	fi
	rm -rf .wheels-download
else
	# A Python the bundle has no pins for: everything from the package index.
	.venv-agent/bin/python -m pip install --no-input "${wheel}[mcp]"
fi

printf '\nAgent Alpha is installed. MCP command: %s/.venv-agent/bin/haumea-mcp\n' "$PWD"
