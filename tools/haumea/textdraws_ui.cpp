// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "workspace.h"
#include "textdraws.h"
#include "icons.h"
#include "shortcuts.h"
#include "imgui/imgui_internal.h"

#include <ctype.h>
#include <string.h>
#include <algorithm>
#include <map>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

// TextDraw mode: the textdraw file on a 640x448 canvas in place of the world,
// drawn with the game's own font textures (models/fonts.txd) and widths
// (data/fonts.dat). Map editing is off while it is on.

static int gSelected = -1;
static bool gMode;
enum { TD_BACKGROUND_PLAIN, TD_BACKGROUND_STILL };
static int gBackground = TD_BACKGROUND_STILL;
static unsigned int gCanvasColour = 0x3C4450FF;
static bool gStillPending;	// take the still frame after this frame's world
static rw::Texture *gStillFrame;
static std::vector<std::string> gRecent;
static const int MAX_RECENT_TEXTDRAW_FILES = 8;

static void
releaseStillFrame(void)
{
	if(gStillFrame){
		gStillFrame->destroy();
		gStillFrame = nil;
	}
}

bool
InTextDrawMode(void)
{
	return gMode;
}

bool
TextDrawModeHidesWorld(void)
{
	return gMode && !gStillPending;
}

void
EnterTextDrawMode(void)
{
	if(gMode)
		return;
	gMode = true;
	// The world as it is now, behind the canvas.
	releaseStillFrame();
	gStillPending = gBackground == TD_BACKGROUND_STILL;
}

void
ExitTextDrawMode(void)
{
	gMode = false;
	gStillPending = false;
	releaseStillFrame();
}

void
SelectTextDraw(int index)
{
	EnterTextDrawMode();
	gSelected = index;
}

void
TextDrawsAfterWorldRender(void)
{
	if(!gStillPending)
		return;
	gStillPending = false;
	if(Scene.camera == nil || Scene.camera->frameBuffer == nil)
		return;
#ifdef RW_GL3
	glPixelStorei(GL_PACK_ALIGNMENT, 1);	// rows of width*3 bytes, as librw reads them
#endif
	rw::Image *image = Scene.camera->frameBuffer->toImage();
	if(image == nil)
		return;
	rw::Raster *raster = rw::Raster::createFromImage(image);
	image->destroy();
	if(raster == nil)
		return;
	gStillFrame = rw::Texture::create(raster);
	gStillFrame->setFilter(rw::Texture::LINEAR);
}

const std::vector<std::string>&
GetRecentTextDrawFiles(void)
{
	return gRecent;
}

void
AddRecentTextDrawFile(const char *path)
{
	if(path == nil || path[0] == '\0')
		return;
	gRecent.erase(std::remove(gRecent.begin(), gRecent.end(), std::string(path)), gRecent.end());
	gRecent.insert(gRecent.begin(), path);
	if(gRecent.size() > (size_t)MAX_RECENT_TEXTDRAW_FILES)
		gRecent.resize(MAX_RECENT_TEXTDRAW_FILES);
}

bool
LoadTextDrawSetting(const char *key, const char *value)
{
	if(strcmp(key, "textdraw_background") == 0)
		gBackground = atoi(value) == TD_BACKGROUND_PLAIN ? TD_BACKGROUND_PLAIN : TD_BACKGROUND_STILL;
	else if(strcmp(key, "textdraw_canvas_colour") == 0)
		gCanvasColour = (unsigned int)strtoul(value, nil, 16);
	else
		return false;
	return true;
}

void
SaveTextDrawSettings(FILE *f)
{
	fprintf(f, "textdraw_background %d\n", gBackground);
	fprintf(f, "textdraw_canvas_colour %08X\n", gCanvasColour);
}

// ---- Game fonts ----

// font2 holds Beckett (0) above Bank Gothic (2), font1 Aharoni (1) above
// Pricedown (3). fonts.dat's first table is font2's, the second font1's.
static rw::Texture *gFontTex[2];
static int gPropWidths[2][208];
static int gUnpropWidth[2] = { 27, 27 };
static bool gFontsLoaded;

static void
loadFonts(void)
{
	if(gFontsLoaded)
		return;
	gFontsLoaded = true;
	int slot = AddTxdSlot("~td_fonts");
	LoadTxd(slot, "MODELS/FONTS.TXD");
	gFontTex[0] = FindTextureInTxd(slot, "font2");
	gFontTex[1] = FindTextureInTxd(slot, "font1");
	for(int f = 0; f < 2; f++)
		for(int i = 0; i < 208; i++)
			gPropWidths[f][i] = 27;
	FILE *file = fopen(GamePath("data/fonts.dat").c_str(), "r");
	if(file == nil)
		return;
	char line[512];
	int font = -1, count = 0;
	enum { NONE, PROP, UNPROP, OTHER } section = NONE;
	while(fgets(line, sizeof(line), file)){
		char *hash = strchr(line, '#');
		if(hash) *hash = '\0';
		if(strstr(line, "[FONT_ID]")){ section = OTHER; font++; count = 0; continue; }
		if(strstr(line, "[PROP]")){ section = PROP; continue; }
		if(strstr(line, "[UNPROP]")){ section = UNPROP; continue; }
		if(strchr(line, '[')){ section = OTHER; continue; }
		if(font < 0 || font > 1)
			continue;
		char *p = line;
		while(*p){
			while(*p && !isdigit((unsigned char)*p)) p++;
			if(!*p) break;
			int v = (int)strtol(p, &p, 10);
			if(section == PROP && count < 208)
				gPropWidths[font][count++] = v;
			else if(section == UNPROP)
				gUnpropWidth[font] = v;
		}
	}
	fclose(file);
}

// The glyph a character uses: its index in the 16-wide grid.
static int
glyphIndex(int font, unsigned char c)
{
	int i = c - 0x20;
	if(i < 0 || i >= 208)
		return 0;
	if(font == TD_FONT_BANK_GOTHIC || font == TD_FONT_PRICEDOWN){
		// These fonts sit in the lower rows and only have digits and capitals.
		if(i >= 16 && i <= 25) return i + 128;	// 0-9
		if(i == 26) return 154;	// :
		if(i >= 33 && i <= 58) return i + 122;	// A-Z
		if(i >= 65 && i <= 90) return i + 90;	// a-z, drawn as capitals
		return 0;
	}
	return i;
}

static int
fontTexture(int font)
{
	return font == TD_FONT_AHARONI || font == TD_FONT_PRICEDOWN ? 1 : 0;
}

static float
charAdvance(const TextDraw &td, unsigned char c)
{
	int t = fontTexture(td.font);
	int width = td.proportional ? gPropWidths[t][glyphIndex(td.font, c)] : gUnpropWidth[t];
	return (width + td.outline) * td.letterX;
}

// ---- Layout ----

struct Glyph { float x, y; int index; ImU32 colour; };

static ImU32
rgbaToImU32(unsigned int c)
{
	return IM_COL32(c >> 24 & 0xFF, c >> 16 & 0xFF, c >> 8 & 0xFF, c & 0xFF);
}

// ~r~ and friends: the game's text colours.
static bool
styleColour(char code, unsigned int base, unsigned int *out)
{
	unsigned int alpha = base & 0xFF;
	switch(tolower((unsigned char)code)){
	case 'r': *out = 0xB4191D00 | alpha; return true;
	case 'g': *out = 0x36682C00 | alpha; return true;
	case 'b': *out = 0x323C7F00 | alpha; return true;
	case 'y': *out = 0xE2C06300 | alpha; return true;
	case 'p': *out = 0xA86EFC00 | alpha; return true;
	case 'w': *out = 0xE1E1E100 | alpha; return true;
	case 'l': *out = 0x00000000 | alpha; return true;
	}
	return false;
}

static unsigned int
lighten(unsigned int c)
{
	unsigned int out = c & 0xFF;
	for(int shift = 8; shift <= 24; shift += 8){
		unsigned int v = (c >> shift) & 0xFF;
		v = std::min(255u, v + 40u);
		out |= v << shift;
	}
	return out;
}

static float
lineHeight(const TextDraw &td)
{
	return 16.0f * td.letterY + 2.0f * td.outline;
}

// Glyphs in canvas units, and the text's bounds.
static void
layoutText(const TextDraw &td, std::vector<Glyph> &glyphs, float *minX, float *maxX, int *numLines)
{
	glyphs.clear();
	struct Line { std::vector<Glyph> glyphs; float width; };
	std::vector<Line> lines(1);
	lines[0].width = 0.0f;
	float wrap = td.alignment == 2 ? td.sizeY : td.alignment == 3 ? td.x - td.sizeX : td.sizeX - td.x;
	unsigned int colour = td.colour;
	const std::string &s = td.text;
	for(size_t i = 0; i < s.size(); i++){
		char c = s[i];
		if(c == '~'){
			size_t end = s.find('~', i + 1);
			if(end != std::string::npos){
				std::string code = s.substr(i + 1, end - i - 1);
				unsigned int styled;
				if(code == "n"){
					lines.push_back(Line());
					lines.back().width = 0.0f;
				}else if(code == "h")
					colour = lighten(colour);
				else if(code.size() == 1 && styleColour(code[0], td.colour, &styled))
					colour = styled;
				i = end;
				continue;
			}
		}
		if(c == '\n'){
			lines.push_back(Line());
			lines.back().width = 0.0f;
			continue;
		}
		float advance = charAdvance(td, (unsigned char)c);
		Line *line = &lines.back();
		// Word wrap at the text size, like the client.
		if(wrap > 0.0f && line->width + advance > wrap && c != ' ' && !line->glyphs.empty()){
			size_t cut = line->glyphs.size();
			while(cut > 0 && line->glyphs[cut-1].index != 0) cut--;
			Line next;
			next.width = 0.0f;
			if(cut > 0){
				for(size_t g = cut; g < line->glyphs.size(); g++){
					Glyph moved = line->glyphs[g];
					moved.x = next.width;
					next.width += line->glyphs[g].y;	// advance kept in y for now
					next.glyphs.push_back(moved);
				}
				line->glyphs.resize(cut);
				line->width = 0.0f;
				for(size_t g = 0; g < line->glyphs.size(); g++)
					line->width += line->glyphs[g].y;
			}
			lines.push_back(next);
			line = &lines.back();
		}
		Glyph g;
		g.x = line->width;
		g.y = advance;
		// '_' is the usual invisible character in textdraws.
		g.index = c == '_' ? 0 : glyphIndex(td.font, (unsigned char)c);
		g.colour = rgbaToImU32(colour);
		line->glyphs.push_back(g);
		line->width += advance;
	}
	*minX = 1e9f;
	*maxX = -1e9f;
	for(size_t l = 0; l < lines.size(); l++){
		float start = td.alignment == 2 ? td.x - lines[l].width * 0.5f :
			td.alignment == 3 ? td.x - lines[l].width : td.x;
		*minX = std::min(*minX, start);
		*maxX = std::max(*maxX, start + lines[l].width);
		for(size_t g = 0; g < lines[l].glyphs.size(); g++){
			Glyph glyph = lines[l].glyphs[g];
			glyph.x += start;
			glyph.y = td.y + l * lineHeight(td);
			glyphs.push_back(glyph);
		}
	}
	*numLines = (int)lines.size();
}

// The area a textdraw covers on the canvas: its box, sprite or text.
static void
textDrawBounds(const TextDraw &td, float *x0, float *y0, float *x1, float *y1)
{
	if(td.font == TD_FONT_SPRITE || td.font == TD_FONT_PREVIEW){
		*x0 = td.x; *y0 = td.y;
		*x1 = td.x + td.sizeX; *y1 = td.y + td.sizeY;
		return;
	}
	std::vector<Glyph> glyphs;
	float minX, maxX;
	int lines;
	layoutText(td, glyphs, &minX, &maxX, &lines);
	*y0 = td.y;
	*y1 = td.y + lines * lineHeight(td);
	if(td.useBox){
		if(td.alignment == 2){ *x0 = td.x - td.sizeY * 0.5f; *x1 = td.x + td.sizeY * 0.5f; }
		else if(td.alignment == 3){ *x0 = td.sizeX; *x1 = td.x; }
		else{ *x0 = td.x; *x1 = td.sizeX; }
		if(*x1 < *x0) std::swap(*x0, *x1);
		*x0 -= 2.0f; *x1 += 2.0f; *y0 -= 2.0f; *y1 += 2.0f;
	}else{
		*x0 = minX;
		*x1 = std::max(maxX, minX + 2.0f);
	}
}

// ---- Sprites ----

static std::map<std::string, int> gSpriteTxds;	// lower-case name -> slot

static std::string
lowered(const std::string &s)
{
	std::string out = s;
	for(size_t i = 0; i < out.size(); i++)
		out[i] = (char)tolower((unsigned char)out[i]);
	return out;
}

static bool
fileExists(const std::string &path)
{
	FILE *f = fopen(path.c_str(), "rb");
	if(f) fclose(f);
	return f != nil;
}

// The client looks sprite TXDs up in models/txd, then models and SA-MP's folder.
static int
spriteTxdSlot(const std::string &name)
{
	std::string key = lowered(name);
	std::map<std::string, int>::iterator it = gSpriteTxds.find(key);
	if(it != gSpriteTxds.end())
		return it->second;
	int slot = -1;
	static const char *dirs[] = { "models/txd/", "models/", "SAMP/" };
	for(int d = 0; d < 3 && slot < 0; d++){
		std::string path = GamePath((std::string(dirs[d]) + name + ".txd").c_str());
		if(!fileExists(path))
			continue;
		slot = AddTxdSlot(("~td_" + key).c_str());
		LoadTxd(slot, path.c_str());
	}
	gSpriteTxds[key] = slot;
	return slot;
}

static rw::Texture*
spriteTexture(const std::string &text)
{
	size_t colon = text.find(':');
	if(colon == std::string::npos)
		return nil;
	int slot = spriteTxdSlot(text.substr(0, colon));
	return slot >= 0 ? FindTextureInTxd(slot, text.substr(colon + 1).c_str()) : nil;
}

static void
listSpriteTxds(std::vector<std::string> &out)
{
	static std::vector<std::string> cache;
	if(cache.empty()){
		static const char *dirs[] = { "models/txd", "SAMP" };
		for(int d = 0; d < 2; d++){
			std::string dir = GamePath(dirs[d]);
#ifdef _WIN32
			WIN32_FIND_DATAA fd;
			HANDLE h = FindFirstFileA((dir + "/*.txd").c_str(), &fd);
			if(h != INVALID_HANDLE_VALUE){
				do cache.push_back(std::string(fd.cFileName).substr(0, strlen(fd.cFileName) - 4));
				while(FindNextFileA(h, &fd));
				FindClose(h);
			}
#else
			DIR *dp = opendir(dir.c_str());
			if(dp == nil)
				continue;
			struct dirent *e;
			while((e = readdir(dp)) != nil){
				size_t len = strlen(e->d_name);
				if(len > 4 && rw::strcmp_ci(e->d_name + len - 4, ".txd") == 0)
					cache.push_back(std::string(e->d_name, len - 4));
			}
			closedir(dp);
#endif
		}
		std::sort(cache.begin(), cache.end(), [](const std::string &a, const std::string &b){
			return lowered(a) < lowered(b);
		});
	}
	out = cache;
}

// ---- Drawing ----

struct Canvas {
	float x, y, sx, sy;	// screen origin and units per canvas unit
	float areaY;	// the top of the free area, above the canvas when it is letterboxed
	ImVec2 at(float cx, float cy) const { return ImVec2(x + cx * sx, y + cy * sy); }
};

// The game screen is the 3D view's free area between the docked panels,
// at the window's shape, as a player's screen would be.
static Canvas
getCanvas(void)
{
	ImGuiViewport *vp = ImGui::GetMainViewport();
	ImVec2 pos = vp->WorkPos, size = vp->WorkSize;
#ifdef IMGUI_HAS_DOCK
	ImGuiDockNode *central = ImGui::DockBuilderGetCentralNode(ImHashStr("HaumeaDockSpace"));
	if(central && central->Size.x > 32.0f && central->Size.y > 32.0f){
		pos = central->Pos;
		size = central->Size;
	}
#endif
	float aspect = vp->Size.x / std::max(1.0f, vp->Size.y);
	float w = size.x, h = size.x / aspect;
	if(h > size.y){
		h = size.y;
		w = h * aspect;
	}
	Canvas c;
	c.x = pos.x + (size.x - w) * 0.5f;
	c.y = pos.y + (size.y - h) * 0.5f;
	c.sx = w / 640.0f;
	c.sy = h / 448.0f;
	c.areaY = pos.y;
	return c;
}

// ---- Model previews ----

struct PreviewEntry {
	int model;
	float rx, ry, rz, zoom;
	int colour1, colour2;
	unsigned int background;
	rw::Raster *color, *depth;
	rw::Texture *texture;
	bool ready;
	int lastFrame;
};
static PreviewEntry gPreviews[16];
static const int PREVIEW_SIZE = 256;

static bool
previewMatches(const PreviewEntry &e, const TextDraw &td)
{
	return e.texture && e.model == td.previewModel && e.rx == td.previewRotX && e.ry == td.previewRotY &&
		e.rz == td.previewRotZ && e.zoom == td.previewZoom && e.colour1 == td.previewColour1 &&
		e.colour2 == td.previewColour2 && e.background == td.backgroundColour;
}

// The texture of a preview, or nil until it has been rendered.
static rw::Texture*
previewTexture(const TextDraw &td)
{
	int frame = ImGui::GetFrameCount();
	for(int i = 0; i < 16; i++)
		if(previewMatches(gPreviews[i], td)){
			gPreviews[i].lastFrame = frame;
			return gPreviews[i].ready ? gPreviews[i].texture : nil;
		}
	// Take the least recently used entry and render it next frame.
	PreviewEntry *e = &gPreviews[0];
	for(int i = 1; i < 16; i++)
		if(gPreviews[i].lastFrame < e->lastFrame)
			e = &gPreviews[i];
	if(e->texture == nil){
		e->color = rw::Raster::create(PREVIEW_SIZE, PREVIEW_SIZE, 32, rw::Raster::C8888|rw::Raster::CAMERATEXTURE);
		e->depth = rw::Raster::create(PREVIEW_SIZE, PREVIEW_SIZE, 0, rw::Raster::ZBUFFER);
		if(e->color == nil || e->depth == nil)
			return nil;
		e->texture = rw::Texture::create(e->color);
		e->texture->setFilter(rw::Texture::LINEAR);
	}
	e->model = td.previewModel;
	e->rx = td.previewRotX; e->ry = td.previewRotY; e->rz = td.previewRotZ;
	e->zoom = td.previewZoom;
	e->colour1 = td.previewColour1; e->colour2 = td.previewColour2;
	e->background = td.backgroundColour;
	e->ready = false;
	e->lastFrame = frame;
	return nil;
}

void
TextDrawsBeforeWorldRender(void)
{
	for(int i = 0; i < 16; i++){
		PreviewEntry &e = gPreviews[i];
		if(e.texture == nil || e.ready)
			continue;
		rw::RGBA bg = { (uint8)(e.background >> 24), (uint8)(e.background >> 16),
			(uint8)(e.background >> 8), (uint8)e.background };
		rw::V3d rot = { e.rx, e.ry, e.rz };
		e.ready = RenderModelPreviewToRaster(e.model, rot, e.zoom, e.colour1, e.colour2, bg, e.color, e.depth);
		if(!e.ready)
			e.model = -999999;	// don't try again every frame
		return;	// one per frame
	}
}

static void
drawGlyphs(ImDrawList *dl, const Canvas &cv, const TextDraw &td, const std::vector<Glyph> &glyphs,
	float dx, float dy, ImU32 overrideColour, bool useOverride)
{
	rw::Texture *tex = gFontTex[fontTexture(td.font)];
	if(tex == nil)
		return;
	float w = 32.0f * td.letterX, h = 20.0f * td.letterY;
	for(size_t i = 0; i < glyphs.size(); i++){
		const Glyph &g = glyphs[i];
		if(g.index == 0)
			continue;
		float u = (g.index % 16) / 16.0f, v = (g.index / 16) / 12.8f;
		dl->AddImage((ImTextureID)tex, cv.at(g.x + dx, g.y + dy), cv.at(g.x + dx + w, g.y + dy + h),
			ImVec2(u, v), ImVec2(u + 1.0f / 16.0f, v + 1.0f / 12.8f), useOverride ? overrideColour : g.colour);
	}
}

static void
drawTextDraw(ImDrawList *dl, const Canvas &cv, const TextDraw &td)
{
	float x0, y0, x1, y1;
	textDrawBounds(td, &x0, &y0, &x1, &y1);
	if(td.font == TD_FONT_SPRITE){
		rw::Texture *tex = spriteTexture(td.text);
		if(tex)
			dl->AddImage((ImTextureID)tex, cv.at(x0, y0), cv.at(x1, y1), ImVec2(0, 0), ImVec2(1, 1),
				rgbaToImU32(td.colour));
		else
			dl->AddRect(cv.at(x0, y0), cv.at(x1, y1), IM_COL32(255, 80, 80, 200));
		return;
	}
	if(td.font == TD_FONT_PREVIEW){
		// Rendered with its own background, rotation and zoom.
		rw::Texture *tex = previewTexture(td);
		if(tex)
			dl->AddImage((ImTextureID)tex, cv.at(x0, y0), cv.at(x1, y1), ImVec2(0, 1), ImVec2(1, 0));
		else
			dl->AddRectFilled(cv.at(x0, y0), cv.at(x1, y1), rgbaToImU32(td.backgroundColour));
		return;
	}
	if(td.useBox)
		dl->AddRectFilled(cv.at(x0, y0), cv.at(x1, y1), rgbaToImU32(td.boxColour));
	std::vector<Glyph> glyphs;
	float minX, maxX;
	int lines;
	layoutText(td, glyphs, &minX, &maxX, &lines);
	ImU32 back = rgbaToImU32(td.backgroundColour);
	if(td.outline > 0){
		float o = (float)td.outline;
		for(int oy = -1; oy <= 1; oy++)
			for(int ox = -1; ox <= 1; ox++)
				if(ox || oy)
					drawGlyphs(dl, cv, td, glyphs, ox * o, oy * o, back, true);
	}else if(td.shadow > 0)
		drawGlyphs(dl, cv, td, glyphs, (float)td.shadow, (float)td.shadow, back, true);
	drawGlyphs(dl, cv, td, glyphs, 0.0f, 0.0f, 0, false);
}

// ---- On-screen editing ----

static void
selectTextDraw(int index)
{
	gSelected = index;
}

// The canvas: the still frame or a plain colour where the world was, the
// map's read-only textdraws, then the file's, which can be dragged.
void
DrawTextDrawsOverlay(void)
{
	if(!gMode)
		return;
	std::vector<TextDraw> &tds = GetTextDraws();
	loadFonts();
	Canvas cv = getCanvas();
	ImDrawList *dl = ImGui::GetBackgroundDrawList();
	ImGuiViewport *vp = ImGui::GetMainViewport();
	dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(24, 26, 30, 255));
	if(gBackground == TD_BACKGROUND_STILL && gStillFrame)
		dl->AddImage((ImTextureID)gStillFrame, cv.at(0, 0), cv.at(640, 448));
	else
		dl->AddRectFilled(cv.at(0, 0), cv.at(640, 448), rgbaToImU32(gCanvasColour));
	const std::vector<TextDraw> &mapTds = GetMapTextDraws();
	for(size_t i = 0; i < mapTds.size(); i++){
		drawTextDraw(dl, cv, mapTds[i]);
		float x0, y0, x1, y1;
		textDrawBounds(mapTds[i], &x0, &y0, &x1, &y1);
		dl->AddRect(cv.at(x0, y0), cv.at(x1, y1), IM_COL32(160, 160, 170, 120));
	}
	for(size_t i = 0; i < tds.size(); i++)
		drawTextDraw(dl, cv, tds[i]);
	if(gSelected >= (int)tds.size())
		gSelected = -1;
	// The screen's edge, so textdraws can be placed against it.
	dl->AddRect(cv.at(0, 0), cv.at(640, 448), IM_COL32(255, 255, 255, 60));

	// Invisible windows over the textdraws take the clicks and drags.
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
	static int dragging = -1;
	static bool resizing;
	for(int i = (int)tds.size() - 1; i >= 0; i--){
		TextDraw &td = tds[i];
		float x0, y0, x1, y1;
		textDrawBounds(td, &x0, &y0, &x1, &y1);
		ImVec2 p0 = cv.at(x0, y0), p1 = cv.at(x1, y1);
		if(p1.x - p0.x < 6.0f) p1.x = p0.x + 6.0f;
		if(p1.y - p0.y < 6.0f) p1.y = p0.y + 6.0f;
		char id[32];
		snprintf(id, sizeof(id), "##tdhit%d", i);
		ImGui::SetNextWindowPos(p0);
		ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
		ImGui::Begin(id, nil, flags);
		ImGui::InvisibleButton("hit", ImVec2(p1.x - p0.x, p1.y - p0.y));
		if(ImGui::IsItemActivated()){
			selectTextDraw(i);
			dragging = i;
			resizing = false;
		}
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", td.name[0] ? td.name : "textdraw");
		if(dragging == i && !resizing && ImGui::IsItemActive()){
			ImVec2 d = ImGui::GetIO().MouseDelta;
			if(d.x != 0.0f || d.y != 0.0f){
				td.x += d.x / cv.sx;
				td.y += d.y / cv.sy;
				if(td.useBox && td.alignment == 1) td.sizeX += d.x / cv.sx;
				if(td.useBox && td.alignment == 3) td.sizeX += d.x / cv.sx;
				TextDrawsChanged();
			}
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
		if(i == gSelected)
			dl->AddRect(p0, p1, IM_COL32(255, 220, 60, 255), 0.0f, 0, 1.5f);
	}

	// A handle at the selected one's corner resizes it: letters for text,
	// the box's edge for a box, the size for sprites and models.
	if(gSelected >= 0){
		TextDraw &td = tds[gSelected];
		float x0, y0, x1, y1;
		textDrawBounds(td, &x0, &y0, &x1, &y1);
		ImVec2 corner = cv.at(x1, y1);
		float hs = 7.0f;
		ImGui::SetNextWindowPos(ImVec2(corner.x - hs, corner.y - hs));
		ImGui::SetNextWindowSize(ImVec2(hs * 2, hs * 2));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
		ImGui::Begin("##tdresize", nil, flags & ~ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImGui::InvisibleButton("resize", ImVec2(hs * 2, hs * 2));
		if(ImGui::IsItemActivated()){
			dragging = gSelected;
			resizing = true;
		}
		if(ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
		if(resizing && ImGui::IsItemActive()){
			ImVec2 d = ImGui::GetIO().MouseDelta;
			float dx = d.x / cv.sx, dy = d.y / cv.sy;
			if(dx != 0.0f || dy != 0.0f){
				if(td.font == TD_FONT_SPRITE || td.font == TD_FONT_PREVIEW){
					td.sizeX = std::max(1.0f, td.sizeX + dx);
					td.sizeY = std::max(1.0f, td.sizeY + dy);
				}else if(td.useBox){
					if(td.alignment == 2) td.sizeY = std::max(1.0f, td.sizeY + dx * 2.0f);
					else td.sizeX += dx;
					int lines = std::max(1, (int)((y1 - y0) / std::max(0.01f, lineHeight(td))));
					td.letterY = std::max(0.05f, td.letterY + dy / (16.0f * lines));
				}else{
					float width = std::max(1.0f, x1 - x0);
					td.letterX = std::max(0.05f, td.letterX * (width + dx) / width);
					td.letterY = std::max(0.05f, td.letterY + dy / 20.0f);
				}
				TextDrawsChanged();
			}
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
		dl->AddRectFilled(ImVec2(corner.x - 4, corner.y - 4), ImVec2(corner.x + 4, corner.y + 4),
			IM_COL32(255, 220, 60, 255));
	}
	if(!ImGui::IsMouseDown(0))
		dragging = -1;
}

// ---- Window ----

static bool
colourEdit(const char *label, unsigned int *rgba)
{
	float c[4] = { (*rgba >> 24 & 0xFF) / 255.0f, (*rgba >> 16 & 0xFF) / 255.0f,
		(*rgba >> 8 & 0xFF) / 255.0f, (*rgba & 0xFF) / 255.0f };
	if(!ImGui::ColorEdit4(label, c, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
		return false;
	*rgba = (unsigned int)(c[0] * 255.0f + 0.5f) << 24 | (unsigned int)(c[1] * 255.0f + 0.5f) << 16 |
		(unsigned int)(c[2] * 255.0f + 0.5f) << 8 | (unsigned int)(c[3] * 255.0f + 0.5f);
	return true;
}

static void
addTextDraw(int font)
{
	TextDraw td;
	InitTextDraw(&td);
	td.x = 280.0f;
	td.y = 200.0f;
	td.font = font;
	if(font == TD_FONT_SPRITE){
		td.text = "LD_BEAT:chit";
		td.sizeX = td.sizeY = 32.0f;
		td.colour = 0xFFFFFFFF;
	}else if(font == TD_FONT_PREVIEW){
		td.text = "_";
		td.sizeX = td.sizeY = 80.0f;
		td.previewModel = 411;
		td.backgroundColour = 0x00000080;
	}else if(font < 0){
		// A box: an empty text with a background.
		td.font = TD_FONT_AHARONI;
		td.text = "_";
		td.useBox = true;
		td.sizeX = td.x + 120.0f;
		td.letterY = 4.0f;
		td.boxColour = 0x00000099;
	}else
		td.text = "Text";
	GetTextDraws().push_back(td);
	gSelected = (int)GetTextDraws().size() - 1;
	TextDrawsChanged();
}

static void
spritePicker(TextDraw &td)
{
	static std::string txdName;
	if(txdName.empty()){
		size_t colon = td.text.find(':');
		txdName = colon == std::string::npos ? "LD_BEAT" : td.text.substr(0, colon);
	}
	std::vector<std::string> txds;
	listSpriteTxds(txds);
	if(ImGui::BeginCombo("Sprite TXD", txdName.c_str(), ImGuiComboFlags_HeightLarge)){
		for(size_t i = 0; i < txds.size(); i++)
			if(ImGui::Selectable(txds[i].c_str(), rw::strcmp_ci(txds[i].c_str(), txdName.c_str()) == 0))
				txdName = txds[i];
		ImGui::EndCombo();
	}
	int slot = spriteTxdSlot(txdName);
	int count = slot >= 0 ? GetTxdTextureCount(slot) : 0;
	ImGui::BeginChild("sprites", ImVec2(0, ImGui::GetFontSize() * 9.0f), ImGuiChildFlags_Borders);
	float cell = ImGui::GetFontSize() * 3.0f;
	int perRow = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cell + 4.0f)));
	for(int i = 0; i < count; i++){
		rw::Texture *tex = GetTxdTexture(slot, i);
		if(tex == nil)
			continue;
		if(i % perRow)
			ImGui::SameLine();
		ImGui::PushID(i);
		if(ImGui::ImageButton("##s", (ImTextureID)tex, ImVec2(cell, cell))){
			td.text = txdName + ":" + tex->name;
			TextDrawsChanged();
		}
		ImGui::SetItemTooltip("%s:%s", txdName.c_str(), tex->name);
		ImGui::PopID();
	}
	if(count == 0)
		ImGui::TextDisabled("No textures in %s.", txdName.c_str());
	ImGui::EndChild();
}

static void
properties(TextDraw &td)
{
	// What it is, what it shows, then where; the style is closed by default.
	bool changed = false;
	ImGui::InputText("Variable", td.name, sizeof(td.name));
	if(ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::SetItemTooltip("The Pawn variable. Empty or not a plain name makes one up.");
	int scope = td.perPlayer ? 1 : 0;
	if(ImGui::Combo("Shown", &scope, "To everyone (TextDrawCreate)\0Per player (CreatePlayerTextDraw)\0")){
		td.perPlayer = scope == 1;
		changed = true;
	}
	static const char *fonts[] = { "0 Beckett (gothic)", "1 Aharoni", "2 Bank Gothic (capitals)",
		"3 Pricedown (capitals)", "4 Sprite", "5 Model preview" };
	changed |= ImGui::Combo("Font", &td.font, fonts, IM_ARRAYSIZE(fonts));
	bool isText = td.font != TD_FONT_SPRITE && td.font != TD_FONT_PREVIEW;
	if(td.font == TD_FONT_SPRITE){
		char buf[256];
		snprintf(buf, sizeof(buf), "%s", td.text.c_str());
		if(ImGui::InputText("Sprite", buf, sizeof(buf))){
			td.text = buf;
			changed = true;
		}
		spritePicker(td);
	}else if(isText){
		char buf[1024];
		snprintf(buf, sizeof(buf), "%s", td.text.c_str());
		if(ImGui::InputTextMultiline("Text", buf, sizeof(buf), ImVec2(-FLT_MIN, ImGui::GetFontSize() * 3.5f))){
			td.text = buf;
			changed = true;
		}
		ImGui::SetItemTooltip("~n~ new line, ~r~ ~g~ ~b~ ~y~ ~p~ ~w~ ~l~ colours, ~h~ lighter, _ blank");
	}else{
		changed |= ImGui::InputInt("Model", &td.previewModel);
		float rot[3] = { td.previewRotX, td.previewRotY, td.previewRotZ };
		if(ImGui::DragFloat3("Rotation", rot, 1.0f, -360.0f, 360.0f, "%.1f")){
			td.previewRotX = rot[0]; td.previewRotY = rot[1]; td.previewRotZ = rot[2];
			changed = true;
		}
		changed |= ImGui::DragFloat("Zoom", &td.previewZoom, 0.01f, 0.1f, 10.0f, "%.2f");
		int cols[2] = { td.previewColour1, td.previewColour2 };
		if(ImGui::InputInt2("Vehicle colours", cols)){
			td.previewColour1 = cols[0]; td.previewColour2 = cols[1];
			changed = true;
		}
	}
	changed |= colourEdit(td.font == TD_FONT_SPRITE ? "Tint" : "Colour", &td.colour);

	ImGui::SeparatorText("Position");
	float pos[2] = { td.x, td.y };
	if(ImGui::DragFloat2("Position", pos, 0.5f, -100.0f, 740.0f, "%.2f")){
		td.x = pos[0]; td.y = pos[1];
		changed = true;
	}
	ImGui::SetItemTooltip("On the 640x448 canvas every resolution is scaled to.");
	float size[2] = { td.sizeX, td.sizeY };
	if(ImGui::DragFloat2("Text size", size, 0.5f, -1280.0f, 1280.0f, "%.2f")){
		td.sizeX = size[0]; td.sizeY = size[1];
		changed = true;
	}
	ImGui::SetItemTooltip(td.font >= TD_FONT_SPRITE ? "Width and height." :
		"Left: the box's right edge. Centre: width and height are swapped (the second is the width).\n"
		"Right: the box's left edge. Text wraps there.");

	if(ImGui::CollapsingHeader("Style")){
		if(isText){
			float letter[2] = { td.letterX, td.letterY };
			if(ImGui::DragFloat2("Letter size", letter, 0.005f, 0.0f, 10.0f, "%.3f")){
				td.letterX = letter[0]; td.letterY = letter[1];
				changed = true;
			}
			int align = td.alignment - 1;
			if(ImGui::Combo("Alignment", &align, "Left\0Centre\0Right\0")){
				td.alignment = align + 1;
				changed = true;
			}
			changed |= ImGui::Checkbox("Proportional", &td.proportional);
			ImGui::SetItemTooltip("Letters take their own width instead of a fixed one.");
			ImGui::SameLine();
			changed |= ImGui::Checkbox("Box", &td.useBox);
			if(td.useBox)
				changed |= colourEdit("Box colour", &td.boxColour);
			changed |= ImGui::SliderInt("Shadow", &td.shadow, 0, 10);
			changed |= ImGui::SliderInt("Outline", &td.outline, 0, 10);
		}
		changed |= colourEdit(td.font == TD_FONT_PREVIEW ? "Background" : "Shadow/outline colour", &td.backgroundColour);
		changed |= ImGui::Checkbox("Selectable", &td.selectable);
		ImGui::SetItemTooltip("Players can click it while SelectTextDraw is on (OnPlayerClickTextDraw).");
	}
	if(changed)
		TextDrawsChanged();
}

// ---- The file ----

static void (*gAfterClose)(void);	// runs once unsaved textdraws are dealt with
static bool gOpenUnsavedPrompt;
static bool gShowFileDialog, gFileDialogSave, gFileDialogFocus;
static char gFileDialogPath[1024];
static bool gOpenMovePrompt;
static char gMovePath[1024];

static void
afterFileChange(const char *verb)
{
	gSelected = -1;
	AddRecentTextDrawFile(GetTextDrawFilePath());
	SaveEditorSettings();
	if(verb)
		Toast(TOAST_SAVE, "%s %s", verb, GetTextDrawFileName());
}

static void
openFile(const char *path)
{
	std::string error;
	if(!OpenTextDrawFile(path, &error)){
		ToastFor(TOAST_SAVE, 5.0f, "Could not open %s", path);
		gRecent.erase(std::remove(gRecent.begin(), gRecent.end(), std::string(path)), gRecent.end());
		return;
	}
	afterFileChange("Opened");
}

static bool
saveFile(const char *path)
{
	std::string error;
	if(!SaveTextDrawFile(path, &error)){
		ToastFor(TOAST_SAVE, 5.0f, "Could not save the textdraws: %s", error.c_str());
		return false;
	}
	afterFileChange("Saved");
	if(gAfterClose){
		void (*then)(void) = gAfterClose;
		gAfterClose = nil;
		then();
	}
	return true;
}

// The file's folder, else the map's, else the workspace's gamemodes.
static std::string
suggestedPath(void)
{
	if(GetTextDrawFilePath()[0])
		return GetTextDrawFilePath();
	if(GetPawnMapPath()[0])
		return SuggestedTextDrawFilePath(GetPawnMapPath());
	std::string dir = WorkspaceDefaultDir("gamemodes");
	return (dir.empty() ? std::string(".") : dir) + "/textdraws.pwn";
}

static void
showFileDialog(bool save)
{
	gShowFileDialog = true;
	gFileDialogSave = save;
	gFileDialogFocus = true;
	snprintf(gFileDialogPath, sizeof(gFileDialogPath), "%s", save ? suggestedPath().c_str() : "");
}

static void showOpenDialog(void) { showFileDialog(false); }

static void
newFile(void)
{
	NewTextDrawFile();
	afterFileChange(nil);
	Toast(TOAST_SAVE, "New textdraws");
}

static void
saveOrAsk(void)
{
	if(GetTextDrawFilePath()[0])
		saveFile(nil);
	else
		showFileDialog(true);
}

void
RequestTextDrawFileClose(void (*then)(void))
{
	if(!TextDrawFileHasUnsavedChanges()){
		if(then)
			then();
		return;
	}
	gAfterClose = then;
	gOpenUnsavedPrompt = true;
}

void
OfferMoveMapTextDraws(const char *mapPath)
{
	if(GetMapTextDraws().empty())
		return;
	snprintf(gMovePath, sizeof(gMovePath), "%s", SuggestedTextDrawFilePath(mapPath).c_str());
	gOpenMovePrompt = true;
}

static void
moveMapTextDraws(void)
{
	std::string error;
	int moved = MoveMapTextDrawsToFile(GetTextDrawFilePath()[0] ? nil : gMovePath, &error);
	if(moved < 0){
		ToastFor(TOAST_SAVE, 6.0f, "Could not move the textdraws: %s", error.c_str());
		return;
	}
	afterFileChange(nil);
	ToastFor(TOAST_SAVE, 6.0f, "Moved %d textdraw(s) to %s. Save the map to take them out of it.",
		moved, GetTextDrawFileName());
}

static void
drawPopups(void)
{
	if(gOpenUnsavedPrompt){
		ImGui::OpenPopup("Unsaved textdraws");
		gOpenUnsavedPrompt = false;
	}
	if(ImGui::BeginPopupModal("Unsaved textdraws", nil, ImGuiWindowFlags_AlwaysAutoResize)){
		ImGui::Text("Save changes to %s?", GetTextDrawFileName());
		ImGui::TextDisabled("Your textdraw changes are lost if you don't save them.");
		ImGui::Spacing();
		if(ImGui::Button("Save", ImVec2(110, 0))){
			ImGui::CloseCurrentPopup();
			saveOrAsk();	// continues once the file is written
		}
		ImGui::SameLine();
		if(ImGui::Button("Don't Save", ImVec2(110, 0))){
			ImGui::CloseCurrentPopup();
			void (*then)(void) = gAfterClose;
			gAfterClose = nil;
			if(then)
				then();
		}
		ImGui::SameLine();
		if(ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)){
			gAfterClose = nil;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if(gOpenMovePrompt){
		ImGui::OpenPopup("Move textdraws");
		gOpenMovePrompt = false;
	}
	if(ImGui::BeginPopupModal("Move textdraws", nil, ImGuiWindowFlags_AlwaysAutoResize)){
		int count = (int)GetMapTextDraws().size();
		ImGui::Text("This map has %d textdraw%s.", count, count == 1 ? "" : "s");
		ImGui::TextDisabled("Textdraws now have their own file, which Test in game includes.\n"
			"Left in the map they are read-only.");
		ImGui::Spacing();
		if(GetTextDrawFilePath()[0])
			ImGui::Text("They go into %s.", GetTextDrawFileName());
		else{
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f);
			ImGui::InputText("File", gMovePath, sizeof(gMovePath));
		}
		ImGui::Spacing();
		if(ImGui::Button("Move textdraws to a separate file")){
			ImGui::CloseCurrentPopup();
			moveMapTextDraws();
		}
		ImGui::SameLine();
		if(ImGui::Button("Keep them read-only") || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::SetItemTooltip("They stay in the map and are saved with it. TextDraws can move them later.");
		ImGui::EndPopup();
	}

	if(gShowFileDialog){
		const char *title = gFileDialogSave ? "Save TextDraws As###tdfile" : "Open TextDraws###tdfile";
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if(gFileDialogFocus){
			ImGui::SetNextWindowFocus();
			gFileDialogFocus = false;
		}
		if(ImGui::Begin(title, &gShowFileDialog, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoDocking)){
			ImGui::TextDisabled("A .pwn or .inc file with only textdraws; your gamemode includes it.");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f);
			ImGui::InputText("Path", gFileDialogPath, sizeof(gFileDialogPath));
			ImGui::SameLine();
			if(ImGui::Button("Browse...")){
				char picked[1024];
				if(PickPawnFile(picked, sizeof(picked), gFileDialogSave, gFileDialogPath[0] ? gFileDialogPath : nil))
					snprintf(gFileDialogPath, sizeof(gFileDialogPath), "%s", picked);
			}
			if(ImGui::Button(gFileDialogSave ? "Save" : "Open", ImVec2(110, 0)) && gFileDialogPath[0]){
				gShowFileDialog = false;
				if(gFileDialogSave)
					saveFile(gFileDialogPath);
				else
					openFile(gFileDialogPath);
			}
			ImGui::SameLine();
			if(ImGui::Button("Cancel", ImVec2(110, 0))){
				gShowFileDialog = false;
				gAfterClose = nil;
			}
		}
		ImGui::End();
	}
}

// ---- Mode ----

static void
drawBanner(void)
{
	Canvas cv = getCanvas();
	KeepNextEditorOverlayOnMainWindow();
	// In the band above the canvas when there is one, so it covers less of it.
	ImGui::SetNextWindowPos(ImVec2(cv.at(320, 0).x, cv.areaY + ImGui::GetFontSize() * 0.5f), ImGuiCond_Always,
		ImVec2(0.5f, 0.0f));
	ImGui::SetNextWindowBgAlpha(0.85f);
	if(ImGui::Begin("##textdraw_mode_banner", nil, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoDocking)){
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), ICON_FA_FONT " TextDraw mode");
		ImGui::SameLine();
		ImGui::Text("%s%s", GetTextDrawFileName(), TextDrawFileHasUnsavedChanges() ? " *" : "");
		ImGui::SameLine();
		ImGui::TextDisabled("Map editing is off.");
		ImGui::SameLine();
		if(ImGui::Button(ICON_FA_XMARK " Exit"))
			ExitTextDrawMode();
		ImGui::SetItemTooltip("Back to the map (%s)", ShortcutLabel(SC_CANCEL));
	}
	ImGui::End();
}

bool
TextDrawModeShortcut(int action)
{
	if(!gMode)
		return false;
	std::vector<TextDraw> &tds = GetTextDraws();
	bool selected = gSelected >= 0 && gSelected < (int)tds.size();
	switch(action){
	case SC_UNDO:
		if(UndoTextDraws())
			Toast(TOAST_UNDO_REDO, "Undo");
		return true;
	case SC_REDO:
		if(RedoTextDraws())
			Toast(TOAST_UNDO_REDO, "Redo");
		return true;
	case SC_NEW_MAP: RequestTextDrawFileClose(newFile); return true;
	case SC_OPEN_MAP: RequestTextDrawFileClose(showOpenDialog); return true;
	case SC_SAVE_MAP: saveOrAsk(); return true;
	case SC_SAVE_MAP_AS: showFileDialog(true); return true;
	case SC_CANCEL: ExitTextDrawMode(); return true;
	case SC_DELETE:
		if(selected){
			tds.erase(tds.begin() + gSelected);
			gSelected = -1;
			TextDrawsChanged();
		}
		return true;
	case SC_DUPLICATE:
		if(selected){
			TextDraw copy = tds[gSelected];
			copy.name[0] = '\0';
			copy.x += 10.0f; copy.y += 10.0f;
			tds.push_back(copy);
			gSelected = (int)tds.size() - 1;
			TextDrawsChanged();
		}
		return true;
	// The rest works as usual: testing, windows, the interface size.
	case SC_TEST_IN_GAME:
	case SC_UI_SCALE_UP: case SC_UI_SCALE_DOWN: case SC_UI_SCALE_RESET:
	case SC_SCROLL_UP: case SC_SCROLL_DOWN:
		return false;
	}
	return action < SC_WINDOW_BROWSER;	// map editing is off
}

static void
fileBar(void)
{
	if(ImGui::Button(ICON_FA_FILE " New"))
		RequestTextDrawFileClose(newFile);
	ImGui::SameLine();
	if(ImGui::Button(ICON_FA_FOLDER_OPEN " Open..."))
		RequestTextDrawFileClose(showOpenDialog);
	ImGui::SameLine();
	if(ImGui::Button(ICON_FA_FLOPPY_DISK " Save"))
		saveOrAsk();
	ImGui::SetItemTooltip("%s", ShortcutLabel(SC_SAVE_MAP));
	ImGui::SameLine();
	if(ImGui::Button("Save As..."))
		showFileDialog(true);
	ImGui::SameLine();
	ImGui::BeginDisabled(gRecent.empty());
	if(ImGui::Button(ICON_FA_CLOCK_ROTATE_LEFT "##recent"))
		ImGui::OpenPopup("recent");
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Recent textdraw files");
	if(ImGui::BeginPopup("recent")){
		static std::string chosen;
		for(size_t i = 0; i < gRecent.size(); i++){
			ImGui::PushID((int)i);
			const char *name = gRecent[i].c_str() + gRecent[i].find_last_of("/\\") + 1;
			if(ImGui::MenuItem(name)){
				chosen = gRecent[i];
				RequestTextDrawFileClose([]{ openFile(chosen.c_str()); });
			}
			ImGui::SetItemTooltip("%s", gRecent[i].c_str());
			ImGui::PopID();
		}
		ImGui::Separator();
		if(ImGui::MenuItem("Clear list")){
			gRecent.clear();
			SaveEditorSettings();
		}
		ImGui::EndPopup();
	}
	ImGui::TextDisabled("%s%s", TextDrawFileHasUnsavedChanges() ? "Unsaved changes - " : "Saved - ",
		GetTextDrawFileName());
	if(GetTextDrawFilePath()[0])
		ImGui::SetItemTooltip("%s\nInclude it in your gamemode and call %s_CreateTextDraws()\n"
			"from OnGameModeInit and %s_ShowTextDraws(playerid).", GetTextDrawFilePath(),
			TextDrawFilePrefix(GetTextDrawFilePath()).c_str(), TextDrawFilePrefix(GetTextDrawFilePath()).c_str());
}

void
DrawTextDrawsWindow(void)
{
	drawPopups();
	if(!gMode)
		return;
	UpdateTextDrawsUndo();
	drawBanner();
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 24.0f, ImGui::GetFontSize() * 34.0f), ImGuiCond_FirstUseEver);
	bool open = true;
	if(!ImGui::Begin(ICON_FA_FONT " TextDraws###textdraws", &open)){
		ImGui::End();
		if(!open)
			ExitTextDrawMode();
		return;
	}
	std::vector<TextDraw> &tds = GetTextDraws();
	fileBar();

	int background = gBackground;
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
	if(ImGui::Combo("Background", &background, "Plain\0Still frame\0")){
		gBackground = background;
		if(gBackground == TD_BACKGROUND_STILL && gStillFrame == nil){
			// Taken from the world on the next frame.
			gStillPending = true;
		}
		SaveEditorSettings();
	}
	ImGui::SetItemTooltip("Still frame: the world as the camera saw it when TextDraw mode started.");
	if(gBackground == TD_BACKGROUND_PLAIN){
		ImGui::SameLine();
		if(colourEdit("##canvas", &gCanvasColour))
			SaveEditorSettings();
	}

	const std::vector<TextDraw> &mapTds = GetMapTextDraws();
	if(!mapTds.empty()){
		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " %d in the map (read-only)", (int)mapTds.size());
		ImGui::SetItemTooltip("From a map made before textdraws had their own file. They are saved\n"
			"back with the map as they are; move them here to edit them.");
		ImGui::SameLine();
		if(ImGui::SmallButton("Move to a separate file..."))
			OfferMoveMapTextDraws(GetPawnMapPath());
	}
	ImGui::Separator();

	if(ImGui::Button(ICON_FA_PLUS " Text")) addTextDraw(TD_FONT_AHARONI);
	ImGui::SameLine();
	if(ImGui::Button(ICON_FA_PLUS " Box")) addTextDraw(-1);
	ImGui::SameLine();
	if(ImGui::Button(ICON_FA_PLUS " Sprite")) addTextDraw(TD_FONT_SPRITE);
	ImGui::SameLine();
	if(ImGui::Button(ICON_FA_PLUS " Model")) addTextDraw(TD_FONT_PREVIEW);

	ImGui::BeginChild("list", ImVec2(0, ImGui::GetFontSize() * 8.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
	for(size_t i = 0; i < tds.size(); i++){
		const TextDraw &td = tds[i];
		char label[160];
		const char *kind = td.font == TD_FONT_SPRITE ? "sprite" : td.font == TD_FONT_PREVIEW ? "model" :
			td.useBox ? "box" : "text";
		std::string preview = td.text.substr(0, 28);
		std::replace(preview.begin(), preview.end(), '\n', ' ');
		snprintf(label, sizeof(label), "%s %s  %s##td%d", td.perPlayer ? ICON_FA_USER : ICON_FA_GLOBE,
			kind, preview.c_str(), (int)i);
		if(ImGui::Selectable(label, gSelected == (int)i))
			gSelected = (int)i;
	}
	if(tds.empty())
		ImGui::TextDisabled("No textdraws yet.");
	ImGui::EndChild();

	if(gSelected >= 0 && gSelected < (int)tds.size()){
		if(ImGui::Button(ICON_FA_COPY " Duplicate")){
			TextDraw copy = tds[gSelected];
			copy.name[0] = '\0';
			copy.x += 10.0f; copy.y += 10.0f;
			tds.push_back(copy);
			gSelected = (int)tds.size() - 1;
			TextDrawsChanged();
		}
		ImGui::SameLine();
		if(ImGui::Button("Up") && gSelected > 0){
			std::swap(tds[gSelected], tds[gSelected-1]);
			gSelected--;
			TextDrawsChanged();
		}
		ImGui::SetItemTooltip("Drawn earlier, so under the others.");
		ImGui::SameLine();
		if(ImGui::Button("Down") && gSelected + 1 < (int)tds.size()){
			std::swap(tds[gSelected], tds[gSelected+1]);
			gSelected++;
			TextDrawsChanged();
		}
		ImGui::SetItemTooltip("Drawn later, so over the others.");
		ImGui::SameLine();
		if(ImGui::Button(ICON_FA_TRASH " Delete")){
			tds.erase(tds.begin() + gSelected);
			gSelected = -1;
			TextDrawsChanged();
		}
		if(gSelected >= 0){
			ImGui::Separator();
			properties(tds[gSelected]);
		}
	}
	ImGui::End();
	if(!open)
		ExitTextDrawMode();
}
