// Haumea verification map: every element, streamer format.
#include <open.mp>
#include <streamer>

stock hv_streamer_RemoveWorldObjectsForPlayer(playerid)
{
	RemoveBuildingForPlayer(playerid, 3563, 2063.132813, -1731.781250, 15.273438, 0.25);
}

stock hv_streamer_CreateObjects()
{
	new STREAMER_TAG_OBJECT:tmp = CreateDynamicObject(19353, 1600.000000, -1890.000000, 14.000000, 0.000000, 0.000000, 90.000000, 0, -1, -1, 300.00, 300.00);
	SetDynamicObjectMaterial(tmp, 0, 19297, "matcolours", "white", 0xFFFF0000);
	tmp = CreateDynamicObject(19353, 1604.000000, -1890.000000, 14.000000, 0.000000, 0.000000, 90.000000, 5, 3, -1, 200.00, 150.00);
	SetDynamicObjectMaterialText(tmp, 0, "OPEN", 90, "Arial", 24, 1, 0xFFFFFFFF, 0xFF000000, 1);
	new base = CreateDynamicObject(1271, 1616.000000, -1890.000000, 13.500000, 0.000000, 0.000000, 45.000000);
	new crate = CreateDynamicObject(1271, 1616.000000, -1890.000000, 14.300000, 0.000000, 0.000000, 0.000000);
	AttachDynamicObjectToObject(crate, base, 0.000000, 0.000000, 0.800000, 15.000000, 25.000000, 35.000000, 1);

	AddStaticVehicle(411, 1610.000000, -1880.000000, 13.300000, 90.000000, 1, 1);
	AddStaticVehicleEx(560, 1615.000000, -1880.000000, 13.300000, 180.000000, 3, 3, 120);
	new car = CreateVehicle(522, 1620.000000, -1880.000000, 13.300000, 0.000000, 6, 6, 60);
	LinkVehicleToInterior(car, 1);

	CreateDynamicPickup(1240, 2, 1600.000000, -1900.000000, 13.500000, 0, -1, -1, 100.00);
	CreateDynamic3DTextLabel("Welcome", 0xFFFF00FF, 1602.000000, -1900.000000, 14.000000, 25.00, INVALID_PLAYER_ID, INVALID_VEHICLE_ID, 1, 0, -1, -1, STREAMER_3D_TEXT_LABEL_SD);

	new a = CreateDynamicActor(179, 1605.000000, -1900.000000, 13.500000, 180.000000, 0, 50.00, 0, -1, -1, STREAMER_ACTOR_SD);
	ApplyDynamicActorAnimation(a, "DEALER", "DEALER_IDLE", 4.10, 1, 0, 0, 0, 0);
	CreateDynamicActor(180, 1607.000000, -1900.000000, 13.500000, 0.000000, 1, 100.00, 0, -1, -1, STREAMER_ACTOR_SD);

	AddPlayerClass(0, 1610.000000, -1900.000000, 13.500000, 90.000000, 0, 0, 0, 0, 0, 0);
	AddPlayerClassEx(2, 105, 1612.000000, -1900.000000, 13.500000, 270.000000, 24, 100, 31, 300, 0, 0);

	CreateDynamicCP(1630.0, -1900.0, 13.5, 3.0, 0, -1, -1, STREAMER_CP_SD);
	new STREAMER_TAG_CP:cp = CreateDynamicCP(1632.0, -1900.0, 13.5, 2.0);
	CreateDynamicRaceCP(0, 1650.0, -1900.0, 13.5, 1660.0, -1910.0, 13.5, 8.0);
	CreateDynamicRaceCP(0, 1640.0, -1900.0, 13.5, 1650.0, -1900.0, 13.5, 8.0, -1, -1, -1, STREAMER_RACE_CP_SD);
	CreateDynamicRaceCP(1, 1660.0, -1910.0, 13.5, 0.0, 0.0, 0.0, 8.0);
	zone = GangZoneCreate(1590.000000, -1910.000000, 1630.000000, -1870.000000);
	CreateDynamicMapIcon(1600.000000, -1880.000000, 13.500000, 6, 0x00000000, 0, -1, -1, STREAMER_MAP_ICON_SD, MAPICON_LOCAL);

	new p = NPC_CreatePath();
	NPC_AddPointToPath(p, 1600.000000, -1920.000000, 13.500000, 0.50);
	NPC_AddPointToPath(p, 1620.000000, -1920.000000, 13.500000, 0.50);

	new n = NPC_Create("SWalker");
	NPC_Spawn(n);
	NPC_SetSkin(n, 101);
	NPC_SetPos(n, 1600.000000, -1920.000000, 13.500000);
	NPC_SetFacingAngle(n, 90.000000);
	NPC_SetWeapon(n, WEAPON:31);
	NPC_SetAmmo(n, 300);
	NPC_MoveByPath(n, p, NPC_MOVE_TYPE_JOG, 0.5000, true);
	n = NPC_Create("SDancer");
	NPC_Spawn(n);
	NPC_SetSkin(n, 102);
	NPC_SetPos(n, 1606.000000, -1915.000000, 13.500000);
	NPC_SetFacingAngle(n, 0.000000);
	NPC_SetInterior(n, 0);
	NPC_ApplyAnimation(n, "DANCING", "dnce_M_a", 4.10, true, false, false, false, 0);
	new nw = NPC_Create("SNodeWalker");
	NPC_Spawn(nw);
	NPC_SetSkin(nw, 7);
	NPC_SetPos(nw, 2490.000000, -1670.000000, 13.300000);
	NPC_SetFacingAngle(nw, 0.000000);
	new dr = NPC_Create("SDriver");
	NPC_Spawn(dr);
	NPC_SetSkin(dr, 255);
	NPC_SetPos(dr, 1700.000000, -1950.000000, 14.000000);
	NPC_SetFacingAngle(dr, 0.000000);

	NPC_OpenNode(15);
	NPC_SetNodePoint(15, 3);
	NPC_PlayNode(nw, 15, NPC_MOVE_TYPE_WALK, NPC_MOVE_SPEED_AUTO, 0.00, true);
	new streak = AddStaticVehicleEx(538, 2864.750000, 1254.159058, 12.126000, 180.000000, -1, -1, 100);
	NPC_PutInVehicle(dr, streak, 0);
	NPC_StartPlayback(dr, "TRAIN_LINDEN_TO_UNITY", true, 1700.000000, -1950.000000, 14.000000, 0.0, 0.0, 0.0);
}

new zone = -1;

stock hv_streamer_ShowForPlayer(playerid)
{
	GangZoneShowForPlayer(playerid, zone, 0xFF000088);
}
