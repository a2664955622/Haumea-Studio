// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "icons.h"
#include "font_data.h"

ImFont *gTitleFont;	// large Inter for the splash screen title

// Accent color — used for highlights, active elements, selection
static const ImVec4 kAccent       = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);  // soft blue
static const ImVec4 kAccentHover  = ImVec4(0.33f, 0.67f, 1.00f, 1.00f);
static const ImVec4 kAccentActive = ImVec4(0.20f, 0.50f, 0.88f, 1.00f);

void
SetupStyle(void)
{
	ImGuiStyle &style = ImGui::GetStyle();

	// ---- Geometry ----
	style.WindowRounding    = 6.0f;
	style.ChildRounding     = 4.0f;
	style.FrameRounding     = 4.0f;
	style.PopupRounding     = 4.0f;
	style.ScrollbarRounding = 6.0f;
	style.GrabRounding      = 3.0f;
	style.TabRounding       = 4.0f;

	style.WindowPadding     = ImVec2(10, 10);
	style.FramePadding      = ImVec2(6, 4);
	style.ItemSpacing       = ImVec2(8, 5);
	style.ItemInnerSpacing  = ImVec2(6, 4);
	style.ScrollbarSize     = 13.0f;
	style.GrabMinSize       = 10.0f;
	style.WindowBorderSize  = 1.0f;
	style.ChildBorderSize   = 1.0f;
	style.PopupBorderSize   = 1.0f;
	style.FrameBorderSize   = 0.0f;
	style.TabBorderSize     = 0.0f;

	style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);

#ifdef IMGUI_HAS_DOCK
	// Panels dock by dragging their title bar onto the edges of the
	// screen or onto another panel. Needs librw patched with
	// tools/release/patch_librw_haumea.py.
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
	style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);

	// ---- Colors ----
	ImVec4 *c = style.Colors;

	// Backgrounds
	c[ImGuiCol_WindowBg]             = ImVec4(0.11f, 0.11f, 0.13f, 0.95f);
	c[ImGuiCol_ChildBg]              = ImVec4(0.10f, 0.10f, 0.12f, 0.00f);
	c[ImGuiCol_PopupBg]              = ImVec4(0.12f, 0.12f, 0.14f, 0.96f);

	// Borders
	c[ImGuiCol_Border]               = ImVec4(0.22f, 0.22f, 0.26f, 0.60f);
	c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

	// Text
	c[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.94f, 1.00f);
	c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.54f, 1.00f);

	// Frames (input boxes, sliders, checkboxes)
	c[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);
	c[ImGuiCol_FrameBgHovered]       = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
	c[ImGuiCol_FrameBgActive]        = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);

	// Title bar
	c[ImGuiCol_TitleBg]              = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
	c[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
	c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.09f, 0.09f, 0.11f, 0.60f);

	// Menu bar
	c[ImGuiCol_MenuBarBg]            = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);

	// Scrollbar
	c[ImGuiCol_ScrollbarBg]          = ImVec4(0.10f, 0.10f, 0.12f, 0.80f);
	c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
	c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
	c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.42f, 0.42f, 0.48f, 1.00f);

	// Checkbox / Radio mark
	c[ImGuiCol_CheckMark]            = kAccent;

	// Slider
	c[ImGuiCol_SliderGrab]           = ImVec4(0.36f, 0.36f, 0.42f, 1.00f);
	c[ImGuiCol_SliderGrabActive]     = kAccent;

	// Buttons
	c[ImGuiCol_Button]               = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
	c[ImGuiCol_ButtonHovered]        = ImVec4(0.26f, 0.26f, 0.32f, 1.00f);
	c[ImGuiCol_ButtonActive]         = kAccentActive;

	// Headers (collapsing headers, selectable, menu items)
	c[ImGuiCol_Header]               = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
	c[ImGuiCol_HeaderHovered]        = ImVec4(0.24f, 0.24f, 0.30f, 1.00f);
	c[ImGuiCol_HeaderActive]         = kAccentActive;

	// Separators
	c[ImGuiCol_Separator]            = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
	c[ImGuiCol_SeparatorHovered]     = kAccentHover;
	c[ImGuiCol_SeparatorActive]      = kAccent;

	// Resize grip
	c[ImGuiCol_ResizeGrip]           = ImVec4(0.22f, 0.22f, 0.26f, 0.40f);
	c[ImGuiCol_ResizeGripHovered]    = kAccentHover;
	c[ImGuiCol_ResizeGripActive]     = kAccent;

	// Tabs
	c[ImGuiCol_Tab]                  = ImVec4(0.13f, 0.13f, 0.16f, 1.00f);
	c[ImGuiCol_TabHovered]           = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
	c[ImGuiCol_TabSelected]          = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);

	// Table
	c[ImGuiCol_TableHeaderBg]        = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
	c[ImGuiCol_TableBorderStrong]    = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
	c[ImGuiCol_TableBorderLight]     = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
	c[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	c[ImGuiCol_TableRowBgAlt]        = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

	// Selection / Drag-drop
	c[ImGuiCol_TextSelectedBg]       = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
	c[ImGuiCol_DragDropTarget]       = kAccent;

	// Nav highlight
	c[ImGuiCol_NavHighlight]         = kAccent;
	c[ImGuiCol_NavWindowingHighlight]= ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	c[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);

	// Modal dimming
	c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);

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
