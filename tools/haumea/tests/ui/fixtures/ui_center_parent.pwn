#include <a_samp>
stock CenterParentFixture()
{
    new root = CreateObject(1271, 2000.0, -3000.0, 5.0, 0.0, 0.0, 0.0);
    new child = CreateObject(1271, 2020.0, -3000.0, 5.0, 0.0, 0.0, 180.0);
    AttachObjectToObject(child, root, 20.0, 0.0, 0.0, 0.0, 0.0, 180.0, true);
}
