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
#include <functional>
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
static int gStillWait;	// frames the world streams in before it is taken, after a move
static rw::Texture *gStillFrame;
static std::vector<std::string> gRecent;
static const int MAX_RECENT_TEXTDRAW_FILES = 8;

// The canvas: the player's screen shape, zoom and pan, the grid and snapping.
enum { TD_SCREEN_WINDOW, TD_SCREEN_4_3, TD_SCREEN_16_10, TD_SCREEN_16_9, TD_SCREEN_21_9, NUM_TD_SCREENS };
static const float gScreenAspects[NUM_TD_SCREENS] = { 0.0f, 4.0f / 3.0f, 16.0f / 10.0f, 16.0f / 9.0f, 21.0f / 9.0f };
static const char *gScreenNames[NUM_TD_SCREENS] = { N_("Window"), "4:3", "16:10", "16:9", "21:9" };
static int gScreen = TD_SCREEN_WINDOW;
static float gZoom = 1.0f, gPanX, gPanY;	// the pan in pixels, from the centred canvas
static bool gSnap = true, gGrid, gCompare;
static float gGridSize = 10.0f;
static float gNudge = 1.0f;	// the arrow keys' step; Shift ten times it
static bool gClickThroughLocked = true;
// Selection: gSel in file order; gSelected is the one the panel edits.
static std::vector<int> gSel;
// The artboard on the canvas, and boards made but still empty.
static std::string gBoard;
static std::vector<std::string> gEmptyBoards;
// Colours the user saved, shared by every colour field (and kept in the settings).
static std::vector<unsigned int> gSavedColours;
static std::vector<std::string> gFavSprites;	// "txd:texture", starred in the sprite picker
// The eyedropper: a colour field asks, the next click on the canvas answers
// with the reference image's colour there.
static ImGuiID gPickFor;
static bool gPicking, gPicked;
static unsigned int gPickedColour;

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
	return gMode && !gStillPending && gStillWait <= 0;
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
TextDrawsAfterWorldRender(void)
{
	// After a camera preset: the world is drawn (under the canvas) for a
	// while so its models stream in, then the still frame is taken.
	if(gStillWait > 0 && --gStillWait == 0)
		gStillPending = true;
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
	else if(strcmp(key, "textdraw_saved_colours") == 0){
		gSavedColours.clear();
		for(const char *p = value; *p; ){
			char *end;
			unsigned long c = strtoul(p, &end, 16);
			if(end == p)
				break;
			gSavedColours.push_back((unsigned int)c);
			p = end;
		}
	}else if(strcmp(key, "textdraw_favourite_sprites") == 0){
		gFavSprites.clear();
		std::string v = value;
		for(size_t at = 0; at < v.size(); ){
			size_t end = v.find(' ', at);
			if(end == std::string::npos) end = v.size();
			if(end > at)
				gFavSprites.push_back(v.substr(at, end - at));
			at = end + 1;
		}
	}else if(strcmp(key, "textdraw_canvas") == 0){
		int snap = 1, grid = 0, through = 1, compare = 0;
		sscanf(value, "%d %d %d %f %f %d %d", &gScreen, &snap, &grid, &gGridSize, &gNudge, &through, &compare);
		gScreen = std::max(0, std::min(gScreen, NUM_TD_SCREENS - 1));
		gSnap = snap != 0;
		gGrid = grid != 0;
		gClickThroughLocked = through != 0;
		gCompare = compare != 0;
		gGridSize = std::max(1.0f, std::min(gGridSize, 200.0f));
		gNudge = std::max(0.01f, std::min(gNudge, 100.0f));
	}
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
	fputs("textdraw_favourite_sprites", f);
	for(const std::string &fav : gFavSprites)
		fprintf(f, " %s", fav.c_str());
	fputc('\n', f);
	fputs("textdraw_saved_colours", f);
	for(unsigned int c : gSavedColours)
		fprintf(f, " %08X", c);
	fputc('\n', f);
	fprintf(f, "textdraw_canvas %d %d %d %g %g %d %d\n", gScreen, gSnap ? 1 : 0, gGrid ? 1 : 0, gGridSize, gNudge,
		gClickThroughLocked ? 1 : 0, gCompare ? 1 : 0);
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
	if(td.font == TD_FONT_SPRITE || td.font == TD_FONT_PREVIEW || td.font == TD_FONT_REFERENCE){
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

// The free area between the docked panels, under the canvas toolbar.
static float gToolbarHeight;	// measured when the toolbar is drawn
static void
canvasArea(ImVec2 *pos, ImVec2 *size)
{
	ImGuiViewport *vp = ImGui::GetMainViewport();
	*pos = vp->WorkPos;
	*size = vp->WorkSize;
#ifdef IMGUI_HAS_DOCK
	ImGuiDockNode *central = ImGui::DockBuilderGetCentralNode(ImHashStr("HaumeaDockSpace"));
	if(central && central->Size.x > 32.0f && central->Size.y > 32.0f){
		*pos = central->Pos;
		*size = central->Size;
	}
#endif
	pos->y += gToolbarHeight;
	size->y = std::max(32.0f, size->y - gToolbarHeight);
}

// The screen fitted in a rectangle: its shape is the window's, as a
// player's screen would be, or the one picked.
static Canvas
fitCanvas(ImVec2 pos, ImVec2 size, float aspect)
{
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

static float
screenAspect(int screen)
{
	if(screen > TD_SCREEN_WINDOW && screen < NUM_TD_SCREENS)
		return gScreenAspects[screen];
	ImGuiViewport *vp = ImGui::GetMainViewport();
	return vp->Size.x / std::max(1.0f, vp->Size.y);
}

// The canvas as edited: fitted with a margin, then zoomed and panned.
static Canvas
getCanvas(void)
{
	ImVec2 pos, size;
	canvasArea(&pos, &size);
	float margin = ImGui::GetFontSize();
	Canvas c = fitCanvas(ImVec2(pos.x + margin, pos.y + margin),
		ImVec2(size.x - margin * 2.0f, size.y - margin * 2.0f), screenAspect(gScreen));
	float cx = pos.x + size.x * 0.5f, cy = pos.y + size.y * 0.5f;
	c.sx *= gZoom;
	c.sy *= gZoom;
	c.x = cx - 320.0f * c.sx + gPanX;
	c.y = cy - 224.0f * c.sy + gPanY;
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

// ---- Reference images ----

struct RefImage { rw::Image *image; rw::Texture *texture; };
static std::map<std::string, RefImage> gRefImages;	// by file, nil when it can't be read

// PNG, BMP or TGA, read once and kept as 32-bit for the eyedropper.
static const RefImage*
refImage(const std::string &path)
{
	std::map<std::string, RefImage>::iterator it = gRefImages.find(path);
	if(it != gRefImages.end())
		return it->second.texture ? &it->second : nil;
	RefImage ref = { nil, nil };
	std::string low = lowered(path);
	auto ends = [&](const char *ext){ return low.size() > 4 && low.compare(low.size() - 4, 4, ext) == 0; };
	rw::Image *image = ends(".png") ? rw::readPNG(path.c_str()) : ends(".bmp") ? rw::readBMP(path.c_str()) :
		ends(".tga") ? rw::readTGA(path.c_str()) : nil;
	if(image){
		image->unpalettize();
		image->convertTo32();
		rw::Raster *raster = rw::Raster::createFromImage(image);
		if(raster){
			ref.image = image;
			ref.texture = rw::Texture::create(raster);
			ref.texture->setFilter(rw::Texture::LINEAR);
		}else
			image->destroy();
	}
	if(ref.texture == nil && !path.empty())
		log("TextDraws: could not read the reference image %s (PNG, BMP or TGA)\n", path.c_str());
	gRefImages[path] = ref;
	return ref.texture ? &gRefImages[path] : nil;
}

// The image's colour at a point of the canvas, or false when it isn't over it.
static bool
refImageColour(const TextDraw &td, float cx, float cy, unsigned int *rgba)
{
	const RefImage *ref = refImage(td.text);
	if(ref == nil || td.sizeX == 0.0f || td.sizeY == 0.0f)
		return false;
	float u = (cx - td.x) / td.sizeX, v = (cy - td.y) / td.sizeY;	// a negative size mirrors it
	if(u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f)
		return false;
	rw::Image *im = ref->image;
	int px = std::min(im->width - 1, (int)(u * im->width)), py = std::min(im->height - 1, (int)(v * im->height));
	const uint8 *p = im->pixels + py * im->stride + px * 4;
	*rgba = (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | 0xFF;
	return true;
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
	if(td.font == TD_FONT_REFERENCE){
		// Drawn in its colour: the alpha is how much of it shows.
		const RefImage *ref = refImage(td.text);
		ImVec2 uv0(x1 < x0 ? 1.0f : 0.0f, y1 < y0 ? 1.0f : 0.0f), uv1(1.0f - uv0.x, 1.0f - uv0.y);
		if(x1 < x0) std::swap(x0, x1);
		if(y1 < y0) std::swap(y0, y1);
		if(ref)
			dl->AddImage((ImTextureID)ref->texture, cv.at(x0, y0), cv.at(x1, y1), uv0, uv1, rgbaToImU32(td.colour));
		else{
			dl->AddRect(cv.at(x0, y0), cv.at(x1, y1), IM_COL32(255, 80, 80, 200));
			dl->AddText(cv.at(x0 + 2.0f, y0 + 2.0f), IM_COL32(255, 120, 120, 255), td.text.empty() ? "?" : td.text.c_str());
		}
		return;
	}
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

// ---- Selection and editing ----

static bool
onBoard(const TextDraw &td)
{
	return td.board == gBoard;
}

// Drawn on the canvas: on this board and not hidden.
static bool
shown(const TextDraw &td)
{
	return onBoard(td) && !td.hidden;
}

static bool
isSelected(int i)
{
	return std::binary_search(gSel.begin(), gSel.end(), i);
}

static void
selectOnly(int i)
{
	gSel.clear();
	if(i >= 0)
		gSel.push_back(i);
	gSelected = i;
}

static void
addToSelection(int i)
{
	std::vector<int>::iterator it = std::lower_bound(gSel.begin(), gSel.end(), i);
	if(it == gSel.end() || *it != i)
		gSel.insert(it, i);
	gSelected = i;
}

static void
toggleSelected(int i)
{
	std::vector<int>::iterator it = std::lower_bound(gSel.begin(), gSel.end(), i);
	if(it != gSel.end() && *it == i){
		gSel.erase(it);
		if(gSelected == i)
			gSelected = gSel.empty() ? -1 : gSel.back();
	}else
		addToSelection(i);
}

// After an undo, a delete or a board switch: only what exists and is on the board.
static void
validateSelection(void)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	std::vector<int> keep;
	for(int i : gSel)
		if(i >= 0 && i < (int)tds.size() && onBoard(tds[i]))
			keep.push_back(i);
	gSel.swap(keep);
	if(!isSelected(gSelected))
		gSelected = gSel.empty() ? -1 : gSel.back();
}

void
SelectTextDraw(int index)
{
	EnterTextDrawMode();
	std::vector<TextDraw> &tds = GetTextDraws();
	if(index >= 0 && index < (int)tds.size())
		gBoard = tds[index].board;
	selectOnly(index);
}

// Canvas-unit bounds, smallest corner first (mirrored sprites have them the other way).
static void
boundsOf(const TextDraw &td, float *x0, float *y0, float *x1, float *y1)
{
	textDrawBounds(td, x0, y0, x1, y1);
	if(*x1 < *x0) std::swap(*x0, *x1);
	if(*y1 < *y0) std::swap(*y0, *y1);
}

// Moved as a whole: a text's wrap or box edge (TextSize's x, a position on
// the screen for left and right alignment) moves with it.
static void
moveBy(TextDraw &td, float dx, float dy)
{
	td.x += dx;
	td.y += dy;
	bool isText = td.font < TD_FONT_SPRITE;
	if(isText && (td.alignment == 1 || td.alignment == 3))
		td.sizeX += dx;
}

static void
deleteSelected(void)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	for(int k = (int)gSel.size() - 1; k >= 0; k--)
		tds.erase(tds.begin() + gSel[k]);
	selectOnly(-1);
	TextDrawsChanged();
}

// Copies after the last one, offset unless in place; the copies end up selected.
static void
appendCopies(const std::vector<TextDraw> &copies, float offset)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	gSel.clear();
	for(TextDraw td : copies){
		td.name[0] = '\0';
		td.board = gBoard;
		moveBy(td, offset, offset);
		tds.push_back(td);
		gSel.push_back((int)tds.size() - 1);
	}
	gSelected = gSel.empty() ? -1 : gSel.back();
	TextDrawsChanged();
}

static std::vector<TextDraw>
selectedCopies(void)
{
	std::vector<TextDraw> out;
	std::vector<TextDraw> &tds = GetTextDraws();
	for(int i : gSel)
		out.push_back(tds[i]);
	return out;
}

static std::vector<TextDraw> gClipboard;

// One step towards the top (later in the file) or the bottom, past the
// textdraws that aren't selected.
static void
restackSelected(bool up)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	bool moved = false;
	if(up){
		for(int k = (int)gSel.size() - 1; k >= 0; k--){
			int i = gSel[k];
			if(i + 1 < (int)tds.size() && !isSelected(i + 1)){
				std::swap(tds[i], tds[i + 1]);
				gSel[k] = i + 1;
				if(gSelected == i) gSelected = i + 1;
				moved = true;
			}
		}
	}else{
		for(size_t k = 0; k < gSel.size(); k++){
			int i = gSel[k];
			if(i > 0 && !isSelected(i - 1)){
				std::swap(tds[i], tds[i - 1]);
				gSel[k] = i - 1;
				if(gSelected == i) gSelected = i - 1;
				moved = true;
			}
		}
	}
	if(moved)
		TextDrawsChanged();
}

// The selection moved to before file index `at` (tds.size() for the end),
// in its order; it stays selected.
static void
moveSelectionTo(int at)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	if(gSel.empty())
		return;
	std::vector<TextDraw> moving, rest;
	int primary = -1, before = 0;
	for(int i = 0; i < (int)tds.size(); i++){
		if(isSelected(i)){
			if(i == gSelected) primary = (int)moving.size();
			moving.push_back(tds[i]);
		}else{
			if(i < at) before++;
			rest.push_back(tds[i]);
		}
	}
	rest.insert(rest.begin() + before, moving.begin(), moving.end());
	tds.swap(rest);
	gSel.clear();
	for(int k = 0; k < (int)moving.size(); k++)
		gSel.push_back(before + k);
	gSelected = primary >= 0 ? before + primary : gSel.back();
	TextDrawsChanged();
}

// A group is one name on its textdraws, kept together in the file.
static void
groupSelected(const char *name)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	for(int i : gSel)
		tds[i].group = name;
	if(!gSel.empty())
		moveSelectionTo(gSel.front());
	TextDrawsChanged();
}

static void
selectGroup(const std::string &group, bool add)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	if(!add)
		selectOnly(-1);
	for(int i = 0; i < (int)tds.size(); i++)
		if(onBoard(tds[i]) && tds[i].group == group)
			addToSelection(i);
}

static std::string
newGroupName(void)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	for(int n = 1; ; n++){
		std::string name = std::string(_("Group")) + " " + std::to_string(n);
		bool used = false;
		for(const TextDraw &td : tds)
			used |= td.group == name;
		if(!used)
			return name;
	}
}

// Align to the selection's edges, or to the screen's with one selected.
enum { ALIGN_LEFT, ALIGN_HCENTRE, ALIGN_RIGHT, ALIGN_TOP, ALIGN_VCENTRE, ALIGN_BOTTOM };
static void
alignSelected(int how, bool toScreen)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	float X0 = 1e9f, Y0 = 1e9f, X1 = -1e9f, Y1 = -1e9f;
	for(int i : gSel){
		float x0, y0, x1, y1;
		boundsOf(tds[i], &x0, &y0, &x1, &y1);
		X0 = std::min(X0, x0); Y0 = std::min(Y0, y0);
		X1 = std::max(X1, x1); Y1 = std::max(Y1, y1);
	}
	if(toScreen || gSel.size() == 1){
		X0 = 0.0f; Y0 = 0.0f; X1 = 640.0f; Y1 = 448.0f;
	}
	for(int i : gSel){
		TextDraw &td = tds[i];
		if(td.locked)
			continue;
		float x0, y0, x1, y1;
		boundsOf(td, &x0, &y0, &x1, &y1);
		float dx = 0.0f, dy = 0.0f;
		switch(how){
		case ALIGN_LEFT: dx = X0 - x0; break;
		case ALIGN_HCENTRE: dx = (X0 + X1) * 0.5f - (x0 + x1) * 0.5f; break;
		case ALIGN_RIGHT: dx = X1 - x1; break;
		case ALIGN_TOP: dy = Y0 - y0; break;
		case ALIGN_VCENTRE: dy = (Y0 + Y1) * 0.5f - (y0 + y1) * 0.5f; break;
		case ALIGN_BOTTOM: dy = Y1 - y1; break;
		}
		moveBy(td, dx, dy);
	}
	TextDrawsChanged();
}

// Equal gaps between three or more, the outer ones staying.
static void
distributeSelected(bool across)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	if(gSel.size() < 3)
		return;
	struct Item { int i; float a, b; };
	std::vector<Item> items;
	float total = 0.0f;
	for(int i : gSel){
		float x0, y0, x1, y1;
		boundsOf(tds[i], &x0, &y0, &x1, &y1);
		Item it = { i, across ? x0 : y0, across ? x1 : y1 };
		items.push_back(it);
		total += it.b - it.a;
	}
	std::sort(items.begin(), items.end(), [](const Item &p, const Item &q){ return p.a < q.a; });
	float span = items.back().b - items.front().a;
	float gap = (span - total) / (float)(items.size() - 1);
	float at = items.front().a;
	for(const Item &it : items){
		if(!tds[it.i].locked)
			moveBy(tds[it.i], across ? at - it.a : 0.0f, across ? 0.0f : at - it.a);
		at += it.b - it.a + gap;
	}
	TextDrawsChanged();
}

// ---- Artboards ----

// The boards in order: the file's, then those made but still empty.
static void
listBoards(std::vector<std::string> &out)
{
	TextDrawBoards(GetTextDraws(), out);
	if(out.empty())
		out.push_back(std::string());
	for(const std::string &b : gEmptyBoards)
		if(std::find(out.begin(), out.end(), b) == out.end())
			out.push_back(b);
	if(std::find(out.begin(), out.end(), gBoard) == out.end())
		out.push_back(gBoard);
}

static const char*
boardLabel(const std::string &board)
{
	return board.empty() ? _("Main") : board.c_str();
}

static void
switchBoard(const std::string &board)
{
	gBoard = board;
	selectOnly(-1);
}

static void
renameBoard(const std::string &from, const std::string &to)
{
	if(from == to || to.empty())
		return;
	for(TextDraw &td : GetTextDraws())
		if(td.board == from)
			td.board = to;
	std::replace(gEmptyBoards.begin(), gEmptyBoards.end(), from, to);
	if(gBoard == from)
		gBoard = to;
	TextDrawsChanged();
}

static std::string
newBoardName(const char *base)
{
	std::vector<std::string> boards;
	listBoards(boards);
	for(int n = 1; ; n++){
		std::string name = std::string(base) + (n > 1 ? " " + std::to_string(n) : "");
		if(std::find(boards.begin(), boards.end(), name) == boards.end())
			return name;
	}
}

static void
duplicateBoard(const std::string &board)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	std::string copy = newBoardName((std::string(boardLabel(board)) + " " + _("copy")).c_str());
	size_t n = tds.size();
	for(size_t i = 0; i < n; i++)
		if(tds[i].board == board){
			TextDraw td = tds[i];
			td.name[0] = '\0';
			td.board = copy;
			tds.push_back(td);
		}
	gEmptyBoards.push_back(copy);
	switchBoard(copy);
	TextDrawsChanged();
}

static void
deleteBoard(const std::string &board)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	tds.erase(std::remove_if(tds.begin(), tds.end(), [&](const TextDraw &td){ return td.board == board; }), tds.end());
	gEmptyBoards.erase(std::remove(gEmptyBoards.begin(), gEmptyBoards.end(), board), gEmptyBoards.end());
	std::vector<std::string> boards;
	listBoards(boards);
	boards.erase(std::remove(boards.begin(), boards.end(), board), boards.end());
	switchBoard(boards.empty() ? std::string() : boards.front());
	TextDrawsChanged();
}

// ---- The canvas ----

// Dragging the selection: where it started, and each one as it was.
static bool gDragging, gDragMoved;
static ImVec2 gDragStart;
static std::vector<std::pair<int, TextDraw>> gDragFrom;
static bool gResizing;
// The box drawn on empty canvas to select what it touches.
static bool gMarquee, gMarqueeAdd;
static ImVec2 gMarqueeStart;
static bool gPanning;

static void
startDrag(void)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	gDragging = true;
	gDragMoved = false;
	gDragStart = ImGui::GetIO().MousePos;
	gDragFrom.clear();
	for(int i : gSel)
		if(!tds[i].locked)
			gDragFrom.push_back(std::make_pair(i, tds[i]));
}

// The textdraws under a point, the top one first.
static void
textDrawsAt(const Canvas &cv, ImVec2 p, std::vector<int> &out)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	out.clear();
	for(int i = (int)tds.size() - 1; i >= 0; i--){
		if(!shown(tds[i]))
			continue;
		float x0, y0, x1, y1;
		boundsOf(tds[i], &x0, &y0, &x1, &y1);
		ImVec2 a = cv.at(x0, y0), b = cv.at(x1, y1);
		b.x = std::max(b.x, a.x + 6.0f);
		b.y = std::max(b.y, a.y + 6.0f);
		if(p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y)
			out.push_back(i);
	}
}

// Snaps the moved selection's edges and centre to the screen's, the grid's
// and the other textdraws'. Returns the correction; guides are drawn.
static float
snapAxis(const float edges[3], const std::vector<float> &targets, float reach, float *guide)
{
	float best = reach, fix = 0.0f;
	for(float t : targets)
		for(int e = 0; e < 3; e++)
			if(fabsf(t - edges[e]) < best){
				best = fabsf(t - edges[e]);
				fix = t - edges[e];
				*guide = t;
			}
	return fix;
}

static void
dragSelection(const Canvas &cv, ImDrawList *dl)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	ImGuiIO &io = ImGui::GetIO();
	ImVec2 d(io.MousePos.x - gDragStart.x, io.MousePos.y - gDragStart.y);
	if(!gDragMoved && d.x * d.x + d.y * d.y < 9.0f)
		return;
	gDragMoved = true;
	float dx = d.x / cv.sx, dy = d.y / cv.sy;
	if(gSnap && !io.KeyCtrl && !gDragFrom.empty()){
		float X0 = 1e9f, Y0 = 1e9f, X1 = -1e9f, Y1 = -1e9f;
		for(const auto &f : gDragFrom){
			float x0, y0, x1, y1;
			boundsOf(f.second, &x0, &y0, &x1, &y1);
			X0 = std::min(X0, x0); Y0 = std::min(Y0, y0);
			X1 = std::max(X1, x1); Y1 = std::max(Y1, y1);
		}
		std::vector<float> xs = { 0.0f, 320.0f, 640.0f }, ys = { 0.0f, 224.0f, 448.0f };
		for(int i = 0; i < (int)tds.size(); i++){
			if(isSelected(i) || !shown(tds[i]))
				continue;
			float x0, y0, x1, y1;
			boundsOf(tds[i], &x0, &y0, &x1, &y1);
			xs.push_back(x0); xs.push_back((x0 + x1) * 0.5f); xs.push_back(x1);
			ys.push_back(y0); ys.push_back((y0 + y1) * 0.5f); ys.push_back(y1);
		}
		if(gGrid)
			for(float g = 0.0f; g <= 640.0f; g += gGridSize){
				xs.push_back(g);
				if(g <= 448.0f) ys.push_back(g);
			}
		float ex[3] = { X0 + dx, (X0 + X1) * 0.5f + dx, X1 + dx };
		float ey[3] = { Y0 + dy, (Y0 + Y1) * 0.5f + dy, Y1 + dy };
		float gx = -1.0f, gy = -1.0f;
		dx += snapAxis(ex, xs, 6.0f / cv.sx, &gx);
		dy += snapAxis(ey, ys, 6.0f / cv.sy, &gy);
		ImU32 guide = IM_COL32(255, 70, 200, 220);
		if(gx >= 0.0f)
			dl->AddLine(cv.at(gx, 0.0f), cv.at(gx, 448.0f), guide);
		if(gy >= 0.0f)
			dl->AddLine(cv.at(0.0f, gy), cv.at(640.0f, gy), guide);
	}
	for(const auto &f : gDragFrom){
		TextDraw td = f.second;
		moveBy(td, dx, dy);
		tds[f.first] = td;
	}
	TextDrawsChanged();
	if(gSelected >= 0)
		ImGui::SetTooltip("%.1f, %.1f", tds[gSelected].x, tds[gSelected].y);
}

static void
drawGrid(ImDrawList *dl, const Canvas &cv)
{
	if(!gGrid || gGridSize * cv.sx < 4.0f)
		return;
	ImU32 line = IM_COL32(255, 255, 255, 28);
	for(float x = gGridSize; x < 640.0f; x += gGridSize)
		dl->AddLine(cv.at(x, 0.0f), cv.at(x, 448.0f), line);
	for(float y = gGridSize; y < 448.0f; y += gGridSize)
		dl->AddLine(cv.at(0.0f, y), cv.at(640.0f, y), line);
	dl->AddLine(cv.at(320.0f, 0.0f), cv.at(320.0f, 448.0f), IM_COL32(255, 255, 255, 50));
	dl->AddLine(cv.at(0.0f, 224.0f), cv.at(640.0f, 224.0f), IM_COL32(255, 255, 255, 50));
}

// The screen as a player sees it: background, HUD, the map's textdraws, the board's.
static void
drawScreen(ImDrawList *dl, const Canvas &cv)
{
	if(gBackground == TD_BACKGROUND_STILL && gStillFrame)
		dl->AddImage((ImTextureID)gStillFrame, cv.at(0, 0), cv.at(640, 448));
	else
		dl->AddRectFilled(cv.at(0, 0), cv.at(640, 448), rgbaToImU32(gCanvasColour));
	if(gHud.show)
		drawHud(dl, cv);
	const std::vector<TextDraw> &mapTds = GetMapTextDraws();
	for(size_t i = 0; i < mapTds.size(); i++)
		drawTextDraw(dl, cv, mapTds[i]);
	const std::vector<TextDraw> &tds = GetTextDraws();
	for(size_t i = 0; i < tds.size(); i++)
		if(shown(tds[i]))
			drawTextDraw(dl, cv, tds[i]);
}

// Small views of the board on other screen shapes, in the canvas's corner.
static void
drawCompare(ImDrawList *dl)
{
	ImVec2 pos, size;
	canvasArea(&pos, &size);
	static const int shapes[] = { TD_SCREEN_4_3, TD_SCREEN_16_9, TD_SCREEN_21_9 };
	float h = std::max(60.0f, size.y * 0.18f), pad = ImGui::GetFontSize() * 0.5f;
	float x = pos.x + size.x - pad;
	for(int k = IM_ARRAYSIZE(shapes) - 1; k >= 0; k--){
		float w = h * gScreenAspects[shapes[k]];
		x -= w;
		ImVec2 p(x, pos.y + size.y - pad - h);
		Canvas cv = fitCanvas(p, ImVec2(w, h), gScreenAspects[shapes[k]]);
		dl->AddRectFilled(ImVec2(p.x - 3, p.y - 3 - ImGui::GetFontSize()), ImVec2(p.x + w + 3, p.y + h + 3),
			IM_COL32(20, 22, 26, 230), 4.0f);
		dl->PushClipRect(cv.at(0, 0), cv.at(640, 448), true);
		drawScreen(dl, cv);
		dl->PopClipRect();
		dl->AddText(ImVec2(p.x, p.y - ImGui::GetFontSize() - 1), IM_COL32(220, 220, 220, 255), gScreenNames[shapes[k]]);
		x -= pad * 2.0f;
	}
}

// The canvas: the still frame or a plain colour where the world was, the
// map's read-only textdraws, then the board's, which are edited here.
void
DrawTextDrawsOverlay(void)
{
	if(!gMode)
		return;
	std::vector<TextDraw> &tds = GetTextDraws();
	ImGuiIO &io = ImGui::GetIO();
	loadFonts();
	validateSelection();
	Canvas cv = getCanvas();
	ImVec2 areaPos, areaSize;
	canvasArea(&areaPos, &areaSize);
	ImVec2 areaEnd(areaPos.x + areaSize.x, areaPos.y + areaSize.y);
	ImDrawList *dl = ImGui::GetBackgroundDrawList();
	ImGuiViewport *vp = ImGui::GetMainViewport();
	dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(24, 26, 30, 255));
	dl->PushClipRect(areaPos, areaEnd, true);
	drawScreen(dl, cv);
	const std::vector<TextDraw> &mapTds = GetMapTextDraws();
	for(size_t i = 0; i < mapTds.size(); i++){
		float x0, y0, x1, y1;
		boundsOf(mapTds[i], &x0, &y0, &x1, &y1);
		dl->AddRect(cv.at(x0, y0), cv.at(x1, y1), IM_COL32(160, 160, 170, 120));
	}
	drawGrid(dl, cv);
	// The screen's edge, so textdraws can be placed against it.
	dl->AddRect(cv.at(0, 0), cv.at(640, 448), IM_COL32(255, 255, 255, 60));

	// Invisible windows over the textdraws take the clicks and drags; locked
	// ones let them through to what is under them (unless that is turned off).
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
#ifdef IMGUI_HAS_DOCK
	flags |= ImGuiWindowFlags_NoDocking;
#endif
	int hovered = -1;
	if(gPicking){
		// The eyedropper: the top image layer under the click gives its colour.
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		ImVec2 m = io.MousePos;
		bool inside = m.x >= areaPos.x && m.y >= areaPos.y && m.x < areaEnd.x && m.y < areaEnd.y;
		float cx = (m.x - cv.x) / cv.sx, cy = (m.y - cv.y) / cv.sy;
		unsigned int under = 0;
		bool over = false;
		for(int i = (int)tds.size() - 1; i >= 0 && !over; i--)
			if(shown(tds[i]) && tds[i].font == TD_FONT_REFERENCE)
				over = refImageColour(tds[i], cx, cy, &under);
		if(inside && !io.WantCaptureMouse){
			if(over){
				ImGui::BeginTooltip();
				ImGui::ColorButton("##under", ImVec4((under >> 24 & 0xFF) / 255.0f, (under >> 16 & 0xFF) / 255.0f,
					(under >> 8 & 0xFF) / 255.0f, 1.0f));
				ImGui::SameLine();
				ImGui::Text("0x%08X", under);
				ImGui::EndTooltip();
			}
			if(ImGui::IsMouseClicked(0)){
				if(over){
					gPickedColour = under & 0xFFFFFF00u;
					gPicked = true;
				}else
					Toast(TOAST_SELECTION, "%s", _("Click on an image layer to take its colour"));
				gPicking = !over;
			}
		}
		if(ImGui::IsKeyPressed(ImGuiKey_Escape))
			gPicking = false;
	}
	for(int i = (int)tds.size() - 1; i >= 0 && !gPicking; i--){
		TextDraw &td = tds[i];
		if(!shown(td) || (td.locked && gClickThroughLocked))
			continue;
		float x0, y0, x1, y1;
		boundsOf(td, &x0, &y0, &x1, &y1);
		ImVec2 p0 = cv.at(x0, y0), p1 = cv.at(x1, y1);
		if(p1.x - p0.x < 6.0f) p1.x = p0.x + 6.0f;
		if(p1.y - p0.y < 6.0f) p1.y = p0.y + 6.0f;
		// Only the part inside the area, so it never covers the panels.
		p0.x = std::max(p0.x, areaPos.x); p0.y = std::max(p0.y, areaPos.y);
		p1.x = std::min(p1.x, areaEnd.x); p1.y = std::min(p1.y, areaEnd.y);
		if(p1.x - p0.x < 1.0f || p1.y - p0.y < 1.0f)
			continue;
		char id[32];
		snprintf(id, sizeof(id), "##tdhit%d", i);
		ImGui::SetNextWindowPos(p0);
		ImGui::SetNextWindowSize(ImVec2(p1.x - p0.x, p1.y - p0.y));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
		ImGui::Begin(id, nil, flags);
		ImGui::InvisibleButton("hit", ImVec2(p1.x - p0.x, p1.y - p0.y));
		if(ImGui::IsItemActivated()){
			gResizing = false;
			if(io.KeyAlt){
				// Alt+click: the next one down under the cursor, for stacks.
				std::vector<int> under;
				textDrawsAt(cv, io.MousePos, under);
				std::vector<int>::iterator it = std::find(under.begin(), under.end(), gSelected);
				int next = under.empty() ? i : it == under.end() || it + 1 == under.end() ? under.front() : *(it + 1);
				selectOnly(next);
			}else{
				if(io.KeyShift || io.KeyCtrl)
					toggleSelected(i);
				else if(!isSelected(i))
					selectOnly(i);
				else
					gSelected = i;
				if(isSelected(i))
					startDrag();
			}
		}
		if(ImGui::IsItemHovered() && !gDragging){
			hovered = i;
			const char *name = td.name[0] ? td.name : _("textdraw");
			ImGui::SetItemTooltip("%s%s%s", name, td.group.empty() ? "" : "  -  ", td.group.c_str());
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
	}
	if(gDragging){
		if(ImGui::IsMouseDown(0))
			dragSelection(cv, dl);
		else
			gDragging = false;
	}

	// Outlines: what the mouse is over, the selection, the one being edited.
	if(hovered >= 0 && !isSelected(hovered)){
		float x0, y0, x1, y1;
		boundsOf(tds[hovered], &x0, &y0, &x1, &y1);
		dl->AddRect(cv.at(x0, y0), cv.at(x1, y1), IM_COL32(120, 190, 255, 200), 0.0f, 0, 1.0f);
	}
	for(int i : gSel){
		if(!shown(tds[i]))
			continue;
		float x0, y0, x1, y1;
		boundsOf(tds[i], &x0, &y0, &x1, &y1);
		ImU32 c = tds[i].locked ? IM_COL32(200, 200, 200, 200) : i == gSelected ? IM_COL32(255, 220, 60, 255) :
			IM_COL32(255, 220, 60, 150);
		dl->AddRect(cv.at(x0, y0), cv.at(x1, y1), c, 0.0f, 0, i == gSelected ? 1.5f : 1.0f);
	}

	// A handle at the one selected's corner resizes it: letters for text,
	// the box's edge for a box, the size for sprites and models. Dragged
	// past the opposite edge, a sprite's size goes negative and it mirrors.
	if(gSel.size() == 1 && gSelected >= 0 && shown(tds[gSelected]) && !tds[gSelected].locked){
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
			gResizing = true;
			gDragging = false;
		}
		if(ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
		if(gResizing && ImGui::IsItemActive()){
			ImVec2 d = io.MouseDelta;
			float dx = d.x / cv.sx, dy = d.y / cv.sy;
			if(dx != 0.0f || dy != 0.0f){
				if(td.font >= TD_FONT_SPRITE){
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
		gResizing = false;

	// On empty canvas: drag a box to select, click to clear; the wheel zooms
	// round the cursor, the middle button pans.
	ImVec2 m = io.MousePos;
	bool inArea = m.x >= areaPos.x && m.y >= areaPos.y && m.x < areaEnd.x && m.y < areaEnd.y;
	if(inArea && !io.WantCaptureMouse){
		if(ImGui::IsMouseClicked(0) && !gPicking){
			gMarquee = true;
			gMarqueeAdd = io.KeyShift || io.KeyCtrl;
			gMarqueeStart = m;
		}
		if(io.MouseWheel != 0.0f && !io.KeyCtrl){
			float cxU = (m.x - cv.x) / cv.sx, cyU = (m.y - cv.y) / cv.sy;
			gZoom = std::max(0.25f, std::min(gZoom * powf(1.15f, io.MouseWheel), 8.0f));
			Canvas after = getCanvas();
			gPanX += m.x - (after.x + cxU * after.sx);
			gPanY += m.y - (after.y + cyU * after.sy);
		}
		if(ImGui::IsMouseClicked(2))
			gPanning = true;
	}
	if(gPanning){
		if(ImGui::IsMouseDown(2)){
			gPanX += io.MouseDelta.x;
			gPanY += io.MouseDelta.y;
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		}else
			gPanning = false;
	}
	if(gMarquee){
		ImVec2 a(std::min(gMarqueeStart.x, m.x), std::min(gMarqueeStart.y, m.y));
		ImVec2 b(std::max(gMarqueeStart.x, m.x), std::max(gMarqueeStart.y, m.y));
		bool boxed = b.x - a.x > 3.0f || b.y - a.y > 3.0f;
		if(ImGui::IsMouseDown(0)){
			if(boxed){
				dl->AddRectFilled(a, b, IM_COL32(120, 190, 255, 40));
				dl->AddRect(a, b, IM_COL32(120, 190, 255, 200));
			}
		}else{
			gMarquee = false;
			if(!gMarqueeAdd)
				selectOnly(-1);
			if(boxed)
				for(int i = 0; i < (int)tds.size(); i++){
					if(!shown(tds[i]))
						continue;
					float x0, y0, x1, y1;
					boundsOf(tds[i], &x0, &y0, &x1, &y1);
					ImVec2 p = cv.at(x0, y0), q = cv.at(x1, y1);
					if(p.x <= b.x && q.x >= a.x && p.y <= b.y && q.y >= a.y)
						addToSelection(i);
				}
		}
	}

	// Arrow keys nudge the selection while no field has the keyboard.
	ImGuiContext *g = ImGui::GetCurrentContext();
	bool canvasHasKeys = !io.WantTextInput && !ImGui::IsAnyItemActive() &&
		(g->NavWindow == nil || strncmp(g->NavWindow->Name, "##td", 4) == 0);
	if(canvasHasKeys && !gSel.empty()){
		float step = gNudge * (io.KeyShift ? 10.0f : 1.0f);
		float dx = 0.0f, dy = 0.0f;
		if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx -= step;
		if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx += step;
		if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy -= step;
		if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy += step;
		if(dx != 0.0f || dy != 0.0f){
			for(int i : gSel)
				if(!tds[i].locked)
					moveBy(tds[i], dx, dy);
			TextDrawsChanged();
		}
	}
	if(canvasHasKeys && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)){
		selectOnly(-1);
		for(int i = 0; i < (int)tds.size(); i++)
			if(shown(tds[i]))
				addToSelection(i);
	}
	dl->PopClipRect();
	if(gCompare)
		drawCompare(dl);
}

// ---- Window ----

// A colour typed the ways scripts write them: 0xRRGGBBAA, #RRGGBB[AA],
// {RRGGBB}, rgb(r, g, b), rgba(r, g, b, a), "r g b [a]" or "r, g, b[, a]",
// or a decimal like -1. A missing alpha is opaque.
static bool
parseColourText(const char *text, unsigned int *out)
{
	std::string s = text;
	size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
	if(a == std::string::npos)
		return false;
	s = s.substr(a, b - a + 1);
	std::string low = s;
	for(char &c : low)
		c = (char)tolower((unsigned char)c);
	auto hex = [&](std::string h) -> bool {
		if(h.size() != 6 && h.size() != 8)
			return false;
		for(char c : h)
			if(!isxdigit((unsigned char)c))
				return false;
		unsigned int v = (unsigned int)strtoul(h.c_str(), nil, 16);
		*out = h.size() == 6 ? v << 8 | 0xFF : v;
		return true;
	};
	if(low.compare(0, 2, "0x") == 0)
		return hex(low.substr(2));
	if(low[0] == '#')
		return hex(low.substr(1));
	if(low[0] == '{' && low.back() == '}')
		return hex(low.substr(1, low.size() - 2));
	// Numbers: rgb(...), rgba(...), or three or four of them.
	std::string nums = low;
	if(nums.compare(0, 5, "rgba(") == 0) nums = nums.substr(5);
	else if(nums.compare(0, 4, "rgb(") == 0) nums = nums.substr(4);
	for(char &c : nums)
		if(c == ',' || c == '(' || c == ')')
			c = ' ';
	std::vector<long> v;
	char *p = &nums[0], *end;
	while(true){
		long n = strtol(p, &end, 10);
		if(end == p)
			break;
		v.push_back(n);
		p = end;
	}
	while(*p == ' ') p++;
	if(*p != '\0')
		return hex(low);
	if(v.size() == 1){	// a decimal colour, as Pawn prints them (-1 is white)
		*out = (unsigned int)v[0];
		return true;
	}
	if(v.size() != 3 && v.size() != 4)
		return false;
	for(long n : v)
		if(n < 0 || n > 255)
			return false;
	*out = (unsigned int)v[0] << 24 | (unsigned int)v[1] << 16 | (unsigned int)v[2] << 8 |
		(unsigned int)(v.size() == 4 ? v[3] : 255);
	return true;
}

static bool
colourEdit(const char *label, unsigned int *rgba)
{
	bool changed = false;
	float c[4] = { (*rgba >> 24 & 0xFF) / 255.0f, (*rgba >> 16 & 0xFF) / 255.0f,
		(*rgba >> 8 & 0xFF) / 255.0f, (*rgba & 0xFF) / 255.0f };
	ImGui::PushID(label);
	ImGuiID id = ImGui::GetID("##edit");
	if(gPicked && gPickFor == id){
		*rgba = gPickedColour | (*rgba & 0xFF);	// the image's colour, this field's alpha
		gPicked = false;
		changed = true;
	}
	float w = ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x;
	ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 4.0f, w));
	if(ImGui::ColorEdit4("##edit", c, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)){
		*rgba = (unsigned int)(c[0] * 255.0f + 0.5f) << 24 | (unsigned int)(c[1] * 255.0f + 0.5f) << 16 |
			(unsigned int)(c[2] * 255.0f + 0.5f) << 8 | (unsigned int)(c[3] * 255.0f + 0.5f);
		changed = true;
	}
	ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
	if(ImGui::Button(ICON_FA_PAINTBRUSH "##palette", ImVec2(ImGui::GetFrameHeight(), 0)))
		ImGui::OpenPopup("##colours");
	ImGui::SetItemTooltip("%s", _("Saved colours, and a colour typed as 0xRRGGBBAA, #RRGGBB, rgba(...) or numbers"));
	if(ImGui::BeginPopup("##colours")){
		static char typed[64];
		if(ImGui::IsWindowAppearing())
			snprintf(typed, sizeof(typed), "0x%08X", *rgba);
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
		bool enter = ImGui::InputText("##typed", typed, sizeof(typed), ImGuiInputTextFlags_EnterReturnsTrue);
		unsigned int parsed;
		bool ok = parseColourText(typed, &parsed);
		ImGui::SameLine();
		ImGui::BeginDisabled(!ok);
		if(ImGui::Button(L("Use")) || (enter && ok)){
			*rgba = parsed;
			changed = true;
		}
		ImGui::EndDisabled();
		if(!ok && typed[0])
			ImGui::TextDisabled("%s", _("Not a colour"));
		if(ImGui::Button(LI(ICON_FA_DROPLET, "Pick from an image"))){
			gPicking = true;
			gPickFor = id;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemTooltip("%s", _("Then click on an image layer on the canvas (Esc stops)."));
		ImGui::SeparatorText(_("Saved colours"));
		int perRow = 8, remove = -1;
		for(int i = 0; i < (int)gSavedColours.size(); i++){
			unsigned int s = gSavedColours[i];
			ImVec4 v((s >> 24 & 0xFF) / 255.0f, (s >> 16 & 0xFF) / 255.0f, (s >> 8 & 0xFF) / 255.0f, (s & 0xFF) / 255.0f);
			if(i % perRow)
				ImGui::SameLine();
			ImGui::PushID(i);
			if(ImGui::ColorButton("##saved", v, ImGuiColorEditFlags_AlphaPreviewHalf)){
				*rgba = s;
				changed = true;
			}
			if(ImGui::BeginPopupContextItem()){
				if(ImGui::MenuItem(L("Remove")))
					remove = i;
				ImGui::EndPopup();
			}
			ImGui::SetItemTooltip("0x%08X", s);
			ImGui::PopID();
		}
		if(gSavedColours.empty())
			ImGui::TextDisabled("%s", _("None yet."));
		if(remove >= 0){
			gSavedColours.erase(gSavedColours.begin() + remove);
			SaveEditorSettings();
		}
		if(ImGui::Button(LI(ICON_FA_PLUS, "Save this colour")) &&
		   std::find(gSavedColours.begin(), gSavedColours.end(), *rgba) == gSavedColours.end()){
			gSavedColours.push_back(*rgba);
			SaveEditorSettings();
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
	ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
	ImGui::PopID();
	return changed;
}

static void
addTextDraw(int font)
{
	TextDraw td;
	InitTextDraw(&td);
	td.x = 280.0f;
	td.y = 200.0f;
	td.font = font;
	if(font == TD_FONT_REFERENCE){
		td.text = "";
		td.x = 160.0f; td.y = 112.0f;
		td.sizeX = 320.0f; td.sizeY = 224.0f;
		td.colour = 0xFFFFFF99;
		td.locked = false;
	}else if(font == TD_FONT_SPRITE){
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
	td.board = gBoard;
	GetTextDraws().push_back(td);
	selectOnly((int)GetTextDraws().size() - 1);
	TextDrawsChanged();
}

static bool
isFavSprite(const std::string &sprite)
{
	for(const std::string &f : gFavSprites)
		if(lowered(f) == lowered(sprite))
			return true;
	return false;
}

static void
toggleFavSprite(const std::string &sprite)
{
	for(size_t i = 0; i < gFavSprites.size(); i++)
		if(lowered(gFavSprites[i]) == lowered(sprite)){
			gFavSprites.erase(gFavSprites.begin() + i);
			SaveEditorSettings();
			return;
		}
	gFavSprites.push_back(sprite);
	SaveEditorSettings();
}

// The TXDs SA-MP can draw from, searched by name; a texture filter; and
// favourites (right-click a sprite) gathered in their own list at the top.
static void
spritePicker(TextDraw &td)
{
	static std::string txdName;
	static const std::string FAVOURITES = "\x01" "favourites";
	if(txdName.empty()){
		size_t colon = td.text.find(':');
		txdName = colon == std::string::npos ? "LD_BEAT" : td.text.substr(0, colon);
	}
	std::vector<std::string> txds;
	listSpriteTxds(txds);
	std::string shown = txdName == FAVOURITES ? std::string(ICON_FA_STAR " ") + _("Favourites") : txdName;
	if(ImGui::BeginCombo(L("Sprite TXD"), shown.c_str(), ImGuiComboFlags_HeightLarge)){
		static char txdFilter[64];
		if(ImGui::IsWindowAppearing()){
			txdFilter[0] = '\0';
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##txd_filter", _("Search TXDs"), txdFilter, sizeof(txdFilter));
		if(ImGui::Selectable((std::string(ICON_FA_STAR " ") + _("Favourites")).c_str(), txdName == FAVOURITES))
			txdName = FAVOURITES;
		std::string want = lowered(txdFilter);
		for(size_t i = 0; i < txds.size(); i++)
			if(want.empty() || lowered(txds[i]).find(want) != std::string::npos)
				if(ImGui::Selectable(txds[i].c_str(), rw::strcmp_ci(txds[i].c_str(), txdName.c_str()) == 0))
					txdName = txds[i];
		ImGui::EndCombo();
	}
	static char filter[64];
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##sprite_filter", _("Search sprites in it"), filter, sizeof(filter));
	std::string want = lowered(filter);
	// The sprites to show: the TXD's, or the favourites.
	std::vector<std::pair<std::string, rw::Texture*>> sprites;
	if(txdName == FAVOURITES){
		for(const std::string &f : gFavSprites)
			sprites.push_back(std::make_pair(f, spriteTexture(f)));
	}else{
		int slot = spriteTxdSlot(txdName);
		int count = slot >= 0 ? GetTxdTextureCount(slot) : 0;
		for(int i = 0; i < count; i++)
			if(rw::Texture *tex = GetTxdTexture(slot, i))
				sprites.push_back(std::make_pair(txdName + ":" + tex->name, tex));
	}
	ImGui::BeginChild("sprites", ImVec2(0, ImGui::GetFontSize() * 9.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
	float cell = ImGui::GetFontSize() * 3.0f;
	int perRow = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cell + 4.0f))), shownCount = 0;
	for(size_t i = 0; i < sprites.size(); i++){
		const std::string &name = sprites[i].first;
		if(!want.empty() && lowered(name).find(want) == std::string::npos)
			continue;
		if(shownCount++ % perRow)
			ImGui::SameLine();
		ImGui::PushID((int)i);
		bool fav = isFavSprite(name);
		ImVec2 at = ImGui::GetCursorScreenPos();
		bool picked = sprites[i].second ? ImGui::ImageButton("##s", (ImTextureID)sprites[i].second, ImVec2(cell, cell)) :
			ImGui::Button("?##s", ImVec2(cell + ImGui::GetStyle().FramePadding.x * 2.0f, cell + ImGui::GetStyle().FramePadding.y * 2.0f));
		if(picked){
			td.text = name;
			TextDrawsChanged();
		}
		if(fav)
			ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 2.0f, at.y + 1.0f), IM_COL32(255, 210, 60, 255), ICON_FA_STAR);
		if(ImGui::BeginPopupContextItem()){
			if(ImGui::MenuItem(fav ? L("Remove from favourites") : L("Add to favourites")))
				toggleFavSprite(name);
			ImGui::EndPopup();
		}
		ImGui::SetItemTooltip("%s%s", name.c_str(), fav ? "" : _("\nRight-click to keep it in Favourites"));
		ImGui::PopID();
	}
	if(shownCount == 0)
		ImGui::TextDisabled("%s", txdName == FAVOURITES ? _("No favourites yet: right-click a sprite to add it.") :
			_("No sprites match."));
	ImGui::EndChild();
}

// Models for a model preview: objects, vehicles or skins by name or ID,
// with thumbnails as the object browser draws them.
static bool
modelBrowser(int *model)
{
	bool picked = false;
	if(ImGui::Button(LI(ICON_FA_MAGNIFYING_GLASS, "Browse models...")))
		ImGui::OpenPopup("##models");
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 26.0f, ImGui::GetFontSize() * 24.0f), ImGuiCond_Appearing);
	if(ImGui::BeginPopup("##models")){
		static char search[64];
		static int kind;	// all, vehicles, skins, objects
		static int page;
		const char *kinds[] = { _("All"), _("Vehicles"), _("Skins"), _("Objects") };
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
		if(ImGui::Combo("##kind", &kind, kinds, IM_ARRAYSIZE(kinds)))
			page = 0;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if(ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		if(ImGui::InputTextWithHint("##search", _("Name or ID"), search, sizeof(search)))
			page = 0;
		std::string want = lowered(search);
		std::vector<int> ids;
		for(int id = 0; id < NUMOBJECTDEFS && ids.size() < 4000; id++){
			bool vehicle = id >= 400 && id <= 611, skin = id <= 311;
			if((kind == 1 && !vehicle) || (kind == 2 && !skin) || (kind == 3 && (vehicle || skin)))
				continue;
			ObjectDef *obj = GetObjectDef(id);
			if(obj == nil)
				continue;
			if(!want.empty() && lowered(obj->m_name).find(want) == std::string::npos &&
			   std::to_string(id).find(want) != 0)
				continue;
			ids.push_back(id);
		}
		const int perPage = 60;
		int pages = std::max(1, ((int)ids.size() + perPage - 1) / perPage);
		page = std::max(0, std::min(page, pages - 1));
		ImGui::BeginChild("##grid", ImVec2(0, ImGui::GetFontSize() * 18.0f), ImGuiChildFlags_Borders);
		float cell = ImGui::GetFontSize() * 4.0f;
		int perRow = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cell + 8.0f)));
		for(int k = page * perPage, n = 0; k < (int)ids.size() && k < (page + 1) * perPage; k++, n++){
			int id = ids[k];
			if(n % perRow)
				ImGui::SameLine();
			ImGui::PushID(id);
			rw::Texture *thumb = GetObjectThumbnailTexture(id);
			bool clicked = thumb ? ImGui::ImageButton("##m", (ImTextureID)thumb, ImVec2(cell, cell)) :
				ImGui::Button(std::to_string(id).c_str(), ImVec2(cell + ImGui::GetStyle().FramePadding.x * 2.0f,
					cell + ImGui::GetStyle().FramePadding.y * 2.0f));
			if(clicked){
				*model = id;
				picked = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemTooltip("%d  %s", id, GetObjectDef(id)->m_name);
			ImGui::PopID();
		}
		if(ids.empty())
			ImGui::TextDisabled("%s", _("No models match."));
		ImGui::EndChild();
		if(ImGui::SmallButton("<##page") && page > 0) page--;
		ImGui::SameLine();
		ImGui::Text(_("Page %d of %d (%d models)"), page + 1, pages, (int)ids.size());
		ImGui::SameLine();
		if(ImGui::SmallButton(">##page") && page + 1 < pages) page++;
		ImGui::EndPopup();
	}
	return picked;
}

// The panel's settings for what is selected. With several, a change goes to
// all of them: the edited one's value is shown, positions move by the same step.
static void
properties(void)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	TextDraw &td = tds[gSelected];
	const bool many = gSel.size() > 1;
	bool changed = false;
	// Sets the field on every selected textdraw (image layers only move).
	auto all = [&](const std::function<void(TextDraw &)> &set){
		for(int i : gSel)
			if(tds[i].font != TD_FONT_REFERENCE)
				set(tds[i]);
		changed = true;
	};
	bool isText = td.font < TD_FONT_SPRITE;
	if(td.font == TD_FONT_REFERENCE && !many){
		// Not a textdraw: its file, its place and how much of it shows.
		char path[1024];
		snprintf(path, sizeof(path), "%s", td.text.c_str());
		if(ImGui::InputTextWithHint(L("Image file"), _("full path to a PNG, BMP or TGA"), path, sizeof(path),
			ImGuiInputTextFlags_EnterReturnsTrue)){
			td.text = path;
			gRefImages.erase(td.text);	// read it again
			changed = true;
		}
		ImGui::SetItemTooltip("%s", _("Enter loads it. Only the editor shows it: the Pawn has it as a comment."));
		if(const RefImage *ref = refImage(td.text)){
			ImGui::TextDisabled(_("%d x %d pixels"), ref->image->width, ref->image->height);
			ImGui::SameLine();
			if(ImGui::SmallButton(L("Fill the screen"))){
				td.x = 0.0f; td.y = 0.0f; td.sizeX = 640.0f; td.sizeY = 448.0f;
				changed = true;
			}
		}
		float pos[2] = { td.x, td.y }, size[2] = { td.sizeX, td.sizeY };
		if(ImGui::DragFloat2(L("Position"), pos, 0.5f, -640.0f, 1280.0f, "%.1f")){
			td.x = pos[0]; td.y = pos[1];
			changed = true;
		}
		if(ImGui::DragFloat2(L("Size"), size, 0.5f, -1280.0f, 1280.0f, "%.1f")){
			td.sizeX = size[0]; td.sizeY = size[1];
			changed = true;
		}
		int opacity = (int)((td.colour & 0xFF) * 100 / 255);
		if(ImGui::SliderInt(L("Opacity"), &opacity, 0, 100, "%d%%")){
			td.colour = (td.colour & 0xFFFFFF00u) | (unsigned int)(opacity * 255 / 100);
			changed = true;
		}
		if(changed)
			TextDrawsChanged();
		return;
	}
	if(many){
		ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.35f, 1.0f), _("%d selected: changes go to all of them."), (int)gSel.size());
		ImGui::SetItemTooltip("%s", _("The fields show the one clicked last."));
	}

	if(ImGui::BeginTabBar("##td_props")){
		if(ImGui::BeginTabItem(L("Content"))){
			if(!many){
				ImGui::InputText(L("Variable"), td.name, sizeof(td.name));
				if(ImGui::IsItemDeactivatedAfterEdit()) changed = true;
				ImGui::SetItemTooltip("%s", _("The Pawn variable. Empty or not a plain name makes one up."));
			}
			int scope = td.perPlayer ? 1 : 0;
			const char *scopes[] = { _("To everyone (TextDrawCreate)"), _("Per player (CreatePlayerTextDraw)") };
			if(ImGui::Combo(L("Shown"), &scope, scopes, IM_ARRAYSIZE(scopes)))
				all([&](TextDraw &t){ t.perPlayer = scope == 1; });
			static const char *fonts[] = { N_("0 Beckett (gothic)"), N_("1 Aharoni"), N_("2 Bank Gothic (capitals)"),
				N_("3 Pricedown (capitals)"), N_("4 Sprite"), N_("5 Model preview") };
			const char *fontNames[IM_ARRAYSIZE(fonts)];
			for(int i = 0; i < IM_ARRAYSIZE(fonts); i++)
				fontNames[i] = _(fonts[i]);
			int font = td.font;
			if(ImGui::Combo(L("Font"), &font, fontNames, IM_ARRAYSIZE(fonts)))
				all([&](TextDraw &t){ t.font = font; });
			if(td.font == TD_FONT_SPRITE && !many){
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
					std::string text = buf;
					all([&](TextDraw &t){
						if(t.font < TD_FONT_SPRITE)
							t.text = text;
					});
				}
				ImGui::SetItemTooltip("%s", _("~n~ new line, ~r~ ~g~ ~b~ ~y~ ~p~ ~w~ ~l~ colours, ~h~ lighter, _ blank"));
			}else if(td.font == TD_FONT_PREVIEW){
				int model = td.previewModel;
				if(ImGui::InputInt(L("Model"), &model))
					all([&](TextDraw &t){ t.previewModel = model; });
				if(modelBrowser(&model))
					all([&](TextDraw &t){ t.previewModel = model; });
				float rot[3] = { td.previewRotX, td.previewRotY, td.previewRotZ };
				if(ImGui::DragFloat3(L("Rotation"), rot, 1.0f, -360.0f, 360.0f, "%.1f"))
					all([&](TextDraw &t){ t.previewRotX = rot[0]; t.previewRotY = rot[1]; t.previewRotZ = rot[2]; });
				float zoom = td.previewZoom;
				if(ImGui::DragFloat(L("Zoom"), &zoom, 0.01f, 0.1f, 10.0f, "%.2f"))
					all([&](TextDraw &t){ t.previewZoom = zoom; });
				int cols[2] = { td.previewColour1, td.previewColour2 };
				if(ImGui::InputInt2(L("Vehicle colours"), cols))
					all([&](TextDraw &t){ t.previewColour1 = cols[0]; t.previewColour2 = cols[1]; });
			}
			unsigned int colour = td.colour;
			if(colourEdit(td.font == TD_FONT_SPRITE ? L("Tint") : L("Colour"), &colour))
				all([&](TextDraw &t){ t.colour = colour; });
			bool selectable = td.selectable;
			if(ImGui::Checkbox(L("Selectable"), &selectable))
				all([&](TextDraw &t){ t.selectable = selectable; });
			ImGui::SetItemTooltip("%s", _("Players can click it while SelectTextDraw is on (OnPlayerClickTextDraw)."));
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem(L("Layout"))){
			float pos[2] = { td.x, td.y };
			if(ImGui::DragFloat2(L("Position"), pos, 0.5f, -100.0f, 740.0f, "%.2f")){
				float dx = pos[0] - td.x, dy = pos[1] - td.y;
				for(int i : gSel)
					moveBy(tds[i], dx, dy);
				changed = true;
			}
			ImGui::SetItemTooltip("%s", _("On the 640x448 canvas every resolution is scaled to."));
			if(!many){
				float size[2] = { td.sizeX, td.sizeY };
				if(ImGui::DragFloat2(L("Text size"), size, 0.5f, -1280.0f, 1280.0f, "%.2f")){
					td.sizeX = size[0]; td.sizeY = size[1];
					changed = true;
				}
				ImGui::SetItemTooltip("%s", td.font >= TD_FONT_SPRITE ? _("Width and height. A negative one mirrors a sprite.") :
					_("Left: the box's right edge. Centre: width and height are swapped (the second is the width).\n"
					"Right: the box's left edge. Text wraps there."));
			}
			if(isText){
				float letter[2] = { td.letterX, td.letterY };
				if(ImGui::DragFloat2(L("Letter size"), letter, 0.005f, 0.0f, 10.0f, "%.3f"))
					all([&](TextDraw &t){ t.letterX = letter[0]; t.letterY = letter[1]; });
				int align = td.alignment - 1;
				const char *aligns[] = { _("Left"), _("Centre"), _("Right") };
				if(ImGui::Combo(L("Alignment"), &align, aligns, IM_ARRAYSIZE(aligns)))
					all([&](TextDraw &t){ t.alignment = align + 1; });
				bool proportional = td.proportional;
				if(ImGui::Checkbox(L("Proportional"), &proportional))
					all([&](TextDraw &t){ t.proportional = proportional; });
				ImGui::SetItemTooltip("%s", _("Letters take their own width instead of a fixed one."));
			}
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem(L("Style"))){
			if(isText){
				bool box = td.useBox;
				if(ImGui::Checkbox(L("Box"), &box))
					all([&](TextDraw &t){ t.useBox = box; });
				if(td.useBox){
					unsigned int boxColour = td.boxColour;
					if(colourEdit(L("Box colour"), &boxColour))
						all([&](TextDraw &t){ t.boxColour = boxColour; });
				}
				int shadow = td.shadow, outline = td.outline;
				if(ImGui::SliderInt(L("Shadow"), &shadow, 0, 10))
					all([&](TextDraw &t){ t.shadow = shadow; });
				if(ImGui::SliderInt(L("Outline"), &outline, 0, 10))
					all([&](TextDraw &t){ t.outline = outline; });
			}
			unsigned int back = td.backgroundColour;
			if(colourEdit(td.font == TD_FONT_PREVIEW ? L("Background") : L("Shadow/outline colour"), &back))
				all([&](TextDraw &t){ t.backgroundColour = back; });
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
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


// ---- Shapes ----

// Shapes made of several textdraws, as Textdraw Studio builds them: a
// rounded box (LD_SPAC:white bars and LD_BEAT:chit corners), a circle, a
// progress bar and a ring of dots. They are added as a group.
enum { SHAPE_ROUNDED_BOX, SHAPE_CIRCLE, SHAPE_BAR, SHAPE_RING, NUM_SHAPES };
struct ShapeSettings {
	int kind;
	float w, h;	// a circle and a ring use w across
	float radius;	// the rounded box's corners
	float thickness;	// the ring's dots
	int segments;	// the ring's dots
	int value;	// percent, for the bar and the ring
	unsigned int colour, back;
};
static ShapeSettings gShape = { SHAPE_ROUNDED_BOX, 160.0f, 90.0f, 12.0f, 10.0f, 24, 65, 0x2B2F36F0, 0x000000AA };

// LD_BEAT:chit's circle doesn't fill its texture: these are its margins.
static const float CHIT_LEFT = 0.164f, CHIT_RIGHT = 0.164f, CHIT_TOP = 0.158f, CHIT_BOTTOM = 0.17f;

static TextDraw
shapeSprite(const char *sprite, float x, float y, float w, float h, unsigned int colour)
{
	TextDraw td;
	InitTextDraw(&td);
	td.font = TD_FONT_SPRITE;
	td.text = sprite;
	td.x = x; td.y = y;
	td.sizeX = w; td.sizeY = h;
	td.colour = colour;
	td.board = gBoard;
	return td;
}

// A circle of diameter d with its top-left at x, y: the chit, grown by its margins.
static TextDraw
chit(float x, float y, float d, unsigned int colour)
{
	float w = d / (1.0f - CHIT_LEFT - CHIT_RIGHT), h = d / (1.0f - CHIT_TOP - CHIT_BOTTOM);
	return shapeSprite("LD_BEAT:chit", x - w * CHIT_LEFT, y - h * CHIT_TOP, w, h, colour);
}

static void
addShape(void)
{
	const ShapeSettings &sh = gShape;
	std::vector<TextDraw> parts;
	float x = 320.0f - sh.w * 0.5f, y = 224.0f - sh.h * 0.5f;
	const char *name = "Shape";
	switch(sh.kind){
	case SHAPE_ROUNDED_BOX: {
		name = N_("Rounded box");
		float r = std::max(0.0f, std::min(sh.radius, std::min(sh.w, sh.h) * 0.5f));
		parts.push_back(shapeSprite("LD_SPAC:white", x + r, y, sh.w - 2.0f * r, sh.h, sh.colour));
		parts.push_back(shapeSprite("LD_SPAC:white", x, y + r, sh.w, sh.h - 2.0f * r, sh.colour));
		if(r > 0.0f){
			parts.push_back(chit(x, y, 2.0f * r, sh.colour));
			parts.push_back(chit(x + sh.w - 2.0f * r, y, 2.0f * r, sh.colour));
			parts.push_back(chit(x, y + sh.h - 2.0f * r, 2.0f * r, sh.colour));
			parts.push_back(chit(x + sh.w - 2.0f * r, y + sh.h - 2.0f * r, 2.0f * r, sh.colour));
		}
		break;
	}
	case SHAPE_CIRCLE:
		name = N_("Circle");
		parts.push_back(chit(320.0f - sh.w * 0.5f, 224.0f - sh.w * 0.5f, sh.w, sh.colour));
		break;
	case SHAPE_BAR: {
		name = N_("Progress bar");
		parts.push_back(shapeSprite("LD_SPAC:white", x, y, sh.w, sh.h, sh.back));
		float fill = sh.w * std::max(0, std::min(sh.value, 100)) / 100.0f;
		if(fill > 0.0f)
			parts.push_back(shapeSprite("LD_SPAC:white", x, y, fill, sh.h, sh.colour));
		break;
	}
	case SHAPE_RING: {
		// Dots round a circle from the top, clockwise; the first value% lit.
		name = N_("Progress ring");
		int n = std::max(3, std::min(sh.segments, 120));
		float r = (sh.w - sh.thickness) * 0.5f;
		int lit = (int)(n * std::max(0, std::min(sh.value, 100)) / 100.0f + 0.5f);
		for(int k = 0; k < n; k++){
			float a = (float)k / n * 2.0f * 3.14159265f;
			float cx = 320.0f + sinf(a) * r, cy = 224.0f - cosf(a) * r;
			parts.push_back(chit(cx - sh.thickness * 0.5f, cy - sh.thickness * 0.5f, sh.thickness,
				k < lit ? sh.colour : sh.back));
		}
		break;
	}
	}
	if(parts.empty())
		return;
	std::string group = newGroupName();
	group = std::string(_(name)) + group.substr(group.find_last_of(' '));
	std::vector<TextDraw> &tds = GetTextDraws();
	selectOnly(-1);
	for(TextDraw &td : parts){
		td.group = group;
		tds.push_back(td);
		addToSelection((int)tds.size() - 1);
	}
	TextDrawsChanged();
}

static void
shapeButton(void)
{
	if(ImGui::Button(LI(ICON_FA_PLUS, "Shape##add")))
		ImGui::OpenPopup("##shape");
	ImGui::SetItemTooltip("%s", _("A rounded box, a circle, a progress bar or a ring, made of several\n"
		"sprite textdraws and added as a group."));
	if(ImGui::BeginPopup("##shape")){
		static const char *kinds[] = { N_("Rounded box"), N_("Circle"), N_("Progress bar"), N_("Progress ring") };
		const char *names[NUM_SHAPES];
		for(int i = 0; i < NUM_SHAPES; i++)
			names[i] = _(kinds[i]);
		ImGui::PushItemWidth(ImGui::GetFontSize() * 10.0f);
		ImGui::Combo(L("Shape"), &gShape.kind, names, NUM_SHAPES);
		bool round = gShape.kind == SHAPE_CIRCLE || gShape.kind == SHAPE_RING;
		if(round)
			ImGui::DragFloat(L("Diameter"), &gShape.w, 0.5f, 4.0f, 448.0f, "%.1f");
		else{
			float size[2] = { gShape.w, gShape.h };
			if(ImGui::DragFloat2(L("Size"), size, 0.5f, 2.0f, 640.0f, "%.1f")){
				gShape.w = size[0];
				gShape.h = size[1];
			}
		}
		if(gShape.kind == SHAPE_ROUNDED_BOX)
			ImGui::DragFloat(L("Corner radius"), &gShape.radius, 0.25f, 0.0f, 100.0f, "%.1f");
		if(gShape.kind == SHAPE_RING){
			ImGui::DragFloat(L("Dot size"), &gShape.thickness, 0.25f, 1.0f, 60.0f, "%.1f");
			ImGui::SliderInt(L("Dots"), &gShape.segments, 3, 120);
			ImGui::SetItemTooltip("%s", _("Each dot is a textdraw: SA-MP allows 2048 global and 256 per player."));
		}
		if(gShape.kind == SHAPE_BAR || gShape.kind == SHAPE_RING)
			ImGui::SliderInt(L("Value"), &gShape.value, 0, 100, "%d%%");
		ImGui::PopItemWidth();
		colourEdit(L("Colour"), &gShape.colour);
		if(gShape.kind == SHAPE_BAR || gShape.kind == SHAPE_RING)
			colourEdit(L("Empty part"), &gShape.back);
		if(ImGui::Button(LI(ICON_FA_PLUS, "Add the shape"))){
			addShape();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

// ---- Mode ----

// An icon button with a tooltip; label is "ICON###English id".
static bool
iconButton(const char *label, const char *tooltip, bool active = false)
{
	if(active)
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	bool pressed = ImGui::Button(label);
	if(active)
		ImGui::PopStyleColor();
	ImGui::SetItemTooltip("%s", tooltip);
	return pressed;
}

// The artboards as tabs: click to open one, right-click to rename,
// duplicate or delete it, + to add one.
static void
boardTabs(void)
{
	std::vector<std::string> boards;
	listBoards(boards);
	static std::string renaming;
	static char renameBuf[64];
	bool openRename = false;
	static std::string shownBoard = "\x01";	// forces the tab bar onto gBoard after a switch elsewhere
	if(ImGui::BeginTabBar("##boards", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)){
		for(size_t b = 0; b < boards.size(); b++){
			char label[96];
			snprintf(label, sizeof(label), "%s###board%s", boardLabel(boards[b]), boards[b].c_str());
			ImGuiTabItemFlags flags = boards[b] == gBoard && shownBoard != gBoard ? ImGuiTabItemFlags_SetSelected : 0;
			bool open = ImGui::BeginTabItem(label, nil, flags);
			if(ImGui::BeginPopupContextItem()){
				if(ImGui::MenuItem(L("Rename..."))){
					renaming = boards[b];
					snprintf(renameBuf, sizeof(renameBuf), "%s", boardLabel(boards[b]));
					openRename = true;
				}
				if(ImGui::MenuItem(L("Duplicate")))
					duplicateBoard(boards[b]);
				ImGui::BeginDisabled(boards.size() < 2);
				if(ImGui::MenuItem(L("Delete")))
					deleteBoard(boards[b]);
				ImGui::EndDisabled();
				ImGui::EndPopup();
			}
			ImGui::SetItemTooltip("%s", _("An artboard: a screen of its own, shown and hidden in Pawn with\n"
				"<prefix>_Show<Board>(playerid) and _Hide<Board>(playerid). Right-click for more."));
			if(open){
				if(shownBoard == gBoard && boards[b] != gBoard)
					switchBoard(boards[b]);
				ImGui::EndTabItem();
			}
		}
		shownBoard = gBoard;
		if(ImGui::TabItemButton(ICON_FA_PLUS "###add_board", ImGuiTabItemFlags_Trailing)){
			std::string name = newBoardName(_("Board"));
			gEmptyBoards.push_back(name);
			switchBoard(name);
		}
		ImGui::SetItemTooltip("%s", _("New artboard"));
		ImGui::EndTabBar();
	}
	if(openRename)
		ImGui::OpenPopup("##rename_board");
	if(ImGui::BeginPopup("##rename_board")){
		if(ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		bool enter = ImGui::InputText("##name", renameBuf, sizeof(renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if((ImGui::Button(L("Rename")) || enter) && renameBuf[0]){
			std::string to = renameBuf;
			if(to == _("Main") && renaming.empty())
				to.clear();
			renameBoard(renaming, to);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}


// Places to take the still frame at, Textdraw Studio's: a player's spot and
// facing angle; the camera stands behind and above them.
static const struct { const char *name; float x, y, z, angle; } gStillPlaces[] = {
	{ N_("Airport"), 1923.5066f, -2547.8245f, 14.0469f, 90.0f },
	{ N_("Road"), 1714.6931f, -1340.4772f, 13.3828f, 180.1136f },
	{ N_("Grove Street"), 2450.7434f, -1658.5571f, 13.3047f, 258.7504f },
	{ N_("Santa Maria Pier"), 370.1010f, -2032.7593f, 7.6719f, 0.7464f },
	{ N_("Rooftop"), 1547.9363f, -1352.2819f, 329.4693f, 15.2985f },
	{ N_("Beach"), 482.6832f, -1857.9556f, 3.6218f, 248.6140f },
};

static void
takeStillAt(int place, int hour)
{
	if(place >= 0){
		const auto &p = gStillPlaces[place];
		float a = p.angle * 3.14159265f / 180.0f;
		rw::V3d forward = { -sinf(a), cosf(a), 0.0f };	// SA-MP's facing angle, 0 is north
		// Set as Namaka's camera poses are (setTarget would pan the position too).
		rw::V3d up = { 0.0f, 0.0f, 1.0f };
		TheCamera.m_position = { p.x - forward.x * 4.0f, p.y - forward.y * 4.0f, p.z + 1.4f };
		TheCamera.m_target = { p.x + forward.x * 6.0f, p.y + forward.y * 6.0f, p.z + 0.6f };
		TheCamera.m_at = rw::normalize(rw::sub(TheCamera.m_target, TheCamera.m_position));
		TheCamera.m_up = up;
		TheCamera.m_localup = up;
	}
	if(hour >= 0){
		currentHour = hour;
		currentMinute = 0;
	}
	gBackground = TD_BACKGROUND_STILL;
	releaseStillFrame();
	gStillWait = 90;	// a second and a half of streaming
}

static void
stillFramePlaces(void)
{
	if(ImGui::Button(LI(ICON_FA_LOCATION_DOT, "Place")))
		ImGui::OpenPopup("##still_places");
	ImGui::SetItemTooltip("%s", _("Take the still frame somewhere else or at another hour.\n"
		"The map's own camera moves there too."));
	if(ImGui::BeginPopup("##still_places")){
		static int hour = 12;
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
		ImGui::SliderInt(L("Hour"), &hour, 0, 23, "%02d:00");
		ImGui::SeparatorText(_("Places"));
		for(int i = 0; i < IM_ARRAYSIZE(gStillPlaces); i++)
			if(ImGui::MenuItem(_(gStillPlaces[i].name)))
				takeStillAt(i, hour);
		ImGui::Separator();
		if(ImGui::MenuItem(L("Here, at this hour")))
			takeStillAt(-1, hour);
		ImGui::EndPopup();
	}
	if(gStillWait > 0){
		ImGui::SameLine();
		ImGui::TextDisabled("%s", _("Taking it..."));
	}
}

// Above the canvas: the mode and file, the artboards, then how the canvas shows.
static void
drawToolbar(void)
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
	KeepNextEditorOverlayOnMainWindow();
	ImGui::SetNextWindowPos(pos);
	ImGui::SetNextWindowSize(ImVec2(size.x, 0.0f));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoMove;
#ifdef IMGUI_HAS_DOCK
	flags |= ImGuiWindowFlags_NoDocking;
#endif
	if(!ImGui::Begin("##textdraw_toolbar", nil, flags)){
		ImGui::End();
		return;
	}
	ImGui::AlignTextToFramePadding();
	ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f), "%s", TI(ICON_FA_FONT, "TextDraw mode"));
	ImGui::SameLine();
	ImGui::Text("%s%s", GetTextDrawFileName(), TextDrawFileHasUnsavedChanges() ? " *" : "");
	ImGui::SameLine();
	float exitWidth = ImGui::CalcTextSize(LI(ICON_FA_XMARK, "Exit")).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	float tabsWidth = ImGui::GetContentRegionAvail().x - exitWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f;
	ImGui::BeginChild("##boards_area", ImVec2(std::max(60.0f, tabsWidth), ImGui::GetFrameHeight()), ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar);
	boardTabs();
	ImGui::EndChild();
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_XMARK, "Exit")))
		ExitTextDrawMode();
	ImGui::SetItemTooltip(_("Back to the map (%s)"), ShortcutLabel(SC_CANCEL));

	// Second row: the screen, the background, the HUD, then the grid and the zoom.
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.5f);
	const char *screens[NUM_TD_SCREENS];
	for(int i = 0; i < NUM_TD_SCREENS; i++)
		screens[i] = i == TD_SCREEN_WINDOW ? _(gScreenNames[i]) : gScreenNames[i];
	if(ImGui::Combo("##screen", &gScreen, screens, NUM_TD_SCREENS)){
		gZoom = 1.0f; gPanX = gPanY = 0.0f;
		SaveEditorSettings();
	}
	ImGui::SetItemTooltip("%s", _("The player's screen shape. Textdraws stretch with it; the chat doesn't."));
	ImGui::SameLine();
	if(ImGui::Checkbox(L("Compare"), &gCompare))
		SaveEditorSettings();
	ImGui::SetItemTooltip("%s", _("Small views of this board at 4:3, 16:9 and 21:9 in the corner."));
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	int background = gBackground;
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
	const char *backgrounds[] = { _("Plain"), _("Still frame") };
	if(ImGui::Combo("##background", &background, backgrounds, IM_ARRAYSIZE(backgrounds))){
		gBackground = background;
		if(gBackground == TD_BACKGROUND_STILL && gStillFrame == nil)
			gStillPending = true;	// taken from the world on the next frame
		SaveEditorSettings();
	}
	ImGui::SetItemTooltip("%s", _("Still frame: the world as the camera saw it when TextDraw mode started."));
	if(gBackground == TD_BACKGROUND_STILL){
		ImGui::SameLine();
		stillFramePlaces();
	}
	if(gBackground == TD_BACKGROUND_PLAIN){
		ImGui::SameLine();
		float c[4] = { (gCanvasColour >> 24 & 0xFF) / 255.0f, (gCanvasColour >> 16 & 0xFF) / 255.0f,
			(gCanvasColour >> 8 & 0xFF) / 255.0f, 1.0f };
		if(ImGui::ColorEdit3("##canvas", c, ImGuiColorEditFlags_NoInputs)){
			gCanvasColour = (unsigned int)(c[0] * 255.0f + 0.5f) << 24 | (unsigned int)(c[1] * 255.0f + 0.5f) << 16 |
				(unsigned int)(c[2] * 255.0f + 0.5f) << 8 | 0xFF;
			SaveEditorSettings();
		}
	}
	ImGui::SameLine();
	hudSettings();
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	if(ImGui::Checkbox(L("Grid"), &gGrid))
		SaveEditorSettings();
	if(gGrid){
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.0f);
		if(ImGui::DragFloat("##grid", &gGridSize, 0.5f, 1.0f, 200.0f, "%.0f"))
			gGridSize = std::max(1.0f, gGridSize);
		if(ImGui::IsItemDeactivatedAfterEdit())
			SaveEditorSettings();
		ImGui::SetItemTooltip("%s", _("Grid spacing, in canvas units (640 x 448)"));
	}
	ImGui::SameLine();
	if(ImGui::Checkbox(L("Snap"), &gSnap))
		SaveEditorSettings();
	ImGui::SetItemTooltip("%s", _("Dragged textdraws snap to the screen's edges and centre, to the others\n"
		"and to the grid. Hold Ctrl while dragging to place freely."));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.0f);
	if(ImGui::DragFloat("##nudge", &gNudge, 0.05f, 0.01f, 100.0f, ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " %.2f"))
		gNudge = std::max(0.01f, gNudge);
	if(ImGui::IsItemDeactivatedAfterEdit())
		SaveEditorSettings();
	ImGui::SetItemTooltip("%s", _("How far the arrow keys move the selection; Shift moves ten times as far."));
	ImGui::SameLine();
	if(ImGui::Checkbox(L("Click through locked"), &gClickThroughLocked))
		SaveEditorSettings();
	ImGui::SetItemTooltip("%s", _("Clicks on a locked textdraw go to what is under it.\n"
		"Alt+click picks the next one down under the cursor."));
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	if(ImGui::SmallButton("-##zoom")){ gZoom = std::max(0.25f, gZoom / 1.25f); }
	ImGui::SameLine();
	ImGui::Text("%.0f%%", gZoom * 100.0f);
	ImGui::SetItemTooltip("%s", _("The wheel zooms round the cursor, the middle button pans."));
	ImGui::SameLine();
	if(ImGui::SmallButton("+##zoom")){ gZoom = std::min(8.0f, gZoom * 1.25f); }
	ImGui::SameLine();
	if(ImGui::SmallButton(L("Fit"))){ gZoom = 1.0f; gPanX = gPanY = 0.0f; }
	gToolbarHeight = ImGui::GetWindowSize().y;
	ImGui::End();
}

bool
TextDrawModeShortcut(int action)
{
	if(!gMode)
		return false;
	std::vector<TextDraw> &tds = GetTextDraws();
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
	case SC_CANCEL:
		if(!gSel.empty())
			selectOnly(-1);
		else
			ExitTextDrawMode();
		return true;
	case SC_DELETE:
		if(!gSel.empty())
			deleteSelected();
		return true;
	case SC_DUPLICATE:
		if(!gSel.empty())
			appendCopies(selectedCopies(), 10.0f);
		return true;
	case SC_COPY:
		if(!gSel.empty()){
			gClipboard = selectedCopies();
			Toast(TOAST_COPY_PASTE, _("Copied %d textdraws"), (int)gClipboard.size());
		}
		return true;
	case SC_CUT:
		if(!gSel.empty()){
			gClipboard = selectedCopies();
			deleteSelected();
		}
		return true;
	case SC_PASTE:
		if(!gClipboard.empty())
			appendCopies(gClipboard, 10.0f);
		return true;
	case SC_PASTE_IN_PLACE:
		if(!gClipboard.empty())
			appendCopies(gClipboard, 0.0f);
		return true;
	case SC_GROUP_SELECTION:
		if(!gSel.empty())
			groupSelected(newGroupName().c_str());
		return true;
	case SC_HIDE_SELECTION:
		for(int i : gSel)
			tds[i].hidden = true;
		if(!gSel.empty())
			TextDrawsChanged();
		selectOnly(-1);
		return true;
	case SC_SHOW_ALL:
		for(TextDraw &td : tds)
			if(onBoard(td))
				td.hidden = false;
		TextDrawsChanged();
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
	char label[64];
	snprintf(label, sizeof(label), ICON_FA_FILE "###New");
	if(iconButton(label, _("New textdraw file")))
		RequestTextDrawFileClose(newFile);
	ImGui::SameLine();
	if(iconButton(ICON_FA_FOLDER_OPEN "###Open...", _("Open a textdraw file")))
		RequestTextDrawFileClose(showOpenDialog);
	ImGui::SameLine();
	if(iconButton(ICON_FA_FLOPPY_DISK "###Save", ShortcutLabel(SC_SAVE_MAP)))
		saveOrAsk();
	ImGui::SameLine();
	if(iconButton(ICON_FA_FILE_EXPORT "###Save As...", _("Save As...")))
		showFileDialog(true);
	ImGui::SameLine();
	ImGui::BeginDisabled(gRecent.empty());
	if(iconButton(ICON_FA_CLOCK_ROTATE_LEFT "##recent", _("Recent textdraw files")))
		ImGui::OpenPopup("recent");
	ImGui::EndDisabled();
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
	ImGui::SameLine(0.0f, ImGui::GetFontSize());
	if(iconButton(ICON_FA_ROTATE_LEFT "###Undo", ShortcutLabel(SC_UNDO)))
		UndoTextDraws();
	ImGui::SameLine();
	if(iconButton(ICON_FA_ROTATE_RIGHT "###Redo", ShortcutLabel(SC_REDO)))
		RedoTextDraws();
	ImGui::TextDisabled(TextDrawFileHasUnsavedChanges() ? _("Unsaved changes - %s") : _("Saved - %s"),
		GetTextDrawFileName());
	if(GetTextDrawFilePath()[0])
		ImGui::SetItemTooltip(_("%s\nInclude it in your gamemode and call %s_CreateTextDraws()\n"
			"from OnGameModeInit and %s_ShowTextDraws(playerid)."), GetTextDrawFilePath(),
			TextDrawFilePrefix(GetTextDrawFilePath()).c_str(), TextDrawFilePrefix(GetTextDrawFilePath()).c_str());
}

// What a row in the layers shows: its kind and a bit of its text.
static std::string
layerLabel(const TextDraw &td)
{
	if(td.font == TD_FONT_REFERENCE){
		const char *file = td.text.c_str() + (td.text.find_last_of("/\\") == std::string::npos ? 0 :
			td.text.find_last_of("/\\") + 1);
		return std::string(ICON_FA_FILE " ") + _("image") + "  " + file;
	}
	const char *kind = td.font == TD_FONT_SPRITE ? _("sprite") : td.font == TD_FONT_PREVIEW ? _("model") :
		td.useBox ? _("box") : _("text");
	std::string preview = td.name[0] ? std::string(td.name) : td.text.substr(0, 28);
	std::replace(preview.begin(), preview.end(), '\n', ' ');
	return std::string(td.perPlayer ? ICON_FA_USER " " : ICON_FA_GLOBE " ") + kind + "  " + preview;
}

// Small flat toggles at the start of a row: shown and locked.
static bool
rowToggle(const char *on, const char *off, bool value, const char *id, const char *tooltip)
{
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
	if(!value)
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	char label[48];
	snprintf(label, sizeof(label), "%s##%s", value ? on : off, id);
	bool pressed = ImGui::SmallButton(label);
	if(!value)
		ImGui::PopStyleColor();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
	ImGui::SetItemTooltip("%s", tooltip);
	return pressed;
}

static void
rowMenu(int i)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	if(!isSelected(i))
		selectOnly(i);
	if(ImGui::MenuItem(L("Duplicate"), ShortcutLabel(SC_DUPLICATE)))
		appendCopies(selectedCopies(), 10.0f);
	if(ImGui::MenuItem(L("Delete"), ShortcutLabel(SC_DELETE)))
		deleteSelected();
	ImGui::Separator();
	if(ImGui::MenuItem(L("Group"), ShortcutLabel(SC_GROUP_SELECTION)))
		groupSelected(newGroupName().c_str());
	if(ImGui::MenuItem(L("Ungroup"))){
		for(int k : gSel)
			tds[k].group.clear();
		TextDrawsChanged();
	}
	std::vector<std::string> boards;
	listBoards(boards);
	if(boards.size() > 1 && ImGui::BeginMenu(L("Move to artboard"))){
		for(const std::string &b : boards)
			if(b != gBoard && ImGui::MenuItem(boardLabel(b))){
				for(int k : gSel)
					tds[k].board = b;
				selectOnly(-1);
				TextDrawsChanged();
			}
		ImGui::EndMenu();
	}
}

// The board's textdraws, the top one first: groups fold, the eye hides one
// in the editor, the lock keeps it in place; drag a row to restack.
static void
layers(void)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	ImGuiIO &io = ImGui::GetIO();
	static std::vector<std::string> folded;
	ImGui::BeginChild("list", ImVec2(0, ImGui::GetFontSize() * 11.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
	std::string openGroup;	// the group the rows above belong to
	bool any = false;
	int dropAt = -1;
	for(int i = (int)tds.size() - 1; i >= 0; i--){
		TextDraw &td = tds[i];
		if(!onBoard(td))
			continue;
		any = true;
		ImGui::PushID(i);
		if(!td.group.empty() && td.group != openGroup){
			// A group's header: its toggles act on every member.
			openGroup = td.group;
			bool isFolded = std::find(folded.begin(), folded.end(), td.group) != folded.end();
			bool allHidden = true, allLocked = true;
			for(const TextDraw &t : tds)
				if(onBoard(t) && t.group == td.group){
					allHidden &= t.hidden;
					allLocked &= t.locked;
				}
			if(rowToggle(ICON_FA_EYE, ICON_FA_EYE_SLASH, !allHidden, "geye", _("Show or hide the group in the editor"))){
				for(TextDraw &t : tds)
					if(onBoard(t) && t.group == td.group)
						t.hidden = !allHidden;
				TextDrawsChanged();
			}
			ImGui::SameLine();
			if(rowToggle(ICON_FA_LOCK, ICON_FA_LOCK, allLocked, "glock", _("Lock or unlock the group"))){
				for(TextDraw &t : tds)
					if(onBoard(t) && t.group == td.group)
						t.locked = !allLocked;
				TextDrawsChanged();
			}
			ImGui::SameLine();
			char header[96];
			snprintf(header, sizeof(header), "%s " ICON_FA_LAYER_GROUP " %s##group", isFolded ? ">" : "v", td.group.c_str());
			bool all = true;
			for(int k = 0; k < (int)tds.size(); k++)
				if(onBoard(tds[k]) && tds[k].group == td.group)
					all &= isSelected(k);
			if(ImGui::Selectable(header, all, ImGuiSelectableFlags_AllowOverlap)){
				if(io.MousePos.x < ImGui::GetItemRectMin().x + ImGui::GetFontSize()){
					if(isFolded) folded.erase(std::find(folded.begin(), folded.end(), td.group));
					else folded.push_back(td.group);
				}else
					selectGroup(td.group, io.KeyShift || io.KeyCtrl);
			}
			ImGui::SetItemTooltip("%s", _("Click to select the group, on the arrow to fold it. Right-click to rename."));
			if(ImGui::BeginPopupContextItem("##group_menu")){
				static char name[64];
				if(ImGui::IsWindowAppearing())
					snprintf(name, sizeof(name), "%s", td.group.c_str());
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
				if(ImGui::InputText("##rename", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue) && name[0]){
					std::string from = td.group;
					for(TextDraw &t : tds)
						if(t.group == from)
							t.group = name;
					std::replace(folded.begin(), folded.end(), from, std::string(name));
					TextDrawsChanged();
					ImGui::CloseCurrentPopup();
				}
				if(ImGui::MenuItem(L("Ungroup"))){
					std::string from = td.group;
					for(TextDraw &t : tds)
						if(t.group == from)
							t.group.clear();
					TextDrawsChanged();
				}
				ImGui::EndPopup();
			}
			if(isFolded){
				ImGui::PopID();
				while(i > 0 && (!onBoard(tds[i - 1]) || tds[i - 1].group == openGroup))
					i--;
				continue;
			}
		}else if(td.group.empty())
			openGroup.clear();

		if(!td.group.empty())
			ImGui::Indent(ImGui::GetFontSize());
		if(rowToggle(ICON_FA_EYE, ICON_FA_EYE_SLASH, !td.hidden, "eye", _("Shown in the editor (it is in the file either way)"))){
			td.hidden = !td.hidden;
			TextDrawsChanged();
		}
		ImGui::SameLine();
		if(rowToggle(ICON_FA_LOCK, ICON_FA_LOCK, td.locked, "lock", _("Locked: not dragged on the canvas"))){
			td.locked = !td.locked;
			TextDrawsChanged();
		}
		ImGui::SameLine();
		std::string label = layerLabel(td) + "##td" + std::to_string(i);
		if(ImGui::Selectable(label.c_str(), isSelected(i), ImGuiSelectableFlags_AllowOverlap)){
			if(io.KeyCtrl)
				toggleSelected(i);
			else if(io.KeyShift && gSelected >= 0){
				int a = std::min(gSelected, i), b = std::max(gSelected, i);
				for(int k = a; k <= b; k++)
					if(onBoard(tds[k]))
						addToSelection(k);
				gSelected = i;
			}else
				selectOnly(i);
		}
		if(ImGui::BeginPopupContextItem("##row_menu")){
			rowMenu(i);
			ImGui::EndPopup();
		}
		if(ImGui::BeginDragDropSource()){
			if(!isSelected(i))
				selectOnly(i);
			ImGui::SetDragDropPayload("HAUMEA_TD_ROW", &i, sizeof(i));
			ImGui::Text(_("%d textdraws"), (int)gSel.size());
			ImGui::EndDragDropSource();
		}
		if(ImGui::BeginDragDropTarget()){
			if(ImGui::AcceptDragDropPayload("HAUMEA_TD_ROW"))
				dropAt = i + 1;	// above this row: after it in the file
			ImGui::EndDragDropTarget();
		}
		if(!td.group.empty())
			ImGui::Unindent(ImGui::GetFontSize());
		ImGui::PopID();
	}
	if(!any)
		ImGui::TextDisabled("%s", _("No textdraws on this board yet."));
	ImGui::EndChild();
	if(dropAt >= 0)
		moveSelectionTo(dropAt);
}

// What to do with the selection: copies, order, lock, groups, alignment.
static void
selectionActions(void)
{
	std::vector<TextDraw> &tds = GetTextDraws();
	if(ImGui::Button(LI(ICON_FA_COPY, "Duplicate")))
		appendCopies(selectedCopies(), 10.0f);
	ImGui::SetItemTooltip("%s", ShortcutLabel(SC_DUPLICATE));
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_TRASH, "Delete")))
		deleteSelected();
	ImGui::SetItemTooltip("%s", ShortcutLabel(SC_DELETE));
	if(gSel.empty())
		return;
	ImGui::SameLine();
	if(ImGui::Button(L("Up")))
		restackSelected(true);
	ImGui::SetItemTooltip("%s", _("Drawn later, so over the others."));
	ImGui::SameLine();
	if(ImGui::Button(L("Down")))
		restackSelected(false);
	ImGui::SetItemTooltip("%s", _("Drawn earlier, so under the others."));
	bool locked = true;
	for(int i : gSel)
		locked &= tds[i].locked;
	if(ImGui::Checkbox(LI(ICON_FA_LOCK, "Locked"), &locked)){
		for(int i : gSel)
			tds[i].locked = locked;
		TextDrawsChanged();
	}
	ImGui::SetItemTooltip("%s", _("Can't be dragged or resized on the canvas. The fields here still change it."));
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_LAYER_GROUP, "Group")))
		groupSelected(newGroupName().c_str());
	ImGui::SetItemTooltip("%s", _("Gives them one group name and keeps them together in the list.\n"
		"A group hides, locks and is selected as one."));
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_TABLE_COLUMNS, "Align")))
		ImGui::OpenPopup("##align");
	ImGui::SetItemTooltip("%s", _("To each other, or to the screen when one is selected"));
	if(ImGui::BeginPopup("##align")){
		static bool toScreen;
		ImGui::Checkbox(L("To the screen"), &toScreen);
		ImGui::Separator();
		static const char *names[] = { N_("Left edges"), N_("Centres across"), N_("Right edges"),
			N_("Top edges"), N_("Middles"), N_("Bottom edges") };
		for(int k = 0; k < IM_ARRAYSIZE(names); k++)
			if(ImGui::MenuItem(_(names[k])))
				alignSelected(k, toScreen);
		ImGui::Separator();
		ImGui::BeginDisabled(gSel.size() < 3);
		if(ImGui::MenuItem(L("Space evenly across")))
			distributeSelected(true);
		if(ImGui::MenuItem(L("Space evenly down")))
			distributeSelected(false);
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}
}

void
DrawTextDrawsWindow(void)
{
	drawPopups();
	if(!gMode)
		return;
	UpdateTextDrawsUndo();
	validateSelection();
	drawToolbar();
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 24.0f, ImGui::GetFontSize() * 38.0f), ImGuiCond_FirstUseEver);
	{
		// Under the canvas toolbar the first time, not over it (its two rows,
		// before it has been measured).
		ImVec2 pos, size;
		canvasArea(&pos, &size);
		float rows = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().WindowPadding.y * 2.0f;
		if(gToolbarHeight < rows)
			pos.y += rows - gToolbarHeight;
		ImGui::SetNextWindowPos(ImVec2(pos.x + ImGui::GetFontSize() * 0.5f, pos.y + ImGui::GetFontSize() * 0.5f),
			ImGuiCond_FirstUseEver);
	}
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
	fileBar();

	const std::vector<TextDraw> &mapTds = GetMapTextDraws();
	if(!mapTds.empty()){
		char inMap[128];
		snprintf(inMap, sizeof(inMap), _("%d in the map (read-only)"), (int)mapTds.size());
		ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " %s", inMap);
		ImGui::SetItemTooltip("%s", _("From a map made before textdraws had their own file. They are saved\n"
			"back with the map as they are; move them here to edit them."));
		ImGui::SameLine();
		if(ImGui::SmallButton(L("Move to a separate file...")))
			OfferMoveMapTextDraws(GetPawnMapPath());
	}

	ImGui::SeparatorText(_("Add"));
	if(ImGui::Button(LI(ICON_FA_PLUS, "Text##add"))) addTextDraw(TD_FONT_AHARONI);
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_PLUS, "Box##add"))) addTextDraw(-1);
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_PLUS, "Sprite##add"))) addTextDraw(TD_FONT_SPRITE);
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_PLUS, "Model##add"))) addTextDraw(TD_FONT_PREVIEW);
	if(ImGui::Button(LI(ICON_FA_PLUS, "Image##add"))) addTextDraw(TD_FONT_REFERENCE);
	ImGui::SetItemTooltip("%s", _("A picture to build on: a mock-up or a screenshot (PNG, BMP or TGA).\n"
		"It is a layer like the others, only the editor shows it; the eyedropper picks its colours."));
	ImGui::SameLine();
	shapeButton();

	char layersTitle[96];
	snprintf(layersTitle, sizeof(layersTitle), _("Layers - %s"), boardLabel(gBoard));
	ImGui::SeparatorText(layersTitle);
	layers();
	selectionActions();

	if(gSelected >= 0){
		ImGui::Separator();
		properties();
	}else
		ImGui::TextDisabled("%s", _("Click a textdraw, or drag a box round several. Alt+click picks the one under it."));
	ImGui::End();
	if(!open)
		ExitTextDrawMode();
}
