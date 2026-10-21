// Mixed fixture: Texture Studio style, streamer, constants, attachments, tags.
#include <a_samp>
#include <streamer>

new tmpobjid;

public OnPlayerConnect(playerid)
{
	RemoveBuildingForPlayer(playerid, 3563, 2063.132813, -1731.78125, 15.2734375, 0.25);
	// Sound places: a positioned stream and a game sound (from another map's helper)
	PlayAudioStreamForPlayer(playerid, "http://example.com/radio.pls", 1630.0, -1890.0, 13.5, 60.0, true);
	return 1;
}

stock LoadTest()
{
	// Texture Studio export: one temp handle reused for every object
	tmpobjid = CreateObject(19353, 1600.0, -1890.0, 14.0, 0.0, 0.0, 90.0, 300.00); // wall A
	SetObjectMaterial(tmpobjid, 0, 19297, "matcolours", "white", 0xFFFF0000);
	tmpobjid = CreateObject(19353, 1604.0, -1890.0, 14.0, 0.0, 0.0, 90.0, 300.00); // wall B
	SetObjectMaterial(tmpobjid, 0, 19297, "matcolours", "white", 0xFF00FF00);
	tmpobjid = CreateDynamicObjectEx(19353, 1608.0, -1890.0, 14.0, 0.0, 0.0, 90.0, 150.0, 120.0);
	SetDynamicObjectMaterialText(tmpobjid, 0, "TS {FF0000}text", OBJECT_MATERIAL_SIZE_256x128, "Arial", 28, true, 0xFFFFFFFF, 0, OBJECT_MATERIAL_TEXT_ALIGN_CENTER);

	// Texture Studio map whose world/interior are script variables
	tmpobjid = CreateDynamicObject(3578, 1620.0, -1890.0, 14.0, 0.0, -8.0, 90.0, object_world, object_int, -1, 300.00, 300.00);
	SetDynamicObjectMaterial(tmpobjid, 0, 16640, "a51", "concretewall22_256", 0x00000000);

	// Vehicle with a paintjob, parts and a number plate
	new elegy = AddStaticVehicleEx(562, 1600.0, -1900.0, 13.2, 90.0, 140, 201, 300);
	ChangeVehiclePaintjob(elegy, 1);
	AddVehicleComponent(elegy, 1010);
	AddVehicleComponent(elegy, 1036);
	AddVehicleComponent(elegy, 1080);
	AddVehicleComponent(elegy, 1171);
	AddVehicleComponent(elegy, 1009); // replaces 1010: one part per slot
	AddVehicleComponent(elegy, 1100); // doesn't fit an Elegy
	SetVehicleNumberPlate(elegy, "NEU \"FOX\"");

	// Streamer object with world, interior and distances
	new STREAMER_TAG_OBJECT:sign = CreateDynamicObject(19353, 1612.0, -1890.0, 14.0, 10.0, 20.0, 30.0, 5, 3, -1, 200.0, 150.0); // sign @group("Shop") @note("front sign")
	SetDynamicObjectMaterialText(sign, 1, "OPEN");

	// Native text with defaults and an attachment chain
	new base = CreateObject(1271, 1616.0, -1890.0, 13.5, 0.0, 0.0, 45.0); // base @group("Shop")
	new crate = CreateObject(1271, 1616.0, -1890.0, 14.5, 0.0, 0.0, 0.0);
	SetObjectMaterialText(crate, "Crate");
	AttachObjectToObject(crate, base, 0.0, 0.0, 0.8, 15.0, 25.0, 35.0, 1);
	// A named object nothing else uses, and a named vehicle
	new lamp = CreateObject(1215, 1636.0, -1890.0, 13.5, 0.0, 0.0, 0.0);
	new bike = AddStaticVehicleEx(468, 1640.0, -1890.0, 13.5, 0.0, 1, 1, 60);
	// Checkpoints: a plain one and a hand-written race listed out of order
	CreateDynamicCP(1644.0, -1890.0, 13.5, 3.0);
	CreateDynamicRaceCP(1, 1660.0, -1890.0, 13.5, 0.0, 0.0, 0.0, 8.0);
	CreateDynamicRaceCP(0, 1650.0, -1890.0, 13.5, 1660.0, -1890.0, 13.5, 8.0);
	Haumea_old_AddSound(1142, 1634.0, -1890.0, 13.5, 40.0, false, 3, 1); // sound @note("crowd")
	return 1;
}
