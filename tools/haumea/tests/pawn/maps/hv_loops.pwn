// Haumea verification map: NPCs that repeat their path (open.mp).
#include <open.mp>

stock Haumea_hv_loops_CreateObjects()
{
	new yard = NPC_CreatePath();
	NPC_AddPointToPath(yard, 1600.0, -1700.0, 13.5, 0.2);
	NPC_AddPointToPath(yard, 1620.0, -1700.0, 13.5, 0.2);
	NPC_AddPointToPath(yard, 1620.0, -1720.0, 13.5, 0.2);
	new lane = NPC_CreatePath();
	NPC_AddPointToPath(lane, 1640.0, -1700.0, 13.5, 0.5);
	NPC_AddPointToPath(lane, 1680.0, -1700.0, 13.5, 0.5);

	new guard = NPC_Create("YardGuard");
	NPC_Spawn(guard);
	NPC_SetSkin(guard, 280);
	NPC_SetPos(guard, 1600.0, -1702.0, 13.5);
	new runner = NPC_Create("LaneRunner");
	NPC_Spawn(runner);
	NPC_SetSkin(runner, 280);
	NPC_SetPos(runner, 1640.0, -1702.0, 13.5);
	new walker = NPC_Create("OnceWalker");
	NPC_Spawn(walker);
	NPC_SetSkin(walker, 280);
	NPC_SetPos(walker, 1640.0, -1710.0, 13.5);

	Haumea_hv_loops_MoveByPathRepeat(guard, yard, NPC_MOVE_TYPE_WALK, NPC_MOVE_SPEED_AUTO, false, NPC_PATH_LOOP);
	Haumea_hv_loops_MoveByPathRepeat(runner, lane, NPC_MOVE_TYPE_JOG, 0.5, false, NPC_PATH_BACK_AND_FORTH);
	NPC_MoveByPath(walker, lane, NPC_MOVE_TYPE_WALK, NPC_MOVE_SPEED_AUTO, true);
}
