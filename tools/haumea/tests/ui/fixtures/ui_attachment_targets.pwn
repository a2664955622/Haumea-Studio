#include <open.mp>
stock AttachmentTargets()
{
    new walker = NPC_Create("WorkspaceNPC");
    NPC_Spawn(walker);
    NPC_SetSkin(walker, 7);
    NPC_SetPos(walker, 2000.0, -3000.0, 5.0);
    AddPlayerClass(101, 2020.0, -3000.0, 5.0, 90.0, 0, 0, 0, 0, 0, 0);
    CreateVehicle(411, 2010.0, -3000.0, 5.0, 0.0, 1, 1, -1);
    CreateObject(1271, 2003.0, -3000.0, 6.0, 0.0, 0.0, 0.0);
    CreateObject(1337, 2013.0, -3000.0, 6.0, 0.0, 0.0, 0.0);
}
