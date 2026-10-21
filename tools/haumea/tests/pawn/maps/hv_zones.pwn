// Gang zones in every mode: global, per player and streamed (open.mp only).
#include <open.mp>
#include <streamer>

static hv_zones_gate = -1;

stock Haumea_hv_zones_CreateObjects()
{
	hv_zones_gate = GangZoneCreate(1590.000000, -1910.000000, 1630.000000, -1870.000000);
	Haumea_hv_zones_AddPlayerZone(1500.0, -1700.0, 1520.0, -1680.0, 0x4E79A7C8); // gangzone @group("radar art")
	Haumea_hv_zones_AddPlayerZone(1520.0, -1700.0, 1530.0, -1660.0, 0x59A14FC8); // gangzone @group("radar art") @note("park")
	Haumea_hv_zones_AddStreamedZone(1000.0, -1000.0, 1010.0, -990.0, 0xE15759C8); // gangzone @group("streamed art")
	Haumea_hv_zones_AddStreamedZone(1300.0, -1000.0, 1340.0, -980.0, 0xEDC948FF); // gangzone @group("streamed art")
	Haumea_hv_zones_AddStreamedZone(2000.0, -1500.0, 2004.0, -1496.0, 0x76B7B280); // gangzone @group("streamed art")
}

stock Haumea_hv_zones_ShowForPlayer(playerid)
{
	GangZoneShowForPlayer(playerid, hv_zones_gate, 0xFF000088);
}
