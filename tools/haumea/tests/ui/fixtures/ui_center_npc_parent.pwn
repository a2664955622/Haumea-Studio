#include <open.mp>
stock NpcParent()
{
    new walker = NPC_Create("CenterNPC");
    NPC_Spawn(walker);
    NPC_SetSkin(walker, 101);
    NPC_SetPos(walker, 2000.0, -3000.0, 5.0);
    SetPlayerAttachedObject(walker, 4, 1271, 6, 20.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.5, 1.0, 0.8, 0xFFAA0000, 0xFF00AA00);
}
