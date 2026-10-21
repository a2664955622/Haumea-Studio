#pragma once

// Player view: the map's elements as a player at the camera would see them.
// Editor-only and off by default. Native objects exist for every player and
// show within their draw distance (the model's when it is 0); streamer
// objects are created within their stream distance (STREAMER_OBJECT_SD, 300,
// when 0), the nearest 1000 at most, and show within their draw distance.
// Streamer elements only appear in their virtual world and interior; native
// objects have neither. Vehicles and actors stream within 200 m like the
// server's, up to the client's 700 vehicles and 50 actors. World buildings
// are drawn as usual.

struct ObjectInst;

extern bool gPlayerView;
extern int gPlayerViewWorld;	// the virtual world the player is in

// Once per frame before BuildRenderList, with the camera's position.
void PlayerViewUpdate(void);
// For the renderer: whether a map element is left out, and whether it is in
// by the player view's own distance rules (so the editor's culling is skipped).
bool PlayerViewHides(const ObjectInst *inst);
bool PlayerViewShows(const ObjectInst *inst);

struct PlayerViewStats
{
	int objectsVisible, objectsStreamed, objectsInRange;
	int vehiclesStreamed, actorsStreamed;
	int otherWorld;	// elements left out for their virtual world or interior
	bool streamer;
};
const PlayerViewStats &GetPlayerViewStats(void);
