#pragma once

#include <string>
#include <vector>

// Radar art: gang zones drawn from the objects' top-down collision
// footprints. The footprints are rasterised into a grid, cells of one colour
// are merged into rectangles, and each rectangle becomes one gang zone.

enum {
	RADAR_SOURCE_ALL,	// every scripted object in the map
	RADAR_SOURCE_SELECTION,
	RADAR_SOURCE_GROUP,
};

enum {
	RADAR_COLOUR_ONE,
	RADAR_COLOUR_GROUP,	// one colour per object group
	RADAR_COLOUR_TEXTURE,	// the model's top textures, quantised
};

struct RadarArtParams {
	int source;
	char group[48];	// RADAR_SOURCE_GROUP
	float cellSize;	// metres
	int colourMode;
	uint32 colour;	// 0xRRGGBB00: RADAR_COLOUR_ONE, ungrouped objects
	int numColours;	// RADAR_COLOUR_TEXTURE: quantised to this many
	int alpha;	// 0-255, every zone
	bool outline;	// the footprint's edge cells in outlineColour
	uint32 outlineColour;
	bool heightShading;	// higher is lighter, in heightLevels steps
	int heightLevels;
	int zoneMode;	// SAMP_ZONE_*
	char outGroup[48];	// the zones' group
	bool replace;	// delete the zones already in outGroup
};

struct RadarArtZone {
	float minX, minY, maxX, maxY;
	float z;	// the highest surface under it
	uint32 colour;	// 0xRRGGBBAA
};

struct RadarArtResult {
	std::vector<RadarArtZone> zones;
	int objects;	// rasterised
	int skipped;	// no model or footprint
	int cells;	// filled
	int cols, rows;
	float cellSize;
};

void RadarArtDefaults(RadarArtParams *params);
bool RadarArtBuild(const RadarArtParams &params, RadarArtResult *result, std::string *error);
// The smallest cell size (to 0.1 m) whose art needs at most maxZones zones.
bool RadarArtFitCellSize(const RadarArtParams &params, int maxZones, float *cellSize, std::string *error);
// Builds and makes the zones in one undo step; how many, or -1 with *error.
int RadarArtApply(const RadarArtParams &params, std::string *error);
// The Tools window's section, and the preview it draws.
void RadarArtPanel(void);

// Streamed zones (SAMP_ZONE_STREAMED) load around the player in cells of
// this size: the cell the player is in and the eight around it. A zone
// belongs to the cell of its centre.
const float STREAMED_ZONE_CELL = 250.0f;
struct StreamedZoneGrid {
	float x0, y0;	// the corner of cell 0
	int cols, rows;
	int capacity;	// the most zones in any 3x3 block: what a player can have loaded
};
// The grid around these zones' centres (minX, minY, maxX, maxY each), with a
// cell of margin so players walking in load them before they arrive.
void GetStreamedZoneGrid(const std::vector<float> &bounds, StreamedZoneGrid *grid);
