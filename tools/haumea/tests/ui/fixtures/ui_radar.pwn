// Objects for radar art: two groups side by side, one turned.
#tryinclude <open.mp>
#if !defined _INC_open_mp
	#include <a_samp>
#endif

stock Haumea_ui_radar_CreateObjects()
{
	CreateObject(1634, 2000.000000, -3000.000000, 5.000000, 0.000000, 0.000000, 0.000000); // landjump2 @group("ramps")
	CreateObject(1634, 2004.000000, -3000.000000, 5.000000, 0.000000, 0.000000, 0.000000); // landjump2 @group("ramps")
	CreateObject(1634, 2008.000000, -3000.000000, 6.000000, 0.000000, 0.000000, 45.000000); // landjump2 @group("ramps")
	CreateObject(1271, 2000.000000, -2994.000000, 5.000000, 0.000000, 0.000000, 0.000000); // gunbox @group("boxes")
	CreateObject(1271, 2002.000000, -2994.000000, 5.000000, 0.000000, 0.000000, 0.000000); // gunbox @group("boxes")
	CreateObject(1280, 2006.000000, -2994.000000, 5.000000, 0.000000, 0.000000, 90.000000); // parkbench1 @group("boxes")
}
