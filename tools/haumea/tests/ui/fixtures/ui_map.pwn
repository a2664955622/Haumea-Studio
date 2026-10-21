// Objects out over the sea south of Los Santos, so nothing stands in front
// of them when the UI scripts click them.
#include <a_samp>

stock UiMap()
{
	new wall = CreateObject(19353, 2000.0, -3000.0, 5.0, 0.0, 0.0, 90.0); // wall
	SetObjectMaterial(wall, 0, 19297, "matcolours", "white", 0);
	CreateObject(1271, 2010.0, -3000.0, 5.0, 0.0, 0.0, 0.0); // crate
	CreateObject(1271, 2020.0, -3000.0, 5.0, 0.0, 0.0, 0.0); // crate 2
	CreateObject(1634, 2030.0, -3000.0, 5.0, 0.0, 0.0, 0.0); // plate
}
