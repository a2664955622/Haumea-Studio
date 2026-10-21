// The floor and three named boxes, as streamer code, for el_area.uis.
#include <a_samp>
#include <streamer>

stock UiAreas()
{
	CreateDynamicObject(19377, 2000.000000, -3000.000000, 4.000000, 0.000000, 90.000000, 0.000000);
	new box_a = CreateDynamicObject(1271, 1998.000000, -3002.000000, 4.500000, 0.000000, 0.000000, 0.000000);
	new box_b = CreateDynamicObject(1271, 2000.000000, -3002.000000, 4.500000, 0.000000, 0.000000, 0.000000);
	new box_c = CreateDynamicObject(1271, 2002.000000, -3002.000000, 4.500000, 0.000000, 0.000000, 0.000000);
	#pragma unused box_a, box_b, box_c
}
