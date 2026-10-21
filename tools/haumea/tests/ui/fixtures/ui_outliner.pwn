// Named objects in two groups, a vehicle, and an open.mp NPC on a path in it, out at sea.
#include <open.mp>

stock UiOutliner()
{
	new bench_a = CreateObject(1280, 2000.000000, -3000.000000, 5.000000, 0.000000, 0.000000, 0.000000); // parkbench1 @group("Park")
	new bench_b = CreateObject(1280, 2004.000000, -3000.000000, 5.000000, 0.000000, 0.000000, 0.000000); // parkbench1 @group("Park")
	new grill = CreateObject(1481, 2008.000000, -3000.000000, 5.000000, 0.000000, 0.000000, 0.000000); // DYN_BAR_B_Q
	new bin = CreateObject(1337, 2012.000000, -3000.000000, 5.000000, 0.000000, 0.000000, 0.000000); // BinNt07_LA
	new car = CreateVehicle(411, 2016.000000, -3000.000000, 5.500000, 0.000000, 1, 1, 60); // infernus
	new path_a = NPC_CreatePath();
	NPC_AddPointToPath(path_a, 2020.000000, -3000.000000, 5.000000, 0.50);
	NPC_AddPointToPath(path_a, 2030.000000, -3000.000000, 5.000000, 0.50);
	new walker = NPC_Create("Walker");
	NPC_Spawn(walker);
	NPC_SetSkin(walker, 101);
	NPC_SetPos(walker, 2024.000000, -2992.000000, 5.000000);
	NPC_MoveByPath(walker, path_a, NPC_MOVE_TYPE_WALK, NPC_MOVE_SPEED_AUTO, false);
	NPC_PutInVehicle(walker, car, 1);
}
