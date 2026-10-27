#include <a_samp>
stock AttachmentDuplicates(playerid)
{
    CreateObject(1271, 2000.0, -3000.0, 5.0, 0.0, 0.0, 0.0);
    CreateObject(1271, 2000.0, -3000.0, 5.0, 0.0, 0.0, 0.0);
    SetPlayerAttachedObject(playerid, 4, 1271, 6, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0xFFAA0000, 0);
    SetPlayerAttachedObject(playerid, 5, 1271, 6, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0xFFAA0000, 0);
}
