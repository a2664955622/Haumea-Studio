// A sign whose material text font nobody has (Map Check notes it).
#include <a_samp>

stock UiMaterialFont()
{
	new o = CreateObject(19353, 2000.000000, -3000.000000, 5.000000, 0.000000, 0.000000, 0.000000);
	SetObjectMaterialText(o, "Sign", 0, OBJECT_MATERIAL_SIZE_256x128, "Haumea Missing Font", 24, 1, 0xFFFFFFFF, 0, OBJECT_MATERIAL_TEXT_ALIGN_CENTER);
}
