// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "icons.h"
#include "font_data.h"
#include "i18n.h"
#include <algorithm>
#include <string>
#include <cstdio>

ImFont *gCodeFont;
ImFont *gTitleFont;	// large Inter for the splash screen title

// Accent colors — used for highlights, active elements and selection.
static int gUiTheme = UI_THEME_DARK;
static const ImVec4 kDarkAccent       = ImVec4(0.02f, 0.78f, 0.78f, 1.00f);
static const ImVec4 kDarkAccentHover  = ImVec4(0.39f, 0.86f, 0.76f, 1.00f);
static const ImVec4 kDarkAccentActive = ImVec4(0.20f, 0.64f, 0.56f, 1.00f);
static const ImVec4 kLightAccent       = ImVec4(0.00f, 0.43f, 0.48f, 1.00f);
static const ImVec4 kLightAccentHover  = ImVec4(0.04f, 0.34f, 0.31f, 1.00f);
static const ImVec4 kLightAccentActive = ImVec4(0.06f, 0.31f, 0.29f, 1.00f);

int
GetUiTheme(void)
{
	return gUiTheme;
}

void
SetUiTheme(int theme)
{
	gUiTheme = theme == UI_THEME_LIGHT ? UI_THEME_LIGHT : UI_THEME_DARK;
	if(ImGui::GetCurrentContext() != nil)
		SetupStyle();
}

ImVec4
UiSemanticColor(int color)
{
	bool light = gUiTheme == UI_THEME_LIGHT;
	switch(color){
	case UI_COLOR_SUCCESS:
		return light ? ImVec4(0.10f, 0.42f, 0.25f, 1.0f) : ImVec4(0.45f, 0.85f, 0.55f, 1.0f);
	case UI_COLOR_WARNING:
		return light ? ImVec4(0.53f, 0.34f, 0.00f, 1.0f) : ImVec4(1.00f, 0.78f, 0.25f, 1.0f);
	case UI_COLOR_ERROR:
		return light ? ImVec4(0.68f, 0.14f, 0.12f, 1.0f) : ImVec4(1.00f, 0.42f, 0.38f, 1.0f);
	case UI_COLOR_INFO:
		return light ? ImVec4(0.10f, 0.36f, 0.55f, 1.0f) : ImVec4(0.45f, 0.80f, 1.0f, 1.0f);
	}
	return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

void
SetupStyle(void)
{
	ImGuiStyle &style = ImGui::GetStyle();
	bool light = gUiTheme == UI_THEME_LIGHT;
	if(light)
		ImGui::StyleColorsLight(&style);
	else
		ImGui::StyleColorsDark(&style);
	ImVec4 accent = light ? kLightAccent : kDarkAccent;
	ImVec4 accentHover = light ? kLightAccentHover : kDarkAccentHover;
	ImVec4 accentActive = light ? kLightAccentActive : kDarkAccentActive;

	// ---- Geometry ----
	style.WindowRounding    = 8.0f;
	style.ChildRounding     = 6.0f;
	style.FrameRounding     = 6.0f;
	style.PopupRounding     = 6.0f;
	style.ScrollbarRounding = 7.0f;
	style.GrabRounding      = 4.0f;
	style.TabRounding       = 4.0f;

	style.WindowPadding     = ImVec2(16, 14);
	style.FramePadding      = ImVec2(8, 5);
	style.ItemSpacing       = ImVec2(8, 6);
	style.ItemInnerSpacing  = ImVec2(6, 4);
	style.ScrollbarSize     = 12.0f;
	style.GrabMinSize       = 10.0f;
	style.WindowBorderSize  = 0.0f;
	style.ChildBorderSize   = 0.0f;
	style.PopupBorderSize   = 1.0f;
	style.FrameBorderSize   = 0.0f;
	style.TabBorderSize     = 0.0f;
	style.TabBarBorderSize  = 0.0f;
	style.TabBarOverlineSize = 0.0f;

	style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);

#ifdef IMGUI_HAS_DOCK
	// Panels dock by dragging their title bar onto the edges of the
	// screen or onto another panel. Needs librw patched with
	// tools/release/patch_librw_haumea.py.
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
	style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
	style.SeparatorTextPadding = ImVec2(0.0f, 4.0f);
	style.SeparatorTextBorderSize = 0.0f;

	// ---- Colors ----
	ImVec4 *c = style.Colors;

	// Backgrounds
	c[ImGuiCol_WindowBg]             = light ? ImVec4(0.96f, 0.97f, 0.98f, 1.00f) : ImVec4(0.075f, 0.095f, 0.115f, 1.00f);
	c[ImGuiCol_ChildBg]              = light ? ImVec4(0.93f, 0.95f, 0.96f, 0.00f) : ImVec4(0.090f, 0.105f, 0.125f, 0.00f);
	c[ImGuiCol_PopupBg]              = light ? ImVec4(0.99f, 0.995f, 1.00f, 1.00f) : ImVec4(0.105f, 0.122f, 0.145f, 0.98f);

	// Borders
	c[ImGuiCol_Border]               = light ? ImVec4(0.70f, 0.76f, 0.77f, 0.85f) : ImVec4(0.24f, 0.29f, 0.33f, 0.45f);
	c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

	// Text
	c[ImGuiCol_Text]                 = light ? ImVec4(0.13f, 0.16f, 0.17f, 1.00f) : ImVec4(0.93f, 0.95f, 0.96f, 1.00f);
	c[ImGuiCol_TextDisabled]         = light ? ImVec4(0.40f, 0.46f, 0.47f, 1.00f) : ImVec4(0.59f, 0.65f, 0.69f, 1.00f);

	// Frames (input boxes, sliders, checkboxes)
	c[ImGuiCol_FrameBg]              = light ? ImVec4(0.88f, 0.91f, 0.92f, 1.00f) : ImVec4(0.13f, 0.15f, 0.18f, 1.00f);
	c[ImGuiCol_FrameBgHovered]       = light ? ImVec4(0.83f, 0.88f, 0.89f, 1.00f) : ImVec4(0.18f, 0.21f, 0.24f, 1.00f);
	c[ImGuiCol_FrameBgActive]        = light ? ImVec4(0.78f, 0.84f, 0.84f, 1.00f) : ImVec4(0.22f, 0.26f, 0.29f, 1.00f);

	// Title bar
	c[ImGuiCol_TitleBg]              = light ? ImVec4(0.90f, 0.93f, 0.94f, 1.00f) : ImVec4(0.065f, 0.075f, 0.09f, 1.00f);
	c[ImGuiCol_TitleBgActive]        = light ? ImVec4(0.86f, 0.91f, 0.91f, 1.00f) : ImVec4(0.10f, 0.13f, 0.15f, 1.00f);
	c[ImGuiCol_TitleBgCollapsed]     = light ? ImVec4(0.90f, 0.93f, 0.94f, 0.70f) : ImVec4(0.065f, 0.075f, 0.09f, 0.70f);

	// Menu bar
	c[ImGuiCol_MenuBarBg]            = light ? ImVec4(0.91f, 0.94f, 0.95f, 1.00f) : ImVec4(0.09f, 0.105f, 0.125f, 1.00f);

	// Scrollbar
	c[ImGuiCol_ScrollbarBg]          = light ? ImVec4(0.90f, 0.93f, 0.94f, 0.80f) : ImVec4(0.08f, 0.095f, 0.11f, 0.80f);
	c[ImGuiCol_ScrollbarGrab]        = light ? ImVec4(0.68f, 0.73f, 0.74f, 1.00f) : ImVec4(0.24f, 0.29f, 0.32f, 1.00f);
	c[ImGuiCol_ScrollbarGrabHovered] = light ? ImVec4(0.58f, 0.66f, 0.67f, 1.00f) : ImVec4(0.34f, 0.41f, 0.43f, 1.00f);
	c[ImGuiCol_ScrollbarGrabActive]  = light ? ImVec4(0.45f, 0.60f, 0.59f, 1.00f) : ImVec4(0.42f, 0.52f, 0.51f, 1.00f);

	// Checkbox / Radio mark
	c[ImGuiCol_CheckMark]            = accent;

	// Slider
	c[ImGuiCol_SliderGrab]           = light ? ImVec4(0.50f, 0.58f, 0.59f, 1.00f) : ImVec4(0.37f, 0.46f, 0.48f, 1.00f);
	c[ImGuiCol_SliderGrabActive]     = accent;

	// Buttons
	c[ImGuiCol_Button]               = light ? ImVec4(0.88f, 0.91f, 0.92f, 1.00f) : ImVec4(0.14f, 0.17f, 0.19f, 1.00f);
	c[ImGuiCol_ButtonHovered]        = light ? ImVec4(0.81f, 0.87f, 0.88f, 1.00f) : ImVec4(0.20f, 0.25f, 0.27f, 1.00f);
	c[ImGuiCol_ButtonActive]         = accentActive;

	// Headers (collapsing headers, selectable, menu items)
	c[ImGuiCol_Header]               = light ? ImVec4(0.85f, 0.90f, 0.90f, 1.00f) : ImVec4(0.14f, 0.18f, 0.19f, 1.00f);
	c[ImGuiCol_HeaderHovered]        = light ? ImVec4(0.78f, 0.86f, 0.86f, 1.00f) : ImVec4(0.21f, 0.28f, 0.28f, 1.00f);
	c[ImGuiCol_HeaderActive]         = accentActive;

	// Separators
	c[ImGuiCol_Separator]            = light ? ImVec4(0.77f, 0.81f, 0.82f, 1.00f) : ImVec4(0.22f, 0.27f, 0.29f, 1.00f);
	c[ImGuiCol_SeparatorHovered]     = accentHover;
	c[ImGuiCol_SeparatorActive]      = accent;

	// Resize grip
	c[ImGuiCol_ResizeGrip]           = light ? ImVec4(0.45f, 0.65f, 0.61f, 0.40f) : ImVec4(0.22f, 0.32f, 0.31f, 0.40f);
	c[ImGuiCol_ResizeGripHovered]    = accentHover;
	c[ImGuiCol_ResizeGripActive]     = accent;

	// Tabs
	c[ImGuiCol_Tab]                  = light ? ImVec4(0.90f, 0.93f, 0.94f, 1.00f) : ImVec4(0.105f, 0.13f, 0.15f, 1.00f);
	c[ImGuiCol_TabHovered]           = light ? ImVec4(0.82f, 0.89f, 0.89f, 1.00f) : ImVec4(0.18f, 0.24f, 0.25f, 1.00f);
	c[ImGuiCol_TabSelected]          = light ? ImVec4(0.82f, 0.89f, 0.89f, 1.00f) : ImVec4(0.15f, 0.22f, 0.22f, 1.00f);

	c[ImGuiCol_TabDimmed] = c[ImGuiCol_Tab];
	c[ImGuiCol_TabDimmedSelected] = c[ImGuiCol_TabSelected];
	c[ImGuiCol_TabSelectedOverline] = accent;
	c[ImGuiCol_TabDimmedSelectedOverline] = accent;

	// Table
	c[ImGuiCol_TableHeaderBg]        = light ? ImVec4(0.90f, 0.93f, 0.94f, 1.00f) : ImVec4(0.11f, 0.14f, 0.16f, 1.00f);
	c[ImGuiCol_TableBorderStrong]    = light ? ImVec4(0.75f, 0.80f, 0.81f, 1.00f) : ImVec4(0.22f, 0.27f, 0.29f, 1.00f);
	c[ImGuiCol_TableBorderLight]     = light ? ImVec4(0.81f, 0.85f, 0.86f, 1.00f) : ImVec4(0.17f, 0.21f, 0.23f, 1.00f);
	c[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	c[ImGuiCol_TableRowBgAlt]        = light ? ImVec4(0.0f, 0.0f, 0.0f, 0.025f) : ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

	// Selection / Drag-drop
	c[ImGuiCol_TextSelectedBg]       = ImVec4(accent.x, accent.y, accent.z, 0.25f);
	c[ImGuiCol_DragDropTarget]       = accent;

	// Nav highlight
	c[ImGuiCol_NavHighlight]         = accent;
	c[ImGuiCol_NavWindowingHighlight]= light ? ImVec4(0.10f, 0.15f, 0.16f, 0.55f) : ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	c[ImGuiCol_NavWindowingDimBg]    = light ? ImVec4(0.20f, 0.24f, 0.25f, 0.14f) : ImVec4(0.80f, 0.80f, 0.80f, 0.20f);

	// Modal dimming
	c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, light ? 0.32f : 0.55f);

	// Keep ImGui geometry in GLFW logical coordinates. The camera raster and
	// viewport map those positions to framebuffer pixels; scissor clips need
	// their own framebuffer scaling in the draw-list adapter.
}

void
SetupFonts(void)
{
	ImGuiIO &io = ImGui::GetIO();
	float fontSize = 15.0f;

	// Only bake the exact icons used by Haumea. Packing the entire private-use
	// area creates a very large atlas, which can fail on some D3D9 GPUs during
	// startup even though the rest of the app is fine.
	static const ImWchar iconRanges[] = {
		0xe13a, 0xe13a, // code-compare
		0xf002, 0xf005, // magnifying-glass, star
		0xf007, 0xf007, // user
		0xf031, 0xf031, // font
		0xf0c8, 0xf0c8, // square
		0xf276, 0xf276, // map-pin
		0xf3c5, 0xf3c5, // location-dot
		0xf00c, 0xf00d, // check, xmark
		0xf013, 0xf013, // gear
		0xf017, 0xf017, // clock
		0xf023, 0xf023, // lock
		0xf028, 0xf028, // volume-high
		0xf03a, 0xf03a, // list
		0xf043, 0xf043, // droplet
		0xf047, 0xf047, // arrows-up-down-left-right
		0xf04b, 0xf04b, // play
		0xf04d, 0xf04d, // stop
		0xf059, 0xf05a, // circle-question, circle-info
		0xf06e, 0xf06e, // eye
		0xf070, 0xf071, // eye-slash, triangle-exclamation
		0xf076, 0xf076, // magnet
		0xf07c, 0xf07c, // folder-open
		0xf0ac, 0xf0ad, // globe, wrench
		0xf0c1, 0xf0c1, // link
		0xf0c5, 0xf0c7, // copy, floppy-disk
		0xf0db, 0xf0db, // table-columns
		0xf0e7, 0xf0ea, // bolt, paste
		0xf0f3, 0xf0f3, // bell
		0xf11b, 0xf11b, // gamepad
		0xf15b, 0xf15b, // file
		0xf185, 0xf185, // sun
		0xf1b2, 0xf1b2, // cube
		0xf1da, 0xf1da, // clock-rotate-left
		0xf1f8, 0xf1fc, // trash, paintbrush
		0xf245, 0xf245, // arrow-pointer
		0xf2d0, 0xf2d0, // window-maximize
		0xf2ea, 0xf2f1, // rotate-left, rotate
		0xf2f5, 0xf2f9, // right-from-bracket, rotate-right
		0xf304, 0xf304, // pen
		0xf56e, 0xf56f, // file-export, file-import
		0xf5cb, 0xf5cb, // vector-square
		0xf5fd, 0xf5fd, // layer-group
		0xf601, 0xf601, // location-crosshairs
		0xf6fc, 0xf6fc, // mountain
		0xf743, 0xf743, // cloud-sun
		0xf773, 0xf773, // water
		0,
	};

	// --- Main font: Inter (embedded) ---
	ImFontConfig fontCfg;
	fontCfg.FontDataOwnedByAtlas = false;
	io.Fonts->AddFontFromMemoryTTF(
		(void *)inter_font_data, inter_font_size,
		fontSize, &fontCfg);

	// --- Icon font: FontAwesome 6 (embedded, merged) ---
	ImFontConfig iconCfg;
	iconCfg.MergeMode = true;
	iconCfg.PixelSnapH = true;
	iconCfg.GlyphMinAdvanceX = fontSize;
	iconCfg.FontDataOwnedByAtlas = false;
	io.Fonts->AddFontFromMemoryTTF(
		(void *)fa_solid_font_data, fa_solid_font_size,
		fontSize - 1.0f, &iconCfg, iconRanges);

	// --- Other scripts: Inter covers Latin, Greek and Cyrillic, and the atlas
	// holds only the ranges it is given, so a catalog in any other script drew
	// every character as "?". Bake the characters the catalogs translate into,
	// so a language pays only for the glyphs it uses, and merge them in from a
	// face that has them. Static, like iconRanges: the atlas is built at the
	// first frame, after this function returns. ---
	static const ImWchar *scriptRanges = nil;
	static std::vector<ImWchar> scriptRangeList;
	static bool eastAsian = false;
	if(scriptRanges == nil){
		std::vector<unsigned int> codepoints;
		I18nCatalogCodepoints(codepoints);
		// Text the user types (group names, notes) is in no catalog, so an East
		// Asian catalog also gets the common Chinese set. Only characters past
		// Latin Extended-A need baking: the defaults cover the rest, which is
		// why a catalog that only uses them changes nothing here.
		for(unsigned int cp : codepoints)
			if(cp >= 0x2E80)
				eastAsian = true;
		std::vector<unsigned int> baked;
		for(unsigned int cp : codepoints)
			if(cp >= 0x180)
				baked.push_back(cp);
		if(eastAsian){
			for(const ImWchar *r = io.Fonts->GetGlyphRangesChineseSimplifiedCommon(); r[0]; r += 2)
				for(unsigned int c = r[0]; c <= r[1]; c++)
					if(c >= 0x180)
						baked.push_back(c);
		}
		std::sort(baked.begin(), baked.end());
		baked.erase(std::unique(baked.begin(), baked.end()), baked.end());
		// Neighbouring characters share one range; a range per character would
		// make this array needlessly large.
		for(size_t i = 0; i < baked.size(); ){
			size_t last = i;
			while(last + 1 < baked.size() && baked[last + 1] == baked[last] + 1)
				last++;
			scriptRangeList.push_back((ImWchar)baked[i]);
			scriptRangeList.push_back((ImWchar)baked[last]);
			i = last + 1;
		}
		if(!scriptRangeList.empty()){
			scriptRangeList.push_back(0);
			scriptRanges = scriptRangeList.data();
		}
	}
	if(scriptRanges != nil && !eastAsian){
		// Inter carries Greek, Cyrillic and the Latin extensions itself: bake
		// those from the embedded copy instead of loading another face.
		ImFontConfig latinCfg;
		latinCfg.MergeMode = true;
		latinCfg.PixelSnapH = true;
		latinCfg.FontDataOwnedByAtlas = false;
		io.Fonts->AddFontFromMemoryTTF(
			(void *)inter_font_data, inter_font_size,
			fontSize, &latinCfg, scriptRanges);
	}
	if(scriptRanges != nil && eastAsian){
		// A bundled face when the build ships one, else the system's, which
		// every desktop has for its own language.
		std::string scriptFace;
		ImU32 scriptFaceNo = 0;
		char fontRoot[2048];
		if(GetEditorRootDirectory(fontRoot, sizeof(fontRoot))){
			const char *paths[] = { "/fonts/NotoSansSC-Regular.ttf", "/../../../fonts/NotoSansSC-Regular.ttf" };
			for(const char *relative : paths){
				std::string candidate = std::string(fontRoot) + relative;
				FILE *f = fopen(candidate.c_str(), "rb");
				if(f){ fclose(f); scriptFace = candidate; break; }
			}
		}
		if(scriptFace.empty()){
			// A .ttc holds several faces; FontNo picks one from it.
			struct { const char *path; ImU32 face; } faces[] = {
				{ "C:/Windows/Fonts/msyh.ttc", 0 },	// Microsoft YaHei
				{ "C:/Windows/Fonts/simhei.ttf", 0 },	// SimHei
				{ "C:/Windows/Fonts/simsun.ttc", 0 },	// SimSun
				{ "/System/Library/Fonts/PingFang.ttc", 0 },	// macOS
				{ "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0 },	// Debian, Ubuntu
				{ "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", 0 },	// elsewhere
			};
			for(const auto &candidate : faces){
				FILE *f = fopen(candidate.path, "rb");
				if(f){ fclose(f); scriptFace = candidate.path; scriptFaceNo = candidate.face; break; }
			}
		}
		if(!scriptFace.empty()){
			ImFontConfig scriptCfg;
			scriptCfg.MergeMode = true;
			scriptCfg.PixelSnapH = true;
			scriptCfg.FontNo = scriptFaceNo;
			io.Fonts->AddFontFromFileTTF(scriptFace.c_str(), fontSize, &scriptCfg, scriptRanges);
		}
	}

 // A real monospace face, bundled for material text in every release.
 char editorRoot[2048];
 std::string mono;
 if(GetEditorRootDirectory(editorRoot, sizeof(editorRoot))){
  const char *paths[] = { "/fonts/material/LiberationMono-Regular.ttf", "/../../../fonts/material/LiberationMono-Regular.ttf" };
  for(const char *relative : paths){
   std::string candidate = std::string(editorRoot) + relative;
   FILE *f = fopen(candidate.c_str(), "rb");
   if(f){ fclose(f); mono = candidate; break; }
  }
 }
 gCodeFont = mono.empty() ? io.Fonts->AddFontDefault() : io.Fonts->AddFontFromFileTTF(mono.c_str(), fontSize);

	// --- Title font: the splash wordmark. Baked large because the backend
	// cannot render fonts at new sizes, so it is only ever scaled down; only
	// the wordmark's letters, to keep the atlas small ---
	static const ImWchar wordmarkRanges[] = {
		' ', ' ', 'H', 'H', 'S', 'S', 'a', 'a', 'd', 'e', 'i', 'i', 'm', 'm', 'o', 'o',
		't', 'u', 0,
	};
	ImFontConfig titleCfg;
	titleCfg.FontDataOwnedByAtlas = false;
	gTitleFont = io.Fonts->AddFontFromMemoryTTF(
		(void *)inter_font_data, inter_font_size,
		128.0f, &titleCfg, wordmarkRanges);
}
