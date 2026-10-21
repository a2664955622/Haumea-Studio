// A map saved before textdraws had their own file.

#tryinclude <open.mp>
#if !defined _INC_open_mp
	#include <a_samp>
#endif

new Text:OldTitle;

stock Haumea_ui_old_textdraws_CreateObjects()
{
	CreateObject(1271, 2010.000000, -3000.000000, 5.000000, 0.000000, 0.000000, 0.000000); // gunbox
}

stock Haumea_ui_old_textdraws_CreateTextDraws()
{
	OldTitle = TextDrawCreate(320.000, 10.000, "Old title");
}
