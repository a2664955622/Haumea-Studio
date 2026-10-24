#include <a_samp>
stock XMLMergeTest()
{
    new base = CreateObject(1271, 2000.0, -3005.0, 5.0, 0.0, 0.0, 0.0); // @group("Assembly")
    new lamp = CreateObject(1215, 2000.0, -3005.0, 6.0, 0.0, 0.0, 0.0); // @group("Assembly") @note("linked lamp")
    AttachObjectToObject(lamp, base, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
}
