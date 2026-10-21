#ifndef MATERIALTEXT_H
#define MATERIALTEXT_H

#include <string>
#include <vector>

struct ObjectMaterialOverride;

// SA-MP object material text rasteriser.
//
// The 0.3.7 client draws SetObjectMaterialText through D3DXCreateFont and
// ID3DXFont::DrawText into a render target cleared to the background colour.
// This module reproduces that output with stb_truetype and the Windows font
// files the client would use, so the editor preview matches the game.

// OBJECT_MATERIAL_SIZE_* code to texture dimensions. Returns false for codes
// the client rejects (it then never creates the text texture).
bool GetMaterialTextDimensions(int materialSize, int *width, int *height);

// Renders the text texture for a material slot. The caller owns the image.
rw::Image *RenderMaterialTextImage(const ObjectMaterialOverride &material);

// Font family names found on this system, sorted case-insensitively.
const std::vector<std::string> &GetMaterialFontFamilies(void);

// Describes which installed face the preview uses for a font name. Returns
// false when the name is unknown and a fallback face is used instead.
bool DescribeMaterialFont(const char *fontName, bool bold, std::string *description);

// Forgets the font index so newly installed fonts are found.
void RescanMaterialFonts(void);

#endif
