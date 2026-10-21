#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

# Haumea finds the game folder itself (its saved setting, or the folder it is
# started from or installed in). HAUMEA_GAME_DIR and HAUMEA_WORKSPACE override
# them for this run.
BUNDLE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
HAUMEA_EXECUTABLE=${HAUMEA_EXECUTABLE:-"$BUNDLE_DIR/haumea"}
HAUMEA_ENGINE_SOCKET=${HAUMEA_ENGINE_SOCKET:-"$HOME/.cache/haumea-agent.sock"}
mkdir -p "$(dirname "$HAUMEA_ENGINE_SOCKET")"
export HAUMEA_ENGINE_SOCKET
set -- --agent-socket "$HAUMEA_ENGINE_SOCKET" "$@"
if [ -n "${HAUMEA_WORKSPACE:-}" ]; then
	set -- --workspace "$HAUMEA_WORKSPACE" "$@"
fi
if [ -n "${HAUMEA_GAME_DIR:-}" ]; then
	set -- --game "$HAUMEA_GAME_DIR" "$@"
fi
exec "$HAUMEA_EXECUTABLE" "$@"
