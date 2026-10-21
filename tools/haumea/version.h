#pragma once

// Haumea numeric version — bump this when releasing updates.
// Keep this monotonic for the updater: after 1.29, use 1.30, not 1.3.
#define HAUMEA_VERSION "1.41.0"

// Human-facing release label.
#define HAUMEA_RELEASE_NAME "Beta 1"

// Update channel — set at build time via preprocessor define
// Defaults to "master" if not set by the build system
#ifndef HAUMEA_CHANNEL
#define HAUMEA_CHANNEL "master"
#endif

// Display name for the channel (shown in title bar / update popup)
#define HAUMEA_CHANNEL_DISPLAY (strcmp(HAUMEA_CHANNEL, "PE") == 0 ? "FLA" : \
	(strcmp(HAUMEA_CHANNEL, "agent-alpha") == 0 ? "Agent Alpha" : HAUMEA_CHANNEL))
