#pragma once

// Movement preview for open.mp NPCs: walks an NPC in the viewport the way the
// NPC component moves it, from its ObjectPawnProperties. NPC_MoveByPath
// follows its path's points (pathName, npcPathReversed, each point's
// pathStopRange); NPC_PlayNode walks the game's path nodes from
// npcNode/npcNodePoint, picking links like open.mp. Both use npcMoveType and
// npcMoveSpeed, with the PED walk, jog or sprint animation, facing the way it
// goes. A preview never marks the map dirty or goes through undo, and puts
// the NPC back where it was when it stops. It replays after a pause when the
// route ends.

struct ObjectInst;

bool NpcPreviewStart(ObjectInst *npc);	// false when there is nothing to walk
void NpcPreviewStop(ObjectInst *npc);
bool NpcPreviewIsActive(ObjectInst *npc);
void NpcPreviewStopAll(void);	// before saving, so the map keeps the NPCs' own places
// Once per frame, before animation (AnimUpdate) and the render list.
void NpcPreviewUpdate(float timeStep);
