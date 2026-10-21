// Haumea verification map: every element, native natives (open.mp for NPCs).
#include <a_samp>

stock hv_samp_RemoveWorldObjectsForPlayer(playerid)
{
	RemoveBuildingForPlayer(playerid, 3563, 2063.132813, -1731.781250, 15.273438, 0.25);
}

stock hv_samp_CreateObjects()
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
	new bike = AddStaticVehicleEx(468, 1625.000000, -1880.000000, 13.300000, 0.000000, 1, 1, 60);
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
	AddPlayerClass(1, 1614.000000, -1900.000000, 13.500000, 0.000000, WEAPON_NIGHTSTICK, 1, 44, 1, WEAPON_MOLOTOV, 5);

	gate_zone = GangZoneCreate(1590.000000, -1910.000000, 1630.000000, -1870.000000);






}

static gate_zone = -1;

stock hv_samp_ShowForPlayer(playerid)
{
	GangZoneShowForPlayer(playerid, gate_zone, 0xFF000088);
	SetPlayerMapIcon(playerid, 0, 1600.000000, -1880.000000, 13.500000, 6, 0x00000000, 0);
}
