// Windows-1252 text, as pawno and Texture Studio write it: labels, material
// text, textdraws and notes must reach the .amx as the same bytes.
#include <a_samp>

stock hv_text_CreateObjects()
{
	new sign = CreateObject(19353, 1600.0, -1890.0, 14.0, 0.0, 0.0, 90.0); // sign @note("Coração da praça")
	SetObjectMaterialText(sign, "Ação, Coração, € and ñ", 0, OBJECT_MATERIAL_SIZE_256x128, "Arial", 24, true, 0xFFFFFFFF, 0, OBJECT_MATERIAL_TEXT_ALIGN_CENTER);
	Create3DTextLabel("Ação, Coração, € and ñ", 0xFFFFFFFF, 1602.0, -1890.0, 14.0, 20.0, 0, false);
}

new Text:hv_text_td0, PlayerText:hv_text_ptd0[MAX_PLAYERS];

stock hv_text_CreateTextDraws()
{
	hv_text_td0 = TextDrawCreate(320.0, 20.0, "Ação, Coração, € and ñ");
}

stock hv_text_ShowTextDraws(playerid)
{
	hv_text_ptd0[playerid] = CreatePlayerTextDraw(playerid, 320.0, 60.0, "Olá ñandú");
	PlayerTextDrawShow(playerid, hv_text_ptd0[playerid]);
	TextDrawShowForPlayer(playerid, hv_text_td0);
}
