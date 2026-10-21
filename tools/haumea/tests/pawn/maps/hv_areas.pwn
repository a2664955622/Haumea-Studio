// Haumea verification map: dynamic areas (streamer). Items with an areaid
// stream only for players inside that area. The canal block is a tester's
// own code: "new const areaid" reused per block, positional areaid.
#include <open.mp>
#include <streamer>

new Float:hv_areas_yard[] = { 2400.0, -1700.0, 2420.0, -1700.0, 2420.0, -1680.0, 2400.0, -1680.0 };

stock hv_areas_Canals()
{
	new tmpobjid;
	new const areaid = CreateDynamicCuboid(2080.4639, -1776.1041, -89.0, 2534.3696, -1381.6917, 0.0, -1, -1);
	tmpobjid = CreateDynamicObject(18990, 2488.229980, -1656.476928, -8.388013, 89.999992, -45.199764, -89.999961, -1, 0, -1, 300.00, 300.00, areaid);
	SetDynamicObjectMaterial(tmpobjid, 1, 18065, "ab_sfammumain", "breezewall5", 0xFFFCFFE6);
	CreateDynamicObject(18990, 2480.229980, -1650.476928, -8.388013, 89.999992, -45.199764, -89.999961, -1, 0, -1, 300.00, 300.00, areaid);
	CreateDynamicObject(19353, 2470.000000, -1640.000000, -8.000000, 0.000000, 0.000000, 90.000000, -1, 0, -1, 300.00, 300.00, areaid, 0);
}

stock hv_areas_Tunnel()
{
	// The same name again, another area: its items belong to this one.
	new const areaid = CreateDynamicCuboid(1900.0, -1800.0, -40.0, 1950.0, -1750.0, -10.0, -1, -1);
	CreateDynamicObject(19353, 1920.000000, -1780.000000, -20.000000, 0.000000, 0.000000, 0.000000, -1, -1, -1, STREAMER_OBJECT_SD, STREAMER_OBJECT_DD, areaid);
}

stock hv_areas_CreateObjects()
{
	hv_areas_Canals();
	hv_areas_Tunnel();
	new circle = CreateDynamicCircle(2300.0, -1600.0, 25.0, -1, -1);
	CreateDynamicPickup(1240, 2, 2300.000000, -1600.000000, 13.500000, -1, -1, -1, STREAMER_PICKUP_SD, circle);
	new cylinder = CreateDynamicCylinder(2340.0, -1600.0, 10.0, 30.0, 15.0, 0, 0);
	CreateDynamicObject(1271, 2340.000000, -1600.000000, 13.500000, 0.000000, 0.000000, 0.000000, 0, 0, -1, 300.00, 300.00, cylinder);
	new sphere = CreateDynamicSphere(2380.0, -1600.0, 14.0, 12.0);
	CreateDynamicCP(2380.000000, -1600.000000, 13.500000, 3.00, -1, -1, -1, STREAMER_CP_SD, sphere);
	new rectangle = CreateDynamicRectangle(2350.0, -1650.0, 2370.0, -1630.0);
	CreateDynamic3DTextLabel("Members only", 0xFFFFFFFF, 2360.000000, -1640.000000, 14.000000, 20.00, INVALID_PLAYER_ID, INVALID_VEHICLE_ID, false, -1, -1, -1, STREAMER_3D_TEXT_LABEL_SD, rectangle);
	new cube = CreateDynamicCube(2300.0, -1700.0, 10.0, 2310.0, -1690.0, 20.0, -1, -1);
	CreateDynamicMapIcon(2305.000000, -1695.000000, 13.000000, 52, 0, -1, -1, -1, STREAMER_MAP_ICON_SD, MAPICON_LOCAL, cube);
	new yard = CreateDynamicPolygon(hv_areas_yard, 10.0, 30.0, sizeof hv_areas_yard);
	CreateDynamicActor(280, 2410.000000, -1690.000000, 13.500000, 90.000000, true, 100.00, -1, -1, -1, STREAMER_ACTOR_SD, yard);
	new const triangle = CreateDynamicPolygon(Float:{ 2450.0, -1700.0, 2470.0, -1700.0, 2460.0, -1680.0 }, -FLOAT_INFINITY, FLOAT_INFINITY, 6);
	CreateDynamicObject(1271, 2460.000000, -1693.000000, 13.500000, 0.000000, 0.000000, 0.000000, -1, -1, -1, STREAMER_OBJECT_SD, STREAMER_OBJECT_DD, triangle);
	// An area nothing uses.
	new spare = CreateDynamicCircle(2500.0, -1600.0, 5.0);
	#pragma unused spare
}
