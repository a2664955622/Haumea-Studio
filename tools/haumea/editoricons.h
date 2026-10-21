#pragma once

// Editor icons for the SA-MP elements players never see as a model: 3D text
// labels, sounds, map icons, gang zones and NPC path points, plus checkpoints
// (whose game-style marker checkpoints.cpp draws as well). They are drawn
// as camera-facing billboards of a constant screen size, with art made here
// in code (nothing from GTA's files), in the world pass and in the
// colour-coded pick pass, so picking, captures and thumbnails all get them.
// The element keeps its stand-in ObjectDef for bounds and undo, but that
// model is never drawn, outlined or used as a snap target.

struct ObjectInst;

// The kinds drawn as icons.
bool ElementKindUsesIcon(int kind);
bool InstUsesEditorIcon(const ObjectInst *inst);

// The render list: RenderInst queues an icon instead of drawing the model.
void ClearElementIcons(void);
void QueueElementIcon(ObjectInst *inst);
// After the transparent pass: faded where hidden behind geometry, then on top.
void RenderElementIcons(void);
// In the colour-coded pass, so clicks and the right-click menu find them.
void RenderElementIconsColourCoded(void);
// How many were drawn with models of these kinds this frame (always 0; the
// tests check it).
int GetElementProxyModelDraws(void);
void NoteElementProxyModelDraw(void);

// The placement offset above the surface: 0 for icon kinds (the icon's
// centre is the element's position), else the model's own.
float GetElementPlacementBaseOffset(int kind, int objectId);

// The placement and prefab ghosts of an icon kind.
void RenderElementIconGhost(int kind, rw::V3d position);
// One icon facing viewDir at a world size, for renders with their own camera
// (prefab thumbnails).
void RenderElementIconBillboard(int kind, rw::V3d position, rw::V3d viewDir, float worldSize);
// An icon kind's art as an ImGui texture (SA-MP tab tiles), or nil.
void *GetElementIconTexture(int kind);
