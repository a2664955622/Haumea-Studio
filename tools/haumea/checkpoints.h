#pragma once

// Checkpoints as the SA-MP client shows them (checkpoints.cpp): the red
// cylinder, race types 0-8 with the arrow to the next one, the finish flag,
// and the moving air hoops.

struct ObjectInst;

// One checkpoint at time seconds. pickColour: drawn solid in that colour for
// the picking pass (no arrow or flag); nil draws it as in game.
void DrawCheckpointMarker(const ObjectInst *inst, float time, const rw::RGBA *pickColour);
// Every checkpoint in the map, in the transparent pass (renderer.cpp), and
// in the colour-coded pick pass in their id colours.
void RenderCheckpointMarkers(float time);
void RenderCheckpointMarkersColourCoded(void);
