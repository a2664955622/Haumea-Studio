// Radar art as global gang zones: works on SA-MP 0.3.7 and open.mp.
#tryinclude <open.mp>
#if !defined _INC_open_mp
	#include <a_samp>
#endif

static hv_radar_zone_0 = -1, hv_radar_zone_1 = -1, hv_radar_zone_2 = -1;

stock Haumea_hv_radar_CreateObjects()
{
	hv_radar_zone_0 = GangZoneCreate(2000.000000, -3004.000000, 2012.000000, -2996.000000); // gangzone @group("radar art")
	hv_radar_zone_1 = GangZoneCreate(2000.000000, -2996.000000, 2004.000000, -2992.000000); // gangzone @group("radar art")
	hv_radar_zone_2 = GangZoneCreate(2004.000000, -2996.000000, 2012.000000, -2992.000000); // gangzone @group("radar art")
}

stock Haumea_hv_radar_ShowForPlayer(playerid)
{
	GangZoneShowForPlayer(playerid, hv_radar_zone_0, 0x8C7A5AC8);
	GangZoneShowForPlayer(playerid, hv_radar_zone_1, 0x5A7A3CC8);
	GangZoneShowForPlayer(playerid, hv_radar_zone_2, 0x9C9C9CC8);
}
