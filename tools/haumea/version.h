// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Haumea's version, as open.mp numbers its own: MAJOR.MINOR.PATCH here, and a
// build number (the commit count) and commit from the build
// (tools/release/gen_buildinfo.py writes haumea_buildinfo.h). The full version
// is 1.0.0.<build>; see versionutil.h.
#define HAUMEA_VERSION_MAJOR 1
#define HAUMEA_VERSION_MINOR 0
#define HAUMEA_VERSION_PATCH 0

#define HAUMEA_VERSION_STR2(x) #x
#define HAUMEA_VERSION_STR(x) HAUMEA_VERSION_STR2(x)
// "1.0.0", without the build number.
#define HAUMEA_VERSION HAUMEA_VERSION_STR(HAUMEA_VERSION_MAJOR) "." \
	HAUMEA_VERSION_STR(HAUMEA_VERSION_MINOR) "." HAUMEA_VERSION_STR(HAUMEA_VERSION_PATCH)

// The release channel this build belongs to, set by the build: production
// or beta for a release (from its tag, v1.0.0.<build>-<channel>), dev for
// every other build. The updater treats dev as production.
#ifndef HAUMEA_CHANNEL
#define HAUMEA_CHANNEL "dev"
#endif

// The channel as people see it, next to the version; production says nothing.
#define HAUMEA_CHANNEL_DISPLAY (strcmp(HAUMEA_CHANNEL, "production") == 0 ? "" : \
	strcmp(HAUMEA_CHANNEL, "beta") == 0 ? "Beta" : strcmp(HAUMEA_CHANNEL, "dev") == 0 ? "Dev" : HAUMEA_CHANNEL)
