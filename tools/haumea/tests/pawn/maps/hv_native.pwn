// Haumea verification map: every element, native natives (open.mp for NPCs).
#include <open.mp>

stock hv_native_RemoveWorldObjectsForPlayer(playerid)
{
	RemoveBuildingForPlayer(playerid, 3563, 2063.132813, -1731.781250, 15.273438, 0.25);
}

stock hv_native_CreateObjects()
{
	new wall = CreateObject(19353, 1600.000000, -1890.000000, 14.000000, 0.000000, 0.000000, 90.000000, 300.00); // wall
	SetObjectMaterial(wall, 0, 19297, "matcolours", "white", 0xFFFF0000);
	new sign = CreateObject(19353, 1604.000000, -1890.000000, 14.000000, 0.000000, 0.000000, 90.000000); // sign @group("Shop") @note("front sign")
	SetObjectMaterialText(sign, "OPEN", 0, 90, "Arial", 24, 1, 0xFFFFFFFF, 0xFF000000, 1);
	new base = CreateObject(1271, 1616.000000, -1890.000000, 13.500000, 0.000000, 0.000000, 45.000000);
	new crate = CreateObject(1271, 1616.000000, -1890.000000, 14.300000, 0.000000, 0.000000, 0.000000);
	AttachObjectToObject(crate, base, 0.000000, 0.000000, 0.800000, 15.000000, 25.000000, 35.000000, 1);

	AddStaticVehicle(411, 1610.000000, -1880.000000, 13.300000, 90.000000, 1, 1); // infernus
	AddStaticVehicleEx(560, 1615.000000, -1880.000000, 13.300000, 180.000000, 3, 3, 120); // sultan
	new car = CreateVehicle(522, 1620.000000, -1880.000000, 13.300000, 0.000000, 6, 6, 60); // nrg500
	LinkVehicleToInterior(car, 1);

	CreatePickup(1240, 2, 1600.000000, -1900.000000, 13.500000, 0);
	Create3DTextLabel("Welcome", 0xFFFF00FF, 1602.000000, -1900.000000, 14.000000, 25.00, 0, 1);

	new clerk = CreateActor(179, 1605.000000, -1900.000000, 13.500000, 180.000000);
	SetActorInvulnerable(clerk, false);
	SetActorHealth(clerk, 50.00);
	ApplyActorAnimation(clerk, "DEALER", "DEALER_IDLE", 4.10, true, false, false, false, 0);

	AddPlayerClass(0, 1610.000000, -1900.000000, 13.500000, 90.000000, 0, 0, 0, 0, 0, 0);
	AddPlayerClassEx(2, 105, 1612.000000, -1900.000000, 13.500000, 270.000000, 24, 100, 31, 300, 0, 0);

	gate_zone = GangZoneCreate(1590.000000, -1910.000000, 1630.000000, -1870.000000);

	new path_a = NPC_CreatePath();
	NPC_AddPointToPath(path_a, 1600.000000, -1920.000000, 13.500000, 0.50);
	NPC_AddPointToPath(path_a, 1620.000000, -1920.000000, 13.500000, 0.50);
	NPC_AddPointToPath(path_a, 1620.000000, -1930.000000, 13.500000, 0.50);

	new walker = NPC_Create("Walker");
	NPC_Spawn(walker);
	NPC_SetSkin(walker, 101);
	NPC_SetPos(walker, 1600.000000, -1920.000000, 13.500000);
	NPC_SetFacingAngle(walker, 90.000000);
	NPC_SetHealth(walker, 80.00);
	NPC_SetArmour(walker, 50.00);
	NPC_SetWeapon(walker, WEAPON:24);
	NPC_SetAmmo(walker, 70);
	NPC_SetInvulnerable(walker, true);

	new dancer = NPC_Create("Dancer");
	NPC_Spawn(dancer);
	NPC_SetSkin(dancer, 102);
	NPC_SetPos(dancer, 1606.000000, -1915.000000, 13.500000);
	NPC_SetFacingAngle(dancer, 0.000000);
	NPC_SetVirtualWorld(dancer, 0);
	NPC_ApplyAnimation(dancer, "DANCING", "dnce_M_a", 4.10, true, false, false, false, 0);

	new nodewalker = NPC_Create("NodeWalker");
	NPC_Spawn(nodewalker);
	NPC_SetSkin(nodewalker, 7);
	NPC_SetPos(nodewalker, 2490.000000, -1670.000000, 13.300000);
	NPC_SetFacingAngle(nodewalker, 0.000000);

	new driver = NPC_Create("Driver");
	NPC_Spawn(driver);
	NPC_SetSkin(driver, 255);
	NPC_SetPos(driver, 1700.000000, -1950.000000, 14.000000);
	NPC_SetFacingAngle(driver, 0.000000);

	NPC_MoveByPath(walker, path_a, NPC_MOVE_TYPE_WALK, NPC_MOVE_SPEED_AUTO, false);
	NPC_OpenNode(15);
	NPC_SetNodePoint(15, 0);
	NPC_PlayNode(nodewalker, 15, NPC_MOVE_TYPE_WALK, NPC_MOVE_SPEED_AUTO, 0.00, true);
	new streak = AddStaticVehicleEx(538, 2864.750000, 1254.159058, 12.126000, 180.000000, -1, -1, 100);
	NPC_PutInVehicle(driver, streak, 0);
	NPC_StartPlayback(driver, "TRAIN_LINDEN_TO_UNITY", true, 0.000000, 0.000000, 0.000000, 0.0, 0.0, 0.0);
}

static gate_zone = -1;

stock hv_native_ShowForPlayer(playerid)
{
	GangZoneShowForPlayer(playerid, gate_zone, 0xFF000088);
	SetPlayerMapIcon(playerid, 0, 1600.000000, -1880.000000, 13.500000, 6, 0x00000000, 0);
	PlayAudioStreamForPlayer(playerid, "http://example.com/plaza.mp3", 1604.000000, -1900.000000, 13.500000, 40.00, true);
}

new PlayerText:hv_native_ptd0[MAX_PLAYERS];

stock hv_native_ShowTextDraws(playerid)
{
	hv_native_ptd0[playerid] = CreatePlayerTextDraw(playerid, 320.000, 400.000, "Per-player ~y~text");
	PlayerTextDrawAlignment(playerid, hv_native_ptd0[playerid], TEXT_DRAW_ALIGN_CENTER);
	PlayerTextDrawShow(playerid, hv_native_ptd0[playerid]);
}
