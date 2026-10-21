#pragma once

// Haumea's version, as open.mp numbers its own: MAJOR.MINOR.PATCH here, and a
// build number (the commit count) and commit from the build
// (tools/release/gen_buildinfo.py writes haumea_buildinfo.h). The full version
// is 1.41.0.<build>; see versionutil.h. Every release bumps PATCH at least:
// installs before 1.41.0.<build> compare only the first three parts.
#define HAUMEA_VERSION_MAJOR 1
#define HAUMEA_VERSION_MINOR 41
#define HAUMEA_VERSION_PATCH 0

#define HAUMEA_VERSION_STR2(x) #x
#define HAUMEA_VERSION_STR(x) HAUMEA_VERSION_STR2(x)
// "1.41.0", without the build number.
#define HAUMEA_VERSION HAUMEA_VERSION_STR(HAUMEA_VERSION_MAJOR) "." \
	HAUMEA_VERSION_STR(HAUMEA_VERSION_MINOR) "." HAUMEA_VERSION_STR(HAUMEA_VERSION_PATCH)

// Update channel — set at build time via preprocessor define
// Defaults to "master" if not set by the build system
#ifndef HAUMEA_CHANNEL
#define HAUMEA_CHANNEL "master"
#endif

// Display name for the channel (shown in title bar / update popup)
#define HAUMEA_CHANNEL_DISPLAY (strcmp(HAUMEA_CHANNEL, "PE") == 0 ? "FLA" : \
	(strcmp(HAUMEA_CHANNEL, "agent-alpha") == 0 ? "Namaka" : HAUMEA_CHANNEL))
