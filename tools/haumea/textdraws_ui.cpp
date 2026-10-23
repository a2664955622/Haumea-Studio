// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "workspace.h"
#include "textdraws.h"
#include "radar.h"
#include "icons.h"
#include "shortcuts.h"
#include "i18n.h"
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

// CHud's layout on the 640x448 screen, under the textdraws as in game:
// gta-reversed's Hud.cpp, and gta_sa.exe 1.0 US for what it doesn't have
// (DrawPlayerInfo 0x58EAF0, DrawWanted 0x58D9A0). As SA-MP shows it: max
// health 100, so GetYPosBasedOnHealth moves the bars, money and stars up.
enum {
	HUD_RADAR = 1 << 0, HUD_CLOCK = 1 << 1, HUD_MONEY = 1 << 2, HUD_WEAPON = 1 << 3, HUD_AMMO = 1 << 4,
	HUD_HEALTH = 1 << 5, HUD_ARMOUR = 1 << 6, HUD_BREATH = 1 << 7, HUD_WANTED = 1 << 8,
	HUD_ZONE = 1 << 9, HUD_VEHICLE = 1 << 10,
	// SA-MP's defaults: no clock (TogglePlayerClock), zone names or breath bar.
	HUD_DEFAULT = HUD_RADAR | HUD_MONEY | HUD_WEAPON | HUD_AMMO | HUD_HEALTH | HUD_ARMOUR | HUD_WANTED,
};
struct HudSettings {
	bool show;
	int parts;
	int hour, minute;
	int money;
	int health, armour, breath;	// percent
	int wanted;
	int weapon;	// in gHudWeapons
	int ammo, clip;	// the total, and what is in the clip
	char zone[64], vehicle[64];
	// SA-MP's chat, which is sized in the player's pixels, not the 640x448 screen.
	bool chat;
	int resolution;	// in gHudResolutions
	int fontSize;	// /fontsize, -3 to 5
	int pageSize;	// /pagesize, 10 to 20 lines
	bool timestamps;	// /timestamp
	char messages[2048];	// one per line, {RRGGBB} colours
	// ThirteenAG's GTA SA Widescreen Fix, which many players have: the HUD
	// unstretched and smaller, the radar round. Its ini's values.
	bool wsfix;
	float hudWidthScale, hudHeightScale;	// HudWidthScale, HudHeightScale (0.8)
	float radarWidth, radarHeight;	// RadarWidth, RadarHeight (85.5, 78)
};
static const struct { int w, h; } gHudResolutions[] = {
	{ 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1366, 768 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 },
};
static const int NUM_HUD_RESOLUTIONS = IM_ARRAYSIZE(gHudResolutions);
static const HudSettings HUD_DEFAULTS = { false, HUD_DEFAULT, 12, 0, 1500, 100, 50, 100, 0, 1, 120, 17, "Ganton", "Infernus",
	true, 5, 0, 10, false,
	"{A9C4E4}SA-MP 0.3.7-R5 Started\n{A9C4E4}Connecting to 127.0.0.1:7777...\n"
	"{A9C4E4}Connected. Joining the game...\n{A9C4E4}Connected to {B9C9BF}Haumea Server\n"
	"{FFFFFF}Welcome to the server!\n{FFFF00}Player_Name: {FFFFFF}hello everyone",
	false, 0.8f, 0.8f, 85.5f, 78.0f };
static HudSettings gHud = HUD_DEFAULTS;

// The weapon's model, its icon is <model>icon in <model>.txd; its clip as
// CWeaponInfo has it at poor skill (1 and below or 1000 and above: the total only).
static const struct { const char *name, *model; int clip; bool ammo; } gHudWeapons[] = {
	{ N_("Fist"), nil, 0, false },
	{ N_("Baseball bat"), "bat", 0, false },
	{ N_("Knife"), "knifecur", 0, false },
	{ N_("Colt 45"), "colt45", 17, true },
	{ N_("Silenced pistol"), "silenced", 17, true },
	{ N_("Desert Eagle"), "desert_eagle", 7, true },
	{ N_("Shotgun"), "chromegun", 1, true },
	{ N_("Sawn-off shotgun"), "sawnoff", 2, true },
	{ N_("Combat shotgun"), "shotgspa", 7, true },
	{ N_("Micro SMG"), "micro_uzi", 50, true },
	{ N_("MP5"), "mp5lng", 30, true },
	{ N_("Tec-9"), "tec9", 50, true },
	{ N_("AK-47"), "ak47", 30, true },
	{ N_("M4"), "m4", 50, true },
	{ N_("Country rifle"), "cuntgun", 1, true },
	{ N_("Sniper rifle"), "sniper", 1, true },
	{ N_("Rocket launcher"), "rocketla", 1, true },
	{ N_("Minigun"), "minigun", 500, true },
	{ N_("Grenade"), "grenade", 1, true },
	{ N_("Spraycan"), "spraycan", 500, true },
	{ N_("Camera"), "camera", 36, true },
	{ N_("Parachute"), "gun_para", 0, false },
};
static const int NUM_HUD_WEAPONS = IM_ARRAYSIZE(gHudWeapons);

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
	else if(strcmp(key, "textdraw_hud") == 0){
		int show = 0;
		sscanf(value, "%d %x %d %d %d %d %d %d %d %d %d %d", &show, (unsigned int*)&gHud.parts,
			&gHud.hour, &gHud.minute, &gHud.money, &gHud.health, &gHud.armour, &gHud.breath,
			&gHud.wanted, &gHud.weapon, &gHud.ammo, &gHud.clip);
		gHud.show = show != 0;
		if(gHud.weapon < 0 || gHud.weapon >= NUM_HUD_WEAPONS)
			gHud.weapon = 0;
	}else if(strcmp(key, "textdraw_hud_zone") == 0)
		snprintf(gHud.zone, sizeof(gHud.zone), "%s", value);
	else if(strcmp(key, "textdraw_hud_vehicle") == 0)
		snprintf(gHud.vehicle, sizeof(gHud.vehicle), "%s", value);
	else if(strcmp(key, "textdraw_hud_chat") == 0){
		int chat = 1, timestamps = 0;
		sscanf(value, "%d %d %d %d %d", &chat, &gHud.resolution, &gHud.fontSize, &gHud.pageSize, &timestamps);
		gHud.chat = chat != 0;
		gHud.timestamps = timestamps != 0;
		gHud.resolution = std::max(0, std::min(gHud.resolution, NUM_HUD_RESOLUTIONS - 1));
		gHud.fontSize = std::max(-3, std::min(gHud.fontSize, 5));
		gHud.pageSize = std::max(10, std::min(gHud.pageSize, 20));
	}else if(strcmp(key, "textdraw_hud_wsfix") == 0){
		int on = 0;
		sscanf(value, "%d %f %f %f %f", &on, &gHud.hudWidthScale, &gHud.hudHeightScale, &gHud.radarWidth, &gHud.radarHeight);
		gHud.wsfix = on != 0;
		gHud.hudWidthScale = std::max(0.25f, std::min(gHud.hudWidthScale, 4.0f));
		gHud.hudHeightScale = std::max(0.25f, std::min(gHud.hudHeightScale, 4.0f));
	}else if(strcmp(key, "textdraw_hud_messages") == 0){
		// \n between messages, \\ for a backslash.
		size_t n = 0;
		for(const char *p = value; *p && n + 1 < sizeof(gHud.messages); p++){
			if(*p == '\\' && (p[1] == 'n' || p[1] == '\\')){
				gHud.messages[n++] = p[1] == 'n' ? '\n' : '\\';
				p++;
			}else if(*p != '\r' && *p != '\n')
				gHud.messages[n++] = *p;
		}
		gHud.messages[n] = '\0';
	}else
		return false;
	return true;
}

void
SaveTextDrawSettings(FILE *f)
{
	fprintf(f, "textdraw_background %d\n", gBackground);
	fprintf(f, "textdraw_canvas_colour %08X\n", gCanvasColour);
	fprintf(f, "textdraw_hud %d %x %d %d %d %d %d %d %d %d %d %d\n", gHud.show ? 1 : 0, (unsigned int)gHud.parts,
		gHud.hour, gHud.minute, gHud.money, gHud.health, gHud.armour, gHud.breath,
		gHud.wanted, gHud.weapon, gHud.ammo, gHud.clip);
	fprintf(f, "textdraw_hud_zone %s\n", gHud.zone);
	fprintf(f, "textdraw_hud_vehicle %s\n", gHud.vehicle);
	fprintf(f, "textdraw_hud_chat %d %d %d %d %d\n", gHud.chat ? 1 : 0, gHud.resolution, gHud.fontSize,
		gHud.pageSize, gHud.timestamps ? 1 : 0);
	fprintf(f, "textdraw_hud_wsfix %d %.3f %.3f %.1f %.1f\n", gHud.wsfix ? 1 : 0, gHud.hudWidthScale,
		gHud.hudHeightScale, gHud.radarWidth, gHud.radarHeight);
	fputs("textdraw_hud_messages ", f);
	for(const char *p = gHud.messages; *p; p++)
		if(*p == '\n') fputs("\\n", f);
		else if(*p == '\\') fputs("\\\\", f);
		else fputc(*p, f);
	fputc('\n', f);
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
		// These fonts sit in the lower rows and only have digits and capitals
		// (CFont::FindSubFontCharacter, 0x7192C0), Pricedown a few signs too.
		if(font == TD_FONT_PRICEDOWN){
			if(i == 1) return 208;	// !
			if(i == 4) return 93;	// $
			if(i == 7) return 206;	// '
			if(i == 8 || i == 9) return i + 86;	// ( )
			if(i == 14) return 207;	// .
		}
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

struct Glyph { float x, y; int index; ImU32 colour; unsigned int rgba; int line; };

static ImU32
rgbaToImU32(unsigned int c)
{
	return IM_COL32(c >> 24 & 0xFF, c >> 16 & 0xFF, c >> 8 & 0xFF, c & 0xFF);
}

// A token's letter applied to the colour, as CFont::ParseToken (0x718F00)
// does: the colour letters set a HUD colour keeping the alpha, ~h~ multiplies
// by 1.5. Only these letters are remembered as the line's tag.
static bool
applyColourToken(char code, unsigned int *colour)
{
	unsigned int alpha = *colour & 0xFF;
	switch(code){
	case 'R': case 'r': *colour = 0xB4191D00 | alpha; return true;
	case 'G': case 'g': *colour = 0x36682C00 | alpha; return true;
	case 'B': case 'b': *colour = 0x323C7F00 | alpha; return true;
	case 'Y': case 'y': *colour = 0xE2C06300 | alpha; return true;
	case 'P': case 'p': *colour = 0xA86EFC00 | alpha; return true;
	case 'W': case 'w': case 'S': case 's': *colour = 0xE1E1E100 | alpha; return true;
	case 'l': *colour = 0x00000000 | alpha; return true;	// lower case only
	case 'H': case 'h':{
		unsigned int out = alpha;
		for(int shift = 8; shift <= 24; shift += 8)
			out |= (unsigned int)std::min(255.0f, ((*colour >> shift) & 0xFF) * 1.5f) << shift;
		*colour = out;
		return true;
	}
	}
	return false;
}

// The token at s[i] ('~'): its letter, and where the text goes on. The game
// reads only the first letter and skips to the closing '~' (~red~ is ~r~);
// an unclosed one hides the rest.
static char
readToken(const std::string &s, size_t i, size_t *next)
{
	size_t end = s.find('~', i + 1);
	*next = end == std::string::npos ? s.size() : end + 1;
	char code = i + 1 < s.size() ? s[i + 1] : '\0';
	// ~k~~ACTION~ is a key name, put in before any token is read
	// (CMessages::InsertPlayerControlKeysInString): its action isn't a colour.
	if((code == 'k' || code == 'K') && *next < s.size() && s[*next] == '~'){
		end = s.find('~', *next + 1);
		*next = end == std::string::npos ? s.size() : end + 1;
	}
	return code;
}

// SA-MP sets CFont::SetScale(letterX, letterY * 0.5) in 640x448 units
// (DrawTextDrawRuntimeText, 0x100B31A0): glyphs are 32 * x by 20 * y * 0.5
// (CFont::PrintChar) and lines 18 * y * 0.5 apart (ProcessCurrentString).
static const float TD_GLYPH_HEIGHT = 10.0f, TD_LINE_HEIGHT = 9.0f;	// per unit of letterY

static float
lineHeight(const TextDraw &td)
{
	return TD_LINE_HEIGHT * td.letterY;
}

// Glyphs in canvas units, and the text's bounds. Like CFont::ProcessCurrentString
// (0x71A220): a word that doesn't fit starts a new line, and every line starts
// in the textdraw's colour plus the last colour token before it, which the game
// copies to the line's start ("~%c~"). So ~y~a~n~b is yellow twice, but
// ~w~~h~a~n~b's second line is TextDrawColor highlighted, not white.
static void
layoutText(const TextDraw &td, std::vector<Glyph> &glyphs, float *minX, float *maxX, int *numLines)
{
	glyphs.clear();
	struct Line { std::vector<Glyph> glyphs; float width; };
	std::vector<Line> lines(1);
	lines[0].width = 0.0f;
	float wrap = td.alignment == 2 ? td.sizeY : td.alignment == 3 ? td.x - td.sizeX : td.sizeX - td.x;
	unsigned int colour = td.colour;
	char tag = '\0';
	auto newLine = [&](){
		lines.push_back(Line());
		lines.back().width = 0.0f;
		colour = td.colour;
		if(tag)
			applyColourToken(tag, &colour);
	};
	const std::string &s = td.text;
	bool inWord = false;
	for(size_t i = 0; i < s.size(); i++){
		char c = s[i];
		if(c == '~'){
			size_t next;
			char code = readToken(s, i, &next);
			if(code == 'n' || code == 'N'){
				newLine();
				inWord = false;
			}else if(applyColourToken(code, &colour))
				tag = code;
			i = next - 1;
			continue;
		}
		if(c == '\n'){
			newLine();
			inWord = false;
			continue;
		}
		float advance = charAdvance(td, (unsigned char)c);
		if(c == ' ')
			inWord = false;
		else if(!inWord){
			// Word wrap at the text size: the whole word moves, like the client.
			inWord = true;
			float word = 0.0f;
			for(size_t k = i; k < s.size() && s[k] != ' ' && s[k] != '\n'; k++){
				if(s[k] == '~'){
					size_t next;
					char code = readToken(s, k, &next);
					if(code == 'n' || code == 'N')
						break;
					k = next - 1;
					continue;
				}
				word += charAdvance(td, (unsigned char)s[k]);
			}
			if(wrap > 0.0f && !lines.back().glyphs.empty() && lines.back().width + word > wrap)
				newLine();
		}
		Line *line = &lines.back();
		Glyph g;
		g.x = line->width;
		// '_' is the usual invisible character in textdraws.
		g.index = c == '_' ? 0 : glyphIndex(td.font, (unsigned char)c);
		g.colour = rgbaToImU32(colour);
		g.rgba = colour;
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
			glyph.line = (int)l;
			glyphs.push_back(glyph);
		}
	}
	*numLines = (int)lines.size();
}

std::string
DescribeTextDrawColours(const TextDraw &td)
{
	std::vector<Glyph> glyphs;
	float minX, maxX;
	int lines;
	layoutText(td, glyphs, &minX, &maxX, &lines);
	std::string out;
	char buf[32];
	for(int l = 0; l < lines; l++){
		snprintf(buf, sizeof(buf), "%sline %d:", l ? "\n" : "", l + 1);
		out += buf;
		unsigned int last = 0;
		bool any = false;
		for(const Glyph &g : glyphs)
			if(g.line == l && (!any || g.rgba != last)){
				snprintf(buf, sizeof(buf), " %08X", g.rgba);
				out += buf;
				last = g.rgba;
				any = true;
			}
	}
	return out;
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
	float w = 32.0f * td.letterX, h = TD_GLYPH_HEIGHT * td.letterY;
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
		// A negative width or height mirrors it, as in game (e.g. LD_DRV:tvcorn's four corners).
		rw::Texture *tex = spriteTexture(td.text);
		ImVec2 uv0(x1 < x0 ? 1.0f : 0.0f, y1 < y0 ? 1.0f : 0.0f), uv1(1.0f - uv0.x, 1.0f - uv0.y);
		if(x1 < x0) std::swap(x0, x1);
		if(y1 < y0) std::swap(y0, y1);
		if(tex)
			dl->AddImage((ImTextureID)tex, cv.at(x0, y0), cv.at(x1, y1), uv0, uv1, rgbaToImU32(td.colour));
		else
			dl->AddRect(cv.at(x0, y0), cv.at(x1, y1), IM_COL32(255, 80, 80, 200));
		return;
	}
	if(td.font == TD_FONT_PREVIEW){
		// Rendered with its own background, rotation and zoom, then drawn in
		// TextDrawColor like any textdraw texture (DrawTextDrawRuntimeTexture).
		rw::Texture *tex = previewTexture(td);
		if(x1 < x0) std::swap(x0, x1);
		if(y1 < y0) std::swap(y0, y1);
		if(tex)
			dl->AddImage((ImTextureID)tex, cv.at(x0, y0), cv.at(x1, y1), ImVec2(0, 1), ImVec2(1, 0),
				rgbaToImU32(td.colour));
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

// ---- The game's HUD ----

// CHudColours.
static const unsigned int HUD_RED = 0xB4191DFF, HUD_GREEN = 0x36682CFF, HUD_LIGHT_BLUE = 0xACCBF1FF,
	HUD_LIGHT_GRAY = 0xE1E1E1FF, HUD_GOLD = 0x906210FF, HUD_BLACK = 0x000000FF;

static rw::Texture*
hudWeaponIcon(int weapon)
{
	static rw::Texture *icons[NUM_HUD_WEAPONS];
	static bool looked[NUM_HUD_WEAPONS];
	if(weapon < 0 || weapon >= NUM_HUD_WEAPONS)
		return nil;
	if(gHudWeapons[weapon].model == nil)
		return spriteTexture("hud:fist");
	if(!looked[weapon]){
		looked[weapon] = true;
		int slot = FindTxdSlot(gHudWeapons[weapon].model);
		if(slot >= 0){
			if(!IsTxdLoaded(slot))
				LoadTxd(slot);
			icons[weapon] = FindTextureInTxd(slot, (std::string(gHudWeapons[weapon].model) + "icon").c_str());
		}
	}
	return icons[weapon];
}

// CFont text as a textdraw draws it: the same fonts and edges. The HUD sets
// CFont's scale itself, a textdraw's letter height is halved first.
// fromBottom: y is where the line ends (CFont::PrintStringFromBottom).
static void
hudText(ImDrawList *dl, const Canvas &cv, const char *text, float x, float y, int font, float sx, float sy,
	int alignment, bool proportional, int edge, unsigned int colour, bool fromBottom = false)
{
	TextDraw td;
	InitTextDraw(&td);
	td.text = text;
	td.font = font;
	td.letterX = sx; td.letterY = sy * 2.0f;
	td.x = x; td.y = fromBottom ? y - lineHeight(td) : y;
	td.alignment = alignment;
	td.sizeY = 640.0f;	// CFont::SetCentreSize, for centred text
	td.proportional = proportional;
	td.shadow = 0;
	td.outline = edge;
	td.colour = colour;
	td.backgroundColour = HUD_BLACK;
	drawTextDraw(dl, cv, td);
}

// CSprite2d::DrawBarChart with its black border.
static void
hudBar(ImDrawList *dl, const Canvas &cv, float x, float y, float w, float h, float bx, float by, int percent,
	unsigned int colour)
{
	float fill = x + w * std::min(100, std::max(0, percent)) / 100.0f;
	unsigned int dark = ((colour >> 25 & 0x7F) << 24) | ((colour >> 17 & 0x7F) << 16) | ((colour >> 9 & 0x7F) << 8) | 0xFF;
	dl->AddRectFilled(cv.at(x, y), cv.at(fill, y + h), rgbaToImU32(colour));
	dl->AddRectFilled(cv.at(fill, y), cv.at(x + w, y + h), rgbaToImU32(dark));
	ImU32 black = IM_COL32(0, 0, 0, 255);
	dl->AddRectFilled(cv.at(x, y), cv.at(x + w, y + by), black);
	dl->AddRectFilled(cv.at(x, y + h - by), cv.at(x + w, y + h), black);
	dl->AddRectFilled(cv.at(x, y), cv.at(x + bx, y + h), black);
	dl->AddRectFilled(cv.at(x + w - bx, y), cv.at(x + w, y + h), black);
}

// CChatWindow: Arial bold at GetFontSize's size for the screen width
// (14 under 1024, 16 under 1400, 18, 20 from 1600, two per /fontsize step),
// outlined in black, from 45, 10 in the player's pixels. White unless a
// {RRGGBB} changes it.
static void
drawChat(ImDrawList *dl, const Canvas &cv)
{
	int r = std::max(0, std::min(gHud.resolution, NUM_HUD_RESOLUTIONS - 1));
	int w = gHudResolutions[r].w;
	float size = (float)(w < 1024 ? 14 : w < 1400 ? 16 : w < 1600 ? 18 : 20) + 2.0f * gHud.fontSize;
	float px = cv.sy * 448.0f / gHudResolutions[r].h;	// screen pixels per player pixel
	ImFont *font = ImGui::GetFont();
	float fontSize = size * px;
	float x0 = cv.x + 45.0f * px, y = cv.y + 10.0f * px;
	std::vector<std::string> lines;
	for(const char *p = gHud.messages; *p; ){
		const char *e = strchr(p, '\n');
		lines.push_back(std::string(p, e ? e - p : strlen(p)));
		p = e ? e + 1 : p + strlen(p);
	}
	// The last pageSize lines, at the bottom of the page as new ones arrive.
	size_t first = lines.size() > (size_t)gHud.pageSize ? lines.size() - gHud.pageSize : 0;
	y += (gHud.pageSize - (int)(lines.size() - first)) * fontSize;
	for(size_t i = first; i < lines.size(); i++, y += fontSize){
		float x = x0;
		ImU32 colour = IM_COL32_WHITE;
		std::string text = gHud.timestamps ? "[12:00:00] " + lines[i] : lines[i];
		size_t at = 0;
		while(at < text.size()){
			size_t open = text.find('{', at);
			size_t end = open;
			// A run of text up to the next {RRGGBB}.
			while(open != std::string::npos){
				bool hex = open + 7 < text.size() && text[open + 7] == '}';
				for(int k = 1; hex && k <= 6; k++)
					hex = isxdigit((unsigned char)text[open + k]) != 0;
				if(hex)
					break;
				open = text.find('{', open + 1);
			}
			end = open == std::string::npos ? text.size() : open;
			if(end > at){
				const char *b = text.c_str() + at, *e = text.c_str() + end;
				for(int oy = -1; oy <= 1; oy++)
					for(int ox = -1; ox <= 1; ox++)
						if(ox || oy)
							dl->AddText(font, fontSize, ImVec2(x + ox * px, y + oy * px), IM_COL32(0, 0, 0, 255), b, e);
				dl->AddText(font, fontSize, ImVec2(x, y), colour, b, e);
				dl->AddText(font, fontSize, ImVec2(x + 0.5f * px, y), colour, b, e);	// Arial bold
				x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, b, e).x + 0.5f * px;
			}
			if(open == std::string::npos)
				break;
			unsigned int rgb = (unsigned int)strtoul(text.substr(open + 1, 6).c_str(), nil, 16);
			colour = IM_COL32(rgb >> 16 & 0xFF, rgb >> 8 & 0xFF, rgb & 0xFF, 255);
			at = open + 8;
		}
	}
}

// Where the HUD's 640x448 numbers land. The game stretches them to the
// screen. The Widescreen Fix makes them square ((4/3) / aspect across) and
// scales them by HudWidthScale and HudHeightScale, keeping each part against
// its edge (its Frontend.ixx); the radar is only made square.
struct HudLayout {
	float kx, ky;
	float R(float x) const { return 640.0f - kx * (640.0f - x); }	// from the right edge
	float T(float y) const { return y * ky; }	// from the top
	float B(float y) const { return 448.0f - ky * (448.0f - y); }	// from the bottom
};

static void
drawHud(ImDrawList *dl, const Canvas &cv)
{
	const int p = gHud.parts;
	char buf[64];
	float square = (4.0f / 3.0f) / (640.0f * cv.sx / (448.0f * cv.sy));
	HudLayout l = { 1.0f, 1.0f };
	if(gHud.wsfix){
		l.kx = square * gHud.hudWidthScale;
		l.ky = gHud.hudHeightScale;
	}
	if(p & HUD_RADAR){
		// CHud::DrawRadar: 40 from the left, 104 above the bottom, 94 x 76.
		float kx = gHud.wsfix ? square : 1.0f;
		float w = (gHud.wsfix ? gHud.radarWidth : 94.0f) * kx, h = gHud.wsfix ? gHud.radarHeight : 76.0f;
		ImVec2 centre = cv.at(40.0f * kx + w * 0.5f, 448.0f - 104.0f + h * 0.5f);
		DrawRadarOnCanvas(dl, centre, ImVec2(w * 0.5f * cv.sx, h * 0.5f * cv.sy), cv.sy);
	}
	if(p & HUD_WEAPON){
		// 32 from the right and 111 (0.1734 of the width) for the icon, 47 x 58.
		rw::Texture *icon = hudWeaponIcon(gHud.weapon);
		if(icon)
			dl->AddImage((ImTextureID)icon, cv.at(l.R(497.0f), l.T(20.0f)), cv.at(l.R(544.0f), l.T(78.0f)));
	}
	if((p & HUD_AMMO) && gHud.weapon >= 0 && gHud.weapon < NUM_HUD_WEAPONS && gHudWeapons[gHud.weapon].ammo){
		// CHud::DrawAmmo: "total-clip", or the total for one-shot clips.
		int clip = gHudWeapons[gHud.weapon].clip;
		int inClip = std::min(gHud.clip, gHud.ammo);
		if(clip <= 1 || clip >= 1000)
			snprintf(buf, sizeof(buf), "%d", gHud.ammo);
		else
			snprintf(buf, sizeof(buf), "%d-%d", std::min(gHud.ammo - inClip, 9999), inClip);
		hudText(dl, cv, buf, l.R(520.5f), l.T(63.0f), TD_FONT_AHARONI, 0.3f * l.kx, 0.7f * l.ky, 2, true, 1, HUD_LIGHT_BLUE);
	}
	if(p & HUD_CLOCK){
		snprintf(buf, sizeof(buf), "%02d:%02d", gHud.hour, gHud.minute);
		hudText(dl, cv, buf, l.R(608.0f), l.T(22.0f), TD_FONT_PRICEDOWN, 0.55f * l.kx, 1.1f * l.ky, 3, false, 2,
			HUD_LIGHT_GRAY);
	}
	if(p & HUD_HEALTH){	// 141 from the right, 77 - 10 down, 109 wide at max health 100 / 176
		float w = 109.0f * 100.0f / 176.0f;
		hudBar(dl, cv, l.R(499.0f + 109.0f - w), l.T(67.0f), w * l.kx, 9.0f * l.ky, 2.0f * l.kx, 2.0f * l.ky,
			gHud.health, HUD_RED);
	}
	if((p & HUD_ARMOUR) && gHud.armour > 1)	// 94 from the right, 48 - 3 down
		hudBar(dl, cv, l.R(546.0f), l.T(45.0f), 62.0f * l.kx, 9.0f * l.ky, 2.0f * l.kx, 2.0f * l.ky,
			gHud.armour, HUD_LIGHT_GRAY);
	if(p & HUD_BREATH)	// under water: 62 - 6 down
		hudBar(dl, cv, l.R(546.0f), l.T(56.0f), 62.0f * l.kx, 9.0f * l.ky, 2.0f * l.kx, 2.0f * l.ky,
			gHud.breath, HUD_LIGHT_BLUE);
	if(p & HUD_MONEY){
		if(gHud.money < 0)
			snprintf(buf, sizeof(buf), "-$%07d", -gHud.money);
		else
			snprintf(buf, sizeof(buf), "$%08d", gHud.money);
		hudText(dl, cv, buf, l.R(608.0f), l.T(77.0f), TD_FONT_PRICEDOWN, 0.55f * l.kx, 1.1f * l.ky, 3, false, 2,
			gHud.money < 0 ? HUD_RED : HUD_GREEN);
	}
	if((p & HUD_WANTED) && gHud.wanted > 0)	// six stars (']'), the first on the right, 18 apart
		for(int i = 0; i < 6; i++)
			hudText(dl, cv, "]", l.R(611.0f - 18.0f * i), l.T(102.0f), TD_FONT_BECKETT, 0.605f * l.kx, 1.21f * l.ky,
				3, true, 1, i < gHud.wanted ? HUD_GOLD : HUD_BLACK);
	if((p & HUD_VEHICLE) && gHud.vehicle[0])
		hudText(dl, cv, gHud.vehicle, l.R(608.0f), l.B(344.0f), TD_FONT_BANK_GOTHIC, 1.0f * l.kx, 1.5f * l.ky,
			3, true, 2, HUD_GREEN);
	if((p & HUD_ZONE) && gHud.zone[0]){
		// The line ends 104 - 76 above the bottom.
		hudText(dl, cv, gHud.zone, l.R(608.0f), l.B(420.0f), TD_FONT_BECKETT, 1.2f * l.kx, 1.9f * l.ky, 3, true, 2,
			HUD_LIGHT_BLUE, true);
	}
	if(gHud.chat)
		drawChat(dl, cv);
}

static void
hudSettings(void)
{
	bool changed = ImGui::Checkbox(L("Game HUD"), &gHud.show);
	ImGui::SetItemTooltip("%s", _("The game's HUD under the textdraws, where SA-MP shows it, to check they don't cover it."));
	ImGui::SameLine();
	if(ImGui::SmallButton(ICON_FA_GEAR "##hud_settings"))
		ImGui::OpenPopup("##hud_popup");
	ImGui::SetItemTooltip("%s", _("What the HUD shows"));
	if(ImGui::BeginPopup("##hud_popup")){
		static const struct { int bit; const char *label; } parts[] = {
			{ HUD_RADAR, N_("Radar") }, { HUD_WEAPON, N_("Weapon") }, { HUD_AMMO, N_("Ammo") },
			{ HUD_HEALTH, N_("Health") }, { HUD_ARMOUR, N_("Armour") }, { HUD_BREATH, N_("Breath") },
			{ HUD_MONEY, N_("Money") }, { HUD_CLOCK, N_("Clock") }, { HUD_WANTED, N_("Wanted level") },
			{ HUD_VEHICLE, N_("Vehicle name") }, { HUD_ZONE, N_("Zone name") },
		};
		ImGui::SeparatorText(_("Show"));
		for(int i = 0; i < IM_ARRAYSIZE(parts); i++){
			if(i % 3)
				ImGui::SameLine(ImGui::GetFontSize() * 8.0f * (i % 3));
			changed |= ImGui::CheckboxFlags(_(parts[i].label), &gHud.parts, parts[i].bit);
		}
		ImGui::SetItemTooltip("%s", _("SA-MP shows zone names with EnableZoneNames, the clock with TogglePlayerClock."));
		ImGui::SeparatorText(_("Values"));
		float w = ImGui::GetFontSize() * 10.0f;
		ImGui::PushItemWidth(w);
		changed |= ImGui::SliderInt(L("Health"), &gHud.health, 0, 100, "%d%%");
		changed |= ImGui::SliderInt(L("Armour"), &gHud.armour, 0, 100, "%d%%");
		changed |= ImGui::SliderInt(L("Breath"), &gHud.breath, 0, 100, "%d%%");
		changed |= ImGui::InputInt(L("Money"), &gHud.money, 100, 10000);
		gHud.money = std::max(-9999999, std::min(gHud.money, 99999999));
		int time[2] = { gHud.hour, gHud.minute };
		if(ImGui::DragInt2(L("Time"), time, 0.2f, 0, 59, "%02d")){
			gHud.hour = std::max(0, std::min(time[0], 23));
			gHud.minute = std::max(0, std::min(time[1], 59));
			changed = true;
		}
		changed |= ImGui::SliderInt(L("Wanted level"), &gHud.wanted, 0, 6);
		const char *weapons[NUM_HUD_WEAPONS];
		for(int i = 0; i < NUM_HUD_WEAPONS; i++)
			weapons[i] = _(gHudWeapons[i].name);
		changed |= ImGui::Combo(L("Weapon"), &gHud.weapon, weapons, NUM_HUD_WEAPONS);
		int ammo[2] = { gHud.ammo, gHud.clip };
		if(ImGui::InputInt2(L("Ammo, in clip"), ammo)){
			gHud.ammo = std::max(0, ammo[0]);
			gHud.clip = std::max(0, ammo[1]);
			changed = true;
		}
		ImGui::InputText(L("Vehicle name"), gHud.vehicle, sizeof(gHud.vehicle));
		changed |= ImGui::IsItemDeactivatedAfterEdit();
		ImGui::InputText(L("Zone name"), gHud.zone, sizeof(gHud.zone));
		changed |= ImGui::IsItemDeactivatedAfterEdit();
		ImGui::PopItemWidth();

		ImGui::SeparatorText(_("Widescreen Fix"));
		changed |= ImGui::Checkbox(L("As with the Widescreen Fix"), &gHud.wsfix);
		ImGui::SetItemTooltip("%s", _("ThirteenAG's GTA SA Widescreen Fix: the HUD unstretched and smaller, the radar round.\n"
			"Many players have it; check your textdraws both ways."));
		if(gHud.wsfix){
			ImGui::PushItemWidth(w);
			float scale[2] = { gHud.hudWidthScale, gHud.hudHeightScale };
			if(ImGui::DragFloat2(L("HudWidthScale, HudHeightScale"), scale, 0.01f, 0.25f, 4.0f, "%.2f")){
				gHud.hudWidthScale = scale[0];
				gHud.hudHeightScale = scale[1];
				changed = true;
			}
			float radar[2] = { gHud.radarWidth, gHud.radarHeight };
			if(ImGui::DragFloat2(L("RadarWidth, RadarHeight"), radar, 0.5f, 10.0f, 300.0f, "%.1f")){
				gHud.radarWidth = radar[0];
				gHud.radarHeight = radar[1];
				changed = true;
			}
			ImGui::SetItemTooltip("%s", _("As in its GTASA.WidescreenFix.ini (0.8, 0.8, 85.5 and 78 by default)"));
			ImGui::PopItemWidth();
		}

		ImGui::SeparatorText(_("SA-MP chat"));
		changed |= ImGui::Checkbox(L("Chat"), &gHud.chat);
		ImGui::SameLine();
		changed |= ImGui::Checkbox(L("Timestamps"), &gHud.timestamps);
		ImGui::PushItemWidth(w);
		char res[NUM_HUD_RESOLUTIONS][24];
		const char *resNames[NUM_HUD_RESOLUTIONS];
		for(int i = 0; i < NUM_HUD_RESOLUTIONS; i++){
			snprintf(res[i], sizeof(res[i]), "%d x %d", gHudResolutions[i].w, gHudResolutions[i].h);
			resNames[i] = res[i];
		}
		changed |= ImGui::Combo(L("Player's screen"), &gHud.resolution, resNames, NUM_HUD_RESOLUTIONS);
		ImGui::SetItemTooltip("%s", _("The chat is sized in the player's pixels: it takes less of a bigger screen."));
		changed |= ImGui::SliderInt(L("/fontsize"), &gHud.fontSize, -3, 5);
		changed |= ImGui::SliderInt(L("/pagesize"), &gHud.pageSize, 10, 20);
		ImGui::PopItemWidth();
		ImGui::InputTextMultiline(L("Messages"), gHud.messages, sizeof(gHud.messages),
			ImVec2(ImGui::GetFontSize() * 22.0f, ImGui::GetFontSize() * 6.0f));
		changed |= ImGui::IsItemDeactivatedAfterEdit();
		ImGui::SetItemTooltip("%s", _("One message per line; {RRGGBB} changes the colour."));

		if(ImGui::Button(L("Reset"))){
			bool show = gHud.show;
			gHud = HUD_DEFAULTS;
			gHud.show = show;
			changed = true;
		}
		ImGui::SetItemTooltip("%s", _("SA-MP's defaults"));
		ImGui::EndPopup();
	}
	if(changed)
		SaveEditorSettings();
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
	if(gHud.show)
		drawHud(dl, cv);
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
		if(x1 < x0) std::swap(x0, x1);	// mirrored sprites
		if(y1 < y0) std::swap(y0, y1);
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
		if(ImGui::IsItemHovered()){
			const char *name = td.name[0] ? td.name : _("textdraw");
			if(td.locked)
				ImGui::SetTooltip(ICON_FA_LOCK " %s", name);
			else
				ImGui::SetTooltip("%s", name);
		}
		if(dragging == i && !resizing && !td.locked && ImGui::IsItemActive()){
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
	// the box's edge for a box, the size for sprites and models. Dragged
	// past the opposite edge, a sprite's size goes negative and it mirrors.
	if(gSelected >= 0 && !tds[gSelected].locked){
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
					td.sizeX += dx;
					td.sizeY += dy;
				}else if(td.useBox){
					if(td.alignment == 2) td.sizeY = std::max(1.0f, td.sizeY + dx * 2.0f);
					else td.sizeX += dx;
					int lines = std::max(1, (int)((y1 - y0) / std::max(0.01f, lineHeight(td))));
					td.letterY = std::max(0.05f, td.letterY + dy / (TD_LINE_HEIGHT * lines));
				}else{
					float width = std::max(1.0f, x1 - x0);
					td.letterX = std::max(0.05f, td.letterX * (width + dx) / width);
					td.letterY = std::max(0.05f, td.letterY + dy / TD_GLYPH_HEIGHT);
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
	if(ImGui::BeginCombo(L("Sprite TXD"), txdName.c_str(), ImGuiComboFlags_HeightLarge)){
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
		ImGui::TextDisabled(_("No textures in %s."), txdName.c_str());
	ImGui::EndChild();
}

static void
properties(TextDraw &td)
{
	// What it is, what it shows, then where; the style is closed by default.
	bool changed = false;
	ImGui::InputText(L("Variable"), td.name, sizeof(td.name));
	if(ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::SetItemTooltip("%s", _("The Pawn variable. Empty or not a plain name makes one up."));
	int scope = td.perPlayer ? 1 : 0;
	const char *scopes[] = { _("To everyone (TextDrawCreate)"), _("Per player (CreatePlayerTextDraw)") };
	if(ImGui::Combo(L("Shown"), &scope, scopes, IM_ARRAYSIZE(scopes))){
		td.perPlayer = scope == 1;
		changed = true;
	}
	static const char *fonts[] = { N_("0 Beckett (gothic)"), N_("1 Aharoni"), N_("2 Bank Gothic (capitals)"),
		N_("3 Pricedown (capitals)"), N_("4 Sprite"), N_("5 Model preview") };
	const char *fontNames[IM_ARRAYSIZE(fonts)];
	for(int i = 0; i < IM_ARRAYSIZE(fonts); i++)
		fontNames[i] = _(fonts[i]);
	changed |= ImGui::Combo(L("Font"), &td.font, fontNames, IM_ARRAYSIZE(fonts));
	bool isText = td.font != TD_FONT_SPRITE && td.font != TD_FONT_PREVIEW;
	if(td.font == TD_FONT_SPRITE){
		char buf[256];
		snprintf(buf, sizeof(buf), "%s", td.text.c_str());
		if(ImGui::InputText(L("Sprite"), buf, sizeof(buf))){
			td.text = buf;
			changed = true;
		}
		spritePicker(td);
	}else if(isText){
		char buf[1024];
		snprintf(buf, sizeof(buf), "%s", td.text.c_str());
		if(ImGui::InputTextMultiline(L("Text"), buf, sizeof(buf), ImVec2(-FLT_MIN, ImGui::GetFontSize() * 3.5f))){
			td.text = buf;
			changed = true;
		}
		ImGui::SetItemTooltip("%s", _("~n~ new line, ~r~ ~g~ ~b~ ~y~ ~p~ ~w~ ~l~ colours, ~h~ lighter, _ blank"));
	}else{
		changed |= ImGui::InputInt(L("Model"), &td.previewModel);
		float rot[3] = { td.previewRotX, td.previewRotY, td.previewRotZ };
		if(ImGui::DragFloat3(L("Rotation"), rot, 1.0f, -360.0f, 360.0f, "%.1f")){
			td.previewRotX = rot[0]; td.previewRotY = rot[1]; td.previewRotZ = rot[2];
			changed = true;
		}
		changed |= ImGui::DragFloat(L("Zoom"), &td.previewZoom, 0.01f, 0.1f, 10.0f, "%.2f");
		int cols[2] = { td.previewColour1, td.previewColour2 };
		if(ImGui::InputInt2(L("Vehicle colours"), cols)){
			td.previewColour1 = cols[0]; td.previewColour2 = cols[1];
			changed = true;
		}
	}
	changed |= colourEdit(td.font == TD_FONT_SPRITE ? L("Tint") : L("Colour"), &td.colour);

	ImGui::SeparatorText(_("Position"));
	float pos[2] = { td.x, td.y };
	if(ImGui::DragFloat2(L("Position"), pos, 0.5f, -100.0f, 740.0f, "%.2f")){
		td.x = pos[0]; td.y = pos[1];
		changed = true;
	}
	ImGui::SetItemTooltip("%s", _("On the 640x448 canvas every resolution is scaled to."));
	float size[2] = { td.sizeX, td.sizeY };
	if(ImGui::DragFloat2(L("Text size"), size, 0.5f, -1280.0f, 1280.0f, "%.2f")){
		td.sizeX = size[0]; td.sizeY = size[1];
		changed = true;
	}
	ImGui::SetItemTooltip("%s", td.font >= TD_FONT_SPRITE ? _("Width and height. A negative one mirrors a sprite.") :
		_("Left: the box's right edge. Centre: width and height are swapped (the second is the width).\n"
		"Right: the box's left edge. Text wraps there."));

	if(ImGui::CollapsingHeader(L("Style"))){
		if(isText){
			float letter[2] = { td.letterX, td.letterY };
			if(ImGui::DragFloat2(L("Letter size"), letter, 0.005f, 0.0f, 10.0f, "%.3f")){
				td.letterX = letter[0]; td.letterY = letter[1];
				changed = true;
			}
			int align = td.alignment - 1;
			const char *aligns[] = { _("Left"), _("Centre"), _("Right") };
			if(ImGui::Combo(L("Alignment"), &align, aligns, IM_ARRAYSIZE(aligns))){
				td.alignment = align + 1;
				changed = true;
			}
			changed |= ImGui::Checkbox(L("Proportional"), &td.proportional);
			ImGui::SetItemTooltip("%s", _("Letters take their own width instead of a fixed one."));
			ImGui::SameLine();
			changed |= ImGui::Checkbox(L("Box"), &td.useBox);
			if(td.useBox)
				changed |= colourEdit(L("Box colour"), &td.boxColour);
			changed |= ImGui::SliderInt(L("Shadow"), &td.shadow, 0, 10);
			changed |= ImGui::SliderInt(L("Outline"), &td.outline, 0, 10);
		}
		changed |= colourEdit(td.font == TD_FONT_PREVIEW ? L("Background") : L("Shadow/outline colour"), &td.backgroundColour);
		changed |= ImGui::Checkbox(L("Selectable"), &td.selectable);
		ImGui::SetItemTooltip("%s", _("Players can click it while SelectTextDraw is on (OnPlayerClickTextDraw)."));
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
afterFileChange(const char *toast)
{
	gSelected = -1;
	AddRecentTextDrawFile(GetTextDrawFilePath());
	SaveEditorSettings();
	if(toast)
		Toast(TOAST_SAVE, toast, GetTextDrawFileName());
}

static void
openFile(const char *path)
{
	std::string error;
	if(!OpenTextDrawFile(path, &error)){
		ToastFor(TOAST_SAVE, 5.0f, _("Could not open %s"), path);
		gRecent.erase(std::remove(gRecent.begin(), gRecent.end(), std::string(path)), gRecent.end());
		return;
	}
	afterFileChange(_("Opened %s"));
}

static bool
saveFile(const char *path)
{
	std::string error;
	if(!SaveTextDrawFile(path, &error)){
		ToastFor(TOAST_SAVE, 5.0f, _("Could not save the textdraws: %s"), error.c_str());
		return false;
	}
	afterFileChange(_("Saved %s"));
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
	Toast(TOAST_SAVE, "%s", _("New textdraws"));
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
		ToastFor(TOAST_SAVE, 6.0f, _("Could not move the textdraws: %s"), error.c_str());
		return;
	}
	afterFileChange(nil);
	ToastFor(TOAST_SAVE, 6.0f, _("Textdraws moved: %d, into %s. Save the map to take them out of it."),
		moved, GetTextDrawFileName());
}

static void
drawPopups(void)
{
	if(gOpenUnsavedPrompt){
		ImGui::OpenPopup(L("Unsaved textdraws"));
		gOpenUnsavedPrompt = false;
	}
	if(ImGui::BeginPopupModal(L("Unsaved textdraws"), nil, ImGuiWindowFlags_AlwaysAutoResize)){
		ImGui::Text(_("Save changes to %s?"), GetTextDrawFileName());
		ImGui::TextDisabled("%s", _("Your textdraw changes are lost if you don't save them."));
		ImGui::Spacing();
		if(ImGui::Button(L("Save"), ImVec2(110, 0))){
			ImGui::CloseCurrentPopup();
			saveOrAsk();	// continues once the file is written
		}
		ImGui::SameLine();
		if(ImGui::Button(L("Don't Save"), ImVec2(110, 0))){
			ImGui::CloseCurrentPopup();
			void (*then)(void) = gAfterClose;
			gAfterClose = nil;
			if(then)
				then();
		}
		ImGui::SameLine();
		if(ImGui::Button(L("Cancel"), ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)){
			gAfterClose = nil;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if(gOpenMovePrompt){
		ImGui::OpenPopup(L("Move textdraws"));
		gOpenMovePrompt = false;
	}
	if(ImGui::BeginPopupModal(L("Move textdraws"), nil, ImGuiWindowFlags_AlwaysAutoResize)){
		int count = (int)GetMapTextDraws().size();
		ImGui::Text(_("Textdraws in this map: %d."), count);
		ImGui::TextDisabled("%s", _("Textdraws now have their own file, which Test in game includes.\n"
			"Left in the map they are read-only."));
		ImGui::Spacing();
		if(GetTextDrawFilePath()[0])
			ImGui::Text(_("They go into %s."), GetTextDrawFileName());
		else{
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f);
			ImGui::InputText(L("File"), gMovePath, sizeof(gMovePath));
		}
		ImGui::Spacing();
		if(ImGui::Button(L("Move textdraws to a separate file"))){
			ImGui::CloseCurrentPopup();
			moveMapTextDraws();
		}
		ImGui::SameLine();
		if(ImGui::Button(L("Keep them read-only")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::SetItemTooltip("%s", _("They stay in the map and are saved with it. TextDraws can move them later."));
		ImGui::EndPopup();
	}

	if(gShowFileDialog){
		// ###tdfile: one window for both, with the ID it has always had.
		char title[128];
		snprintf(title, sizeof(title), "%s###tdfile", gFileDialogSave ? _("Save TextDraws As") : _("Open TextDraws"));
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if(gFileDialogFocus){
			ImGui::SetNextWindowFocus();
			gFileDialogFocus = false;
		}
		if(ImGui::Begin(title, &gShowFileDialog, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoDocking)){
			ImGui::TextDisabled("%s", _("A .pwn or .inc file with only textdraws; your gamemode includes it."));
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f);
			ImGui::InputText(L("Path"), gFileDialogPath, sizeof(gFileDialogPath));
			ImGui::SameLine();
			if(ImGui::Button(L("Browse..."))){
				char picked[1024];
				if(PickPawnFile(picked, sizeof(picked), gFileDialogSave, gFileDialogPath[0] ? gFileDialogPath : nil))
					snprintf(gFileDialogPath, sizeof(gFileDialogPath), "%s", picked);
			}
			if(ImGui::Button(gFileDialogSave ? L("Save") : L("Open"), ImVec2(110, 0)) && gFileDialogPath[0]){
				gShowFileDialog = false;
				if(gFileDialogSave)
					saveFile(gFileDialogPath);
				else
					openFile(gFileDialogPath);
			}
			ImGui::SameLine();
			if(ImGui::Button(L("Cancel"), ImVec2(110, 0))){
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
		ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "%s", TI(ICON_FA_FONT, "TextDraw mode"));
		ImGui::SameLine();
		ImGui::Text("%s%s", GetTextDrawFileName(), TextDrawFileHasUnsavedChanges() ? " *" : "");
		ImGui::SameLine();
		ImGui::TextDisabled("%s", _("Map editing is off."));
		ImGui::SameLine();
		if(ImGui::Button(LI(ICON_FA_XMARK, "Exit")))
			ExitTextDrawMode();
		ImGui::SetItemTooltip(_("Back to the map (%s)"), ShortcutLabel(SC_CANCEL));
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
			Toast(TOAST_UNDO_REDO, "%s", _("Undo"));
		return true;
	case SC_REDO:
		if(RedoTextDraws())
			Toast(TOAST_UNDO_REDO, "%s", _("Redo"));
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
	if(ImGui::Button(LI(ICON_FA_FILE, "New")))
		RequestTextDrawFileClose(newFile);
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_FOLDER_OPEN, "Open...")))
		RequestTextDrawFileClose(showOpenDialog);
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_FLOPPY_DISK, "Save")))
		saveOrAsk();
	ImGui::SetItemTooltip("%s", ShortcutLabel(SC_SAVE_MAP));
	ImGui::SameLine();
	if(ImGui::Button(L("Save As...")))
		showFileDialog(true);
	ImGui::SameLine();
	ImGui::BeginDisabled(gRecent.empty());
	if(ImGui::Button(ICON_FA_CLOCK_ROTATE_LEFT "##recent"))
		ImGui::OpenPopup("recent");
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("%s", _("Recent textdraw files"));
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
		if(ImGui::MenuItem(L("Clear list"))){
			gRecent.clear();
			SaveEditorSettings();
		}
		ImGui::EndPopup();
	}
	ImGui::TextDisabled(TextDrawFileHasUnsavedChanges() ? _("Unsaved changes - %s") : _("Saved - %s"),
		GetTextDrawFileName());
	if(GetTextDrawFilePath()[0])
		ImGui::SetItemTooltip(_("%s\nInclude it in your gamemode and call %s_CreateTextDraws()\n"
			"from OnGameModeInit and %s_ShowTextDraws(playerid)."), GetTextDrawFilePath(),
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
	// ###textdraws, not LI(): the ID the window has always had, so saved layouts stay.
	char title[64];
	snprintf(title, sizeof(title), ICON_FA_FONT " %s###textdraws", _("TextDraws"));
	if(!ImGui::Begin(title, &open)){
		ImGui::End();
		if(!open)
			ExitTextDrawMode();
		return;
	}
	std::vector<TextDraw> &tds = GetTextDraws();
	fileBar();

	int background = gBackground;
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
	const char *backgrounds[] = { _("Plain"), _("Still frame") };
	if(ImGui::Combo(L("Background"), &background, backgrounds, IM_ARRAYSIZE(backgrounds))){
		gBackground = background;
		if(gBackground == TD_BACKGROUND_STILL && gStillFrame == nil){
			// Taken from the world on the next frame.
			gStillPending = true;
		}
		SaveEditorSettings();
	}
	ImGui::SetItemTooltip("%s", _("Still frame: the world as the camera saw it when TextDraw mode started."));
	if(gBackground == TD_BACKGROUND_PLAIN){
		ImGui::SameLine();
		if(colourEdit("##canvas", &gCanvasColour))
			SaveEditorSettings();
	}
	hudSettings();

	const std::vector<TextDraw> &mapTds = GetMapTextDraws();
	if(!mapTds.empty()){
		ImGui::Separator();
		char inMap[128];
		snprintf(inMap, sizeof(inMap), _("%d in the map (read-only)"), (int)mapTds.size());
		ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " %s", inMap);
		ImGui::SetItemTooltip("%s", _("From a map made before textdraws had their own file. They are saved\n"
			"back with the map as they are; move them here to edit them."));
		ImGui::SameLine();
		if(ImGui::SmallButton(L("Move to a separate file...")))
			OfferMoveMapTextDraws(GetPawnMapPath());
	}
	ImGui::Separator();

	if(ImGui::Button(LI(ICON_FA_PLUS, "Text##add"))) addTextDraw(TD_FONT_AHARONI);
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_PLUS, "Box##add"))) addTextDraw(-1);
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_PLUS, "Sprite##add"))) addTextDraw(TD_FONT_SPRITE);
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_PLUS, "Model##add"))) addTextDraw(TD_FONT_PREVIEW);

	ImGui::BeginChild("list", ImVec2(0, ImGui::GetFontSize() * 8.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
	for(size_t i = 0; i < tds.size(); i++){
		const TextDraw &td = tds[i];
		char label[160];
		const char *kind = td.font == TD_FONT_SPRITE ? _("sprite") : td.font == TD_FONT_PREVIEW ? _("model") :
			td.useBox ? _("box") : _("text");
		std::string preview = td.text.substr(0, 28);
		std::replace(preview.begin(), preview.end(), '\n', ' ');
		snprintf(label, sizeof(label), "%s%s %s  %s##td%d", td.locked ? ICON_FA_LOCK " " : "",
			td.perPlayer ? ICON_FA_USER : ICON_FA_GLOBE, kind, preview.c_str(), (int)i);
		if(ImGui::Selectable(label, gSelected == (int)i))
			gSelected = (int)i;
	}
	if(tds.empty())
		ImGui::TextDisabled("%s", _("No textdraws yet."));
	ImGui::EndChild();

	if(gSelected >= 0 && gSelected < (int)tds.size()){
		if(ImGui::Button(LI(ICON_FA_COPY, "Duplicate"))){
			TextDraw copy = tds[gSelected];
			copy.name[0] = '\0';
			copy.x += 10.0f; copy.y += 10.0f;
			tds.push_back(copy);
			gSelected = (int)tds.size() - 1;
			TextDrawsChanged();
		}
		ImGui::SameLine();
		if(ImGui::Button(L("Up")) && gSelected > 0){
			std::swap(tds[gSelected], tds[gSelected-1]);
			gSelected--;
			TextDrawsChanged();
		}
		ImGui::SetItemTooltip("%s", _("Drawn earlier, so under the others."));
		ImGui::SameLine();
		if(ImGui::Button(L("Down")) && gSelected + 1 < (int)tds.size()){
			std::swap(tds[gSelected], tds[gSelected+1]);
			gSelected++;
			TextDrawsChanged();
		}
		ImGui::SetItemTooltip("%s", _("Drawn later, so over the others."));
		ImGui::SameLine();
		if(ImGui::Button(LI(ICON_FA_TRASH, "Delete"))){
			tds.erase(tds.begin() + gSelected);
			gSelected = -1;
			TextDrawsChanged();
		}
		if(gSelected >= 0){
			ImGui::SameLine();
			if(ImGui::Checkbox(LI(ICON_FA_LOCK, "Locked"), &tds[gSelected].locked))
				TextDrawsChanged();
			ImGui::SetItemTooltip("%s", _("Can't be dragged or resized on the canvas. The fields here still change it."));
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
