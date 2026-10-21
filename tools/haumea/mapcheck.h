// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <vector>

// Map check: what will break or look wrong in game. Limits come from
// open.mp's SDK (values.hpp, Objects component) and SA-MP 0.3.7's a_samp.inc;
// the server's config.json in the workspace decides open.mp's object limit
// and how many player slots NPCs can take.

enum MapCheckSeverity
{
	MAPCHECK_ERROR,	// breaks in game, or is lost
	MAPCHECK_WARNING,	// probably wrong, or needs something on the server
	MAPCHECK_INFO,
};

struct MapCheckItem
{
	int severity;
	const char *category;
	std::string text;
	std::vector<int> instances;	// ObjectInst::m_id, to select or jump to
	int textDraw = -1;	// index in GetTextDraws(), to open in the TextDraws window
};

struct MapCheckResult
{
	std::vector<MapCheckItem> items;
	int numErrors, numWarnings;
	int pawnFormat;
};

// pawnFormat: PAWN_CODE_NATIVE or PAWN_CODE_STREAMER.
void RunMapCheck(int pawnFormat, MapCheckResult *result);
// One line for a toast, e.g. "2 problems and 3 warnings"; "" when clean.
std::string MapCheckSummary(const MapCheckResult &result);
const char *MapCheckSeverityName(int severity);
