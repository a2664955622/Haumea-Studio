// An open.mp NPC with a path, out at sea, for the Preview movement button.
#include <open.mp>

stock UiNpc()
{
	new path_a = NPC_CreatePath();
	NPC_AddPointToPath(path_a, 2000.000000, -3000.000000, 5.000000, 0.50);
	NPC_AddPointToPath(path_a, 2010.000000, -3000.000000, 5.000000, 0.50);
	new walker = NPC_Create("Walker");
	NPC_Spawn(walker);
	NPC_SetSkin(walker, 101);
	NPC_SetPos(walker, 2004.000000, -2992.000000, 5.000000);
	NPC_MoveByPath(walker, path_a, NPC_MOVE_TYPE_WALK, NPC_MOVE_SPEED_AUTO, false);
}
