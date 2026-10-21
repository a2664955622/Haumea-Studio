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

if [ -d wheelhouse ]; then
	if .venv-agent/bin/python -m pip install --no-index --find-links wheelhouse "${wheel}[mcp]"; then
		:
	else
		printf '%s\n' 'No compatible offline wheels found; trying the configured Python package index.' >&2
		.venv-agent/bin/python -m pip install "${wheel}[mcp]"
	fi
else
	.venv-agent/bin/python -m pip install "${wheel}[mcp]"
fi

printf '\nAgent Alpha is installed. MCP command: %s/.venv-agent/bin/haumea-mcp\n' "$PWD"
