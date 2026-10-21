#pragma once

// Haumea's version numbers, parsed and compared in one place (About, the
// title bar, --version, telemetry, the updater, the release tag check).
// A version is MAJOR.MINOR.PATCH.BUILD; BUILD is the commit count, from
// haumea_buildinfo.h. Release tags are v1.41.0.<build>-<channel>, and older
// ones v1.41.0-<channel>-<sha>. No editor headers here, so the tests can
// build it alone.

struct HaumeaVersion {
	int part[4];	// major, minor, patch, build; a missing part is 0
};

// "1.41.0", "1.41.0.57", "v1.41.0.57-master", "v1.41.0-master-abc1234": a
// leading v is skipped and the version ends at the first '-'. False for
// anything else (no digits, a non-number part, more than four parts).
bool ParseHaumeaVersion(const char *text, HaumeaVersion *out);
// <0, 0 or >0, comparing all four parts as numbers.
int CompareHaumeaVersion(const HaumeaVersion &a, const HaumeaVersion &b);
// A tag's channel: everything after the first '-', less a trailing
// -<7 to 40 hex digits> (an old tag's commit). v1.41.0-master-abc1234 is
// "master", v1.41.0.57-agent-alpha "agent-alpha". False without a '-'.
bool HaumeaTagChannel(const char *tag, char *out, int size);

// This build: "1.41.0.57", "1.41.0.57 (abc1234)", and the parts.
const char *HaumeaFullVersion(void);
const char *HaumeaVersionDisplay(void);
const HaumeaVersion &HaumeaCurrentVersion(void);
int HaumeaBuildNumber(void);
const char *HaumeaCommit(void);	// "abc1234", "abc1234-dirty" or "unknown"
