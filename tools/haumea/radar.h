// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdio.h>

// The game's radar over the 3D view, drawn as CRadar does it (gta-reversed's
// Radar.cpp): the camera is the player, the radar turns with it, the map's
// tiles (radarNN.txd) or the sea off the map, and the map's gang zones on
// top, clipped to the circle. Clicking it opens the full map, a window that
// pans and zooms over the same tiles and zones.

// Once per frame, while the editor UI is built: the radar and the map window.
void RadarFrame(void);

// The radar alone, for TextDraw mode's game HUD: centre and radii in pixels
// (an ellipse on a stretched screen), k pixels per unit of the 448-high screen.
struct ImDrawList;
struct ImVec2;
void DrawRadarOnCanvas(ImDrawList *dl, ImVec2 centre, ImVec2 radius, float k);

bool RadarEnabled(void);	// Show Radar
void RadarSetEnabled(bool on);
bool RadarMapOpen(void);

bool RadarLoadSetting(const char *key, const char *value);	// false: not a radar setting
void RadarSaveSettings(FILE *f);
