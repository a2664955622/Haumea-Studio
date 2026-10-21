// Two models side by side over the sea, for material edits on a selection:
// each has a slot 0, textured white so Object Info offers "Browse textures".
#include <a_samp>

stock UiMaterials()
{
	new crate_a = CreateObject(1271, 2000.0, -3000.0, 5.0, 0.0, 0.0, 0.0);
	new crate_b = CreateObject(1271, 2004.0, -3000.0, 5.0, 0.0, 0.0, 0.0);
	new wall_c = CreateObject(19353, 2010.0, -3000.0, 5.0, 0.0, 0.0, 90.0);
	SetObjectMaterial(crate_a, 0, 19297, "matcolours", "white", 0);
	SetObjectMaterial(crate_b, 0, 19297, "matcolours", "white", 0);
	SetObjectMaterial(wall_c, 0, 19297, "matcolours", "white", 0);
}
