#!/bin/sh
set -eu

# Starts Haumea with the agent socket. Haumea finds the game folder itself
# (its saved setting, or the folder it is started from or installed in);
# HAUMEA_GAME_DIR and HAUMEA_WORKSPACE override them for this run.
BUNDLE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
HAUMEA_EXECUTABLE=${HAUMEA_EXECUTABLE:-"$BUNDLE_DIR/haumea"}
HAUMEA_ENGINE_SOCKET=${HAUMEA_ENGINE_SOCKET:-"$HOME/.cache/haumea-agent.sock"}
mkdir -p "$(dirname "$HAUMEA_ENGINE_SOCKET")"
export HAUMEA_EXECUTABLE HAUMEA_ENGINE_SOCKET

# Namaka's folder, or the one it had as Agent Alpha.
for companion in namaka haumea-agent-alpha; do
	if [ -x "$BUNDLE_DIR/$companion/run-haumea-agent.sh" ]; then
		exec "$BUNDLE_DIR/$companion/run-haumea-agent.sh" "$@"
	fi
done

set -- --agent-socket "$HAUMEA_ENGINE_SOCKET" "$@"
if [ -n "${HAUMEA_WORKSPACE:-}" ]; then
	set -- --workspace "$HAUMEA_WORKSPACE" "$@"
fi
if [ -n "${HAUMEA_GAME_DIR:-}" ]; then
	set -- --game "$HAUMEA_GAME_DIR" "$@"
fi
exec "$HAUMEA_EXECUTABLE" "$@"
