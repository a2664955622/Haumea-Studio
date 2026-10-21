// Haumea verification map: RemoveBuildingForPlayer with model -1 (every building
// in the sphere, HD and LOD) and with a model.
#include <a_samp>

stock hv_removals_RemoveWorldObjectsForPlayer(playerid)
{
	RemoveBuildingForPlayer(playerid, -1, 2063.000000, -1731.000000, 15.000000, 5.000000);
	RemoveBuildingForPlayer(playerid, 4206, 1479.554688, -1639.609375, 13.648438, 0.25);
}

stock hv_removals_CreateObjects()
{
	CreateObject(19353, 2063.000000, -1731.000000, 14.000000, 0.000000, 0.000000, 90.000000);
}
