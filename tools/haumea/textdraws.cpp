// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "textdraws.h"
#include "encoding.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#ifdef _WIN32
#include <windows.h>
#endif

// The textdraw file (its own .pwn/.inc), the read-only textdraws of maps
// made before that, their Pawn code, and reading it back.

static std::vector<TextDraw> gTextDraws;	// the textdraw file's
static std::vector<TextDraw> gMapTextDraws;	// an old map's, saved back with it
static std::string gFilePath;
static PawnTextEncoding gFileEncoding = PAWN_TEXT_CP1252;
static unsigned int gChangeSeq, gSavedSeq;
static bool gEditPending;	// changed since the last undo step was recorded
static std::vector<std::vector<TextDraw>> gUndo, gRedo;
static std::vector<TextDraw> gUndoBase;	// the list as of the last step

void
InitTextDraw(TextDraw *td)
{
	*td = TextDraw();
	td->letterX = 0.48f;
	td->letterY = 1.12f;
	td->sizeX = 1280.0f;
	td->sizeY = 1280.0f;
	td->alignment = 1;
	td->colour = 0xE1E1E1FF;
	td->boxColour = 0x80808080;
	td->backgroundColour = 0x000000FF;
	td->shadow = 2;
	td->font = TD_FONT_AHARONI;
	td->proportional = true;
	td->previewZoom = 1.0f;
	td->previewColour1 = -1;
	td->previewColour2 = -1;
}

std::vector<TextDraw>&
GetTextDraws(void)
{
	return gTextDraws;
}

std::vector<TextDraw>&
GetMapTextDraws(void)
{
	return gMapTextDraws;
}

void
TextDrawsChanged(void)
{
	gEditPending = true;
	gChangeSeq++;
}

unsigned int
TextDrawsChangeSeq(void)
{
	return gChangeSeq;
}

const char*
GetTextDrawFilePath(void)
{
	return gFilePath.c_str();
}

const char*
GetTextDrawFileName(void)
{
	if(gFilePath.empty())
		return "Untitled textdraws";
	size_t slash = gFilePath.find_last_of("/\\");
	return gFilePath.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}

bool
TextDrawFileHasUnsavedChanges(void)
{
	return gChangeSeq != gSavedSeq;
}

static void
resetUndo(void)
{
	gUndo.clear();
	gRedo.clear();
	gUndoBase = gTextDraws;
	gEditPending = false;
}

void
NewTextDrawFile(void)
{
	gTextDraws.clear();
	gFilePath.clear();
	gFileEncoding = PAWN_TEXT_CP1252;
	gSavedSeq = ++gChangeSeq;
	resetUndo();
}

static bool
readFile(const char *path, std::string &out)
{
	FILE *f = fopen(path, "rb");
	if(f == nil)
		return false;
	char buffer[4096];
	size_t n;
	out.clear();
	while((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
		out.append(buffer, n);
	fclose(f);
	return true;
}

bool
OpenTextDrawFile(const char *path, std::string *error)
{
	std::string raw;
	if(path == nil || !readFile(path, raw)){
		*error = std::string("could not read ") + (path ? path : "");
		return false;
	}
	PawnTextEncoding encoding;
	std::string source = DecodePawnFileText(raw, &encoding);
	std::vector<TextDraw> read;
	ImportTextDrawsFromSource(source, read);
	gTextDraws = read;
	gFilePath = path;
	gFileEncoding = encoding;
	gSavedSeq = ++gChangeSeq;
	resetUndo();
	log("TextDraws: %d from %s\n", (int)read.size(), path);
	return true;
}

std::string
TextDrawFilePrefix(const char *path)
{
	std::string base = path ? path : "";
	size_t slash = base.find_last_of("/\\");
	if(slash != std::string::npos)
		base = base.substr(slash + 1);
	size_t dot = base.find('.');
	if(dot != std::string::npos)
		base.resize(dot);
	std::string prefix = "Haumea_";
	for(size_t i = 0; i < base.size(); i++)
		prefix += isalnum((unsigned char)base[i]) || base[i] == '_' ? base[i] : '_';
	if(base.empty())
		prefix += "textdraws";
	return prefix;
}

static void writeCompatDefines(FILE *f);
static void writeGlobals(FILE *f, const std::vector<TextDraw> &tds, const char *prefix);
static void writeFunctions(FILE *f, const std::vector<TextDraw> &tds, const char *prefix);

std::string
BuildTextDrawFileCode(const char *path)
{
	std::string prefix = TextDrawFilePrefix(path);
	const char *p = prefix.c_str();
	FILE *f = tmpfile();
	if(f == nil)
		return std::string();
	fprintf(f, "// TextDraws made with Haumea Studio.\n"
		"// Call %s_CreateTextDraws() from OnGameModeInit and %s_ShowTextDraws(playerid)\n"
		"// when a player should see them (it also creates the per-player ones).\n"
		"\n#tryinclude <open.mp>\n#if !defined _INC_open_mp\n\t#include <a_samp>\n#endif\n", p, p);
	writeCompatDefines(f);
	writeGlobals(f, gTextDraws, p);
	writeFunctions(f, gTextDraws, p);
	std::string code;
	long size = ftell(f);
	if(size > 0 && fseek(f, 0, SEEK_SET) == 0){
		code.resize((size_t)size);
		if(fread(&code[0], 1, (size_t)size, f) != (size_t)size)
			code.clear();
	}
	fclose(f);
	return code;
}

bool
SaveTextDrawFile(const char *path, std::string *error)
{
	std::string target = path && path[0] ? path : gFilePath;
	if(target.empty()){
		*error = "the textdraw file has no name yet";
		return false;
	}
	if(!EnsureParentDirectoriesForPath(target.c_str())){
		*error = "could not create the folder for " + target;
		return false;
	}
	std::string code = EncodePawnFileText(BuildTextDrawFileCode(target.c_str()), gFileEncoding);
	// Written beside it and renamed over it, so a failed save keeps the old file.
	std::string temp = target + ".tmp";
	FILE *f = fopen(temp.c_str(), "wb");
	bool written = f && fwrite(code.data(), 1, code.size(), f) == code.size();
	if(f)
		written = fclose(f) == 0 && written;
#ifdef _WIN32
	written = written && MoveFileExA(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
	written = written && rename(temp.c_str(), target.c_str()) == 0;
#endif
	if(!written){
		remove(temp.c_str());
		*error = "could not write " + target;
		return false;
	}
	gFilePath = target;
	gSavedSeq = ++gChangeSeq;
	log("TextDraws: wrote %d to %s\n", (int)gTextDraws.size(), target.c_str());
	return true;
}

// ---- Undo ----

void
UpdateTextDrawsUndo(void)
{
	// A drag or a typed field is one step, taken when it is let go.
	if(gEditPending && !ImGui::IsAnyItemActive()){
		gEditPending = false;
		gUndo.push_back(gUndoBase);
		gRedo.clear();
		if(gUndo.size() > 200)
			gUndo.erase(gUndo.begin());
		gUndoBase = gTextDraws;
	}
}

static bool
stepUndo(std::vector<std::vector<TextDraw>> &from, std::vector<std::vector<TextDraw>> &to)
{
	UpdateTextDrawsUndo();
	if(from.empty())
		return false;
	to.push_back(gTextDraws);
	gTextDraws = from.back();
	from.pop_back();
	gUndoBase = gTextDraws;
	gChangeSeq++;
	return true;
}

bool
UndoTextDraws(void)
{
	return stepUndo(gUndo, gRedo);
}

bool
RedoTextDraws(void)
{
	return stepUndo(gRedo, gUndo);
}

// ---- The map's own ----

void
ResetMapTextDraws(void)
{
	gMapTextDraws.clear();
}

bool
HasTextDraws(void)
{
	return !gMapTextDraws.empty();
}

std::string
SuggestedTextDrawFilePath(const char *mapPath)
{
	std::string base = mapPath && mapPath[0] ? mapPath : "map.pwn";
	size_t slash = base.find_last_of("/\\");
	size_t dot = base.rfind('.');
	if(dot != std::string::npos && (slash == std::string::npos || dot > slash))
		base.resize(dot);
	std::string path = base + "_textdraws.pwn";
	for(int n = 2; n < 100; n++){
		FILE *f = fopen(path.c_str(), "rb");
		if(f == nil)
			break;
		fclose(f);
		path = base + "_textdraws" + std::to_string(n) + ".pwn";
	}
	return path;
}

int
MoveMapTextDrawsToFile(const char *path, std::string *error)
{
	if(gMapTextDraws.empty())
		return 0;
	bool givenPath = path && path[0];
	if(gFilePath.empty() && !givenPath){
		*error = "no file to move them to";
		return -1;
	}
	if(!gFilePath.empty() && givenPath && gFilePath != path){
		*error = "the textdraw file is already " + gFilePath + ": they go there";
		return -1;
	}
	std::vector<TextDraw> before = gTextDraws;
	unsigned int seq = gChangeSeq;
	bool pending = gEditPending;
	gTextDraws.insert(gTextDraws.end(), gMapTextDraws.begin(), gMapTextDraws.end());
	TextDrawsChanged();
	if(!SaveTextDrawFile(gFilePath.empty() ? path : nil, error)){
		// As it was, saved or not.
		gTextDraws = before;
		gChangeSeq = seq;
		gEditPending = pending;
		return -1;
	}
	int moved = (int)gMapTextDraws.size();
	gMapTextDraws.clear();
	BumpChangeSeq();	// the map saves without them
	resetUndo();
	return moved;
}

// ---- Writing ----

static bool
isIdentifier(const char *s)
{
	if(s == nil || !(isalpha((unsigned char)s[0]) || s[0] == '_' || s[0] == '@'))
		return false;
	for(const char *p = s; *p; p++)
		if(!isalnum((unsigned char)*p) && *p != '_' && *p != '@')
			return false;
	return true;
}

// Variable names: the textdraw's own when it is a plain name, otherwise one
// made from the map's prefix.
static void
buildNames(const std::vector<TextDraw> &tds, const char *prefix, std::vector<std::string> &names)
{
	names.clear();
	std::map<std::string, int> used;
	for(size_t i = 0; i < tds.size(); i++){
		const TextDraw &td = tds[i];
		std::string name = isIdentifier(td.name) ? td.name : "";
		if(name.empty() || used.count(name)){
			int n = (int)i;
			do name = std::string(prefix) + (td.perPlayer ? "_ptd" : "_td") + std::to_string(n++);
			while(used.count(name));
		}
		used[name] = 1;
		names.push_back(name);
	}
}

static void
writeString(FILE *f, const std::string &s)
{
	fputc('"', f);
	for(size_t i = 0; i < s.size(); i++){
		char c = s[i];
		if(c == '"' || c == '\\')
			fputc('\\', f);
		if(c == '\n')
			fputs("\\n", f);
		else
			fputc(c, f);
	}
	fputc('"', f);
}

void
WriteTextDrawsPawnHeader(FILE *f, const char *prefix)
{
	if(gMapTextDraws.empty())
		return;
	fprintf(f, "// Call %s_CreateTextDraws() from OnGameModeInit and %s_ShowTextDraws(playerid)\n"
		"// when a player should see the textdraws (it also creates the per-player ones).\n", prefix, prefix);
}

// open.mp's names and tags; SA-MP 0.3.7 gets them from these defines,
// so the same code compiles without warnings on both. Once only, as a map
// and a textdraw file can both have them.
static void
writeCompatDefines(FILE *f)
{
	fputs("\n#if !defined TEXT_DRAW_FONT_0 && !defined HAUMEA_TD_COMPAT\n"
		"\t#define HAUMEA_TD_COMPAT\n"
		"\t#define TEXT_DRAW_FONT: _:\n"
		"\t#define TEXT_DRAW_ALIGN: _:\n"
		"\t#define TextDrawColour TextDrawColor\n"
		"\t#define TextDrawBoxColour TextDrawBoxColor\n"
		"\t#define TextDrawBackgroundColour TextDrawBackgroundColor\n"
		"\t#define TextDrawSetPreviewVehicleColours TextDrawSetPreviewVehCol\n"
		"\t#define PlayerTextDrawColour PlayerTextDrawColor\n"
		"\t#define PlayerTextDrawBoxColour PlayerTextDrawBoxColor\n"
		"\t#define PlayerTextDrawBackgroundColour PlayerTextDrawBackgroundColor\n"
		"\t#define PlayerTextDrawSetPreviewVehicleColours PlayerTextDrawSetPreviewVehCol\n"
		"#endif\n\n", f);
}

static void
writeGlobals(FILE *f, const std::vector<TextDraw> &tds, const char *prefix)
{
	std::vector<std::string> names;
	buildNames(tds, prefix, names);
	for(size_t i = 0; i < tds.size(); i++)
		if(tds[i].perPlayer)
			fprintf(f, "new PlayerText:%s[MAX_PLAYERS];\n", names[i].c_str());
		else
			fprintf(f, "new Text:%s;\n", names[i].c_str());
	fputc('\n', f);
}

void
WriteTextDrawsPawnGlobals(FILE *f, const char *prefix)
{
	if(gMapTextDraws.empty())
		return;
	writeCompatDefines(f);
	writeGlobals(f, gMapTextDraws, prefix);
}

static void
writeTextDraw(FILE *f, const TextDraw &td, const std::string &name)
{
	TextDraw def;
	InitTextDraw(&def);
	// Global: TextDrawX(t, ...); per player: PlayerTextDrawX(playerid, t[playerid], ...)
	std::string fn = td.perPlayer ? "PlayerTextDraw" : "TextDraw";
	std::string target = td.perPlayer ? "playerid, " + name + "[playerid]" : name;
	const char *n = name.c_str();
	if(td.perPlayer)
		fprintf(f, "\t%s[playerid] = CreatePlayerTextDraw(playerid, %.3f, %.3f, ", n, td.x, td.y);
	else
		fprintf(f, "\t%s = TextDrawCreate(%.3f, %.3f, ", n, td.x, td.y);
	writeString(f, td.text.empty() ? std::string("_") : td.text);
	fputs(td.locked ? "); // locked in Haumea\n" : ");\n", f);
	const char *t = target.c_str();
	const char *p = fn.c_str();
	if(td.letterX != def.letterX || td.letterY != def.letterY)
		fprintf(f, "\t%sLetterSize(%s, %.3f, %.3f);\n", p, t, td.letterX, td.letterY);
	if(td.sizeX != def.sizeX || td.sizeY != def.sizeY)
		fprintf(f, "\t%sTextSize(%s, %.3f, %.3f);\n", p, t, td.sizeX, td.sizeY);
	if(td.alignment != def.alignment)
		fprintf(f, "\t%sAlignment(%s, TEXT_DRAW_ALIGN:%d);\n", p, t, td.alignment);
	if(td.colour != def.colour)
		fprintf(f, "\t%sColour(%s, 0x%08X);\n", p, t, td.colour);
	if(td.useBox)
		fprintf(f, "\t%sUseBox(%s, true);\n", p, t);
	if(td.useBox && td.boxColour != def.boxColour)
		fprintf(f, "\t%sBoxColour(%s, 0x%08X);\n", p, t, td.boxColour);
	if(td.shadow != def.shadow)
		fprintf(f, "\t%sSetShadow(%s, %d);\n", p, t, td.shadow);
	if(td.outline != def.outline)
		fprintf(f, "\t%sSetOutline(%s, %d);\n", p, t, td.outline);
	if(td.backgroundColour != def.backgroundColour)
		fprintf(f, "\t%sBackgroundColour(%s, 0x%08X);\n", p, t, td.backgroundColour);
	if(td.font != def.font)
		fprintf(f, "\t%sFont(%s, TEXT_DRAW_FONT:%d);\n", p, t, td.font);
	if(td.proportional != def.proportional)
		fprintf(f, "\t%sSetProportional(%s, %s);\n", p, t, td.proportional ? "true" : "false");
	if(td.selectable)
		fprintf(f, "\t%sSetSelectable(%s, true);\n", p, t);
	if(td.font == TD_FONT_PREVIEW){
		fprintf(f, "\t%sSetPreviewModel(%s, %d);\n", p, t, td.previewModel);
		fprintf(f, "\t%sSetPreviewRot(%s, %.3f, %.3f, %.3f, %.3f);\n", p, t,
			td.previewRotX, td.previewRotY, td.previewRotZ, td.previewZoom);
		if(td.previewColour1 != -1 || td.previewColour2 != -1)
			fprintf(f, "\t%sSetPreviewVehicleColours(%s, %d, %d);\n", p, t,
				td.previewColour1, td.previewColour2);
	}
}

static void
writeFunctions(FILE *f, const std::vector<TextDraw> &tds, const char *prefix)
{
	std::vector<std::string> names;
	buildNames(tds, prefix, names);
	fprintf(f, "stock %s_CreateTextDraws()\n{\n", prefix);
	for(size_t i = 0; i < tds.size(); i++)
		if(!tds[i].perPlayer)
			writeTextDraw(f, tds[i], names[i]);
	fprintf(f, "}\n\n");
	fprintf(f, "stock %s_ShowTextDraws(playerid)\n{\n", prefix);
	for(size_t i = 0; i < tds.size(); i++)
		if(tds[i].perPlayer)
			writeTextDraw(f, tds[i], names[i]);
	for(size_t i = 0; i < tds.size(); i++)
		if(tds[i].perPlayer)
			fprintf(f, "\tPlayerTextDrawShow(playerid, %s[playerid]);\n", names[i].c_str());
		else
			fprintf(f, "\tTextDrawShowForPlayer(playerid, %s);\n", names[i].c_str());
	fprintf(f, "}\n\n");
}

void
WriteTextDrawsPawnFunctions(FILE *f, const char *prefix)
{
	if(!gMapTextDraws.empty())
		writeFunctions(f, gMapTextDraws, prefix);
}

// ---- Reading ----

struct TdCall {
	std::string lhs;	// "x = TextDrawCreate(...)": x, without [playerid]
	std::string name;
	std::vector<std::string> args;
	bool locked;	// "// locked in Haumea" follows on its line
};

static std::string
trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while(a < b && isspace((unsigned char)s[a])) a++;
	while(b > a && isspace((unsigned char)s[b-1])) b--;
	return s.substr(a, b - a);
}

// A handle: the variable without [playerid] (or [i]); other indexes stay,
// so PlayerTD[playerid][0] and PlayerTD[playerid][1] are two textdraws.
static std::string
handleName(const std::string &expr)
{
	std::string s;
	for(size_t i = 0; i < expr.size(); i++)
		if(!isspace((unsigned char)expr[i]))
			s += expr[i];
	size_t colon = s.find(':');
	if(colon != std::string::npos && s.find('[') > colon)
		s = s.substr(colon + 1);	// Text:x
	static const char *playerIndexes[] = { "[playerid]", "[i]" };
	for(int k = 0; k < 2; k++){
		size_t at;
		while((at = s.find(playerIndexes[k])) != std::string::npos)
			s.erase(at, strlen(playerIndexes[k]));
	}
	return s;
}

static void
scanCalls(const std::string &src, std::vector<TdCall> &calls)
{
	size_t i = 0, n = src.size();
	while(i < n){
		char c = src[i];
		// Skip comments and strings.
		if(c == '/' && i + 1 < n && src[i+1] == '/'){
			while(i < n && src[i] != '\n') i++;
			continue;
		}
		if(c == '/' && i + 1 < n && src[i+1] == '*'){
			size_t end = src.find("*/", i + 2);
			i = end == std::string::npos ? n : end + 2;
			continue;
		}
		if(c == '"'){
			for(i++; i < n && src[i] != '"'; i++)
				if(src[i] == '\\') i++;
			i++;
			continue;
		}
		if(!(isalpha((unsigned char)c) || c == '_') || (i > 0 && (isalnum((unsigned char)src[i-1]) || src[i-1] == '_'))){
			i++;
			continue;
		}
		size_t start = i;
		while(i < n && (isalnum((unsigned char)src[i]) || src[i] == '_')) i++;
		std::string word = src.substr(start, i - start);
		if(word.find("TextDraw") == std::string::npos){
			continue;
		}
		size_t j = i;
		while(j < n && isspace((unsigned char)src[j])) j++;
		if(j >= n || src[j] != '(')
			continue;
		// Arguments, split at top-level commas.
		TdCall call;
		call.name = word;
		call.locked = false;
		int depth = 0;
		std::string arg;
		size_t k = j + 1;
		for(; k < n; k++){
			char d = src[k];
			if(d == '"'){
				size_t s = k;
				for(k++; k < n && src[k] != '"'; k++)
					if(src[k] == '\\') k++;
				arg += src.substr(s, k - s + 1);
				continue;
			}
			if(d == '(' || d == '[' || d == '{') depth++;
			if(d == ')' || d == ']' || d == '}'){
				if(depth == 0 && d == ')')
					break;
				depth--;
			}
			if(d == ',' && depth == 0){
				call.args.push_back(trim(arg));
				arg.clear();
				continue;
			}
			arg += d;
		}
		if(!trim(arg).empty() || !call.args.empty())
			call.args.push_back(trim(arg));
		size_t eol = src.find('\n', k);
		if(k < n && src.substr(k, (eol == std::string::npos ? n : eol) - k).find("// locked in Haumea") != std::string::npos)
			call.locked = true;
		// The variable it is stored in.
		size_t b = start;
		while(b > 0 && isspace((unsigned char)src[b-1])) b--;
		if(b > 0 && src[b-1] == '=' && (b < 2 || (src[b-2] != '=' && src[b-2] != '!' && src[b-2] != '<' && src[b-2] != '>'))){
			size_t e = b - 1;
			size_t s = e;
			int bracketDepth = 0;
			while(s > 0){
				char d = src[s-1];
				if(d == ']') bracketDepth++;
				else if(d == '[') bracketDepth--;
				else if(bracketDepth == 0 && !(isalnum((unsigned char)d) || d == '_' || d == ':' || d == '@' || isspace((unsigned char)d)))
					break;
				if(bracketDepth == 0 && d == '\n')
					break;
				s--;
			}
			std::string lhs = trim(src.substr(s, e - s));
			if(lhs.compare(0, 4, "new ") == 0)
				lhs = trim(lhs.substr(4));
			call.lhs = handleName(lhs);
		}
		calls.push_back(call);
		i = k + 1;
	}
}

static bool
parseNumber(std::string s, float *out)
{
	s = trim(s);
	size_t colon = s.find(':');
	if(colon != std::string::npos)
		s = trim(s.substr(colon + 1));	// Float:1.0, TEXT_DRAW_FONT:2
	if(s == "true"){ *out = 1.0f; return true; }
	if(s == "false"){ *out = 0.0f; return true; }
	// open.mp's named constants.
	static const struct { const char *name; int value; } constants[] = {
		{ "TEXT_DRAW_ALIGN_LEFT", 1 }, { "TEXT_DRAW_ALIGN_CENTRE", 2 }, { "TEXT_DRAW_ALIGN_CENTER", 2 },
		{ "TEXT_DRAW_ALIGN_RIGHT", 3 },
		{ "TEXT_DRAW_FONT_0", 0 }, { "TEXT_DRAW_FONT_1", 1 }, { "TEXT_DRAW_FONT_2", 2 }, { "TEXT_DRAW_FONT_3", 3 },
		{ "TEXT_DRAW_FONT_SPRITE_DRAW", 4 }, { "TEXT_DRAW_FONT_MODEL_PREVIEW", 5 },
		{ "TEXT_DRAW_FONT_BANK", 0 }, { "TEXT_DRAW_FONT_STANDARD", 1 }, { "TEXT_DRAW_FONT_SPACEAGE", 2 },
		{ "TEXT_DRAW_FONT_HEADING", 3 }, { "TEXT_DRAW_FONT_BECKETT_REGULAR", 0 },
		{ "TEXT_DRAW_FONT_AHARONI_BOLD", 1 }, { "TEXT_DRAW_FONT_BANK_GOTHIC", 2 },
		{ "TEXT_DRAW_FONT_PRICEDOWN", 3 }, { "TEXT_DRAW_FONT_SPRITE", 4 }, { "TEXT_DRAW_FONT_PREVIEW", 5 },
	};
	for(size_t i = 0; i < sizeof(constants)/sizeof(constants[0]); i++)
		if(s == constants[i].name){
			*out = (float)constants[i].value;
			return true;
		}
	if(s.empty())
		return false;
	char *end = nil;
	if(s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')){
		*out = (float)strtoul(s.c_str(), &end, 16);
		return *end == '\0';
	}
	double v = strtod(s.c_str(), &end);
	*out = (float)v;
	return *end == '\0';
}

static bool
parseColour(std::string s, unsigned int *out)
{
	s = trim(s);
	char *end = nil;
	if(s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		*out = (unsigned int)strtoul(s.c_str(), &end, 16);
	else
		*out = (unsigned int)strtol(s.c_str(), &end, 10);	// -1 is white
	return end && *end == '\0' && !s.empty();
}

static bool
parseString(const std::string &arg, std::string *out)
{
	std::string s = trim(arg);
	if(s.size() < 2 || s[0] != '"' || s.back() != '"')
		return false;
	out->clear();
	for(size_t i = 1; i + 1 < s.size(); i++){
		char c = s[i];
		if(c == '\\' && i + 2 < s.size()){
			char e = s[++i];
			c = e == 'n' ? '\n' : e == 't' ? '\t' : e;
		}
		*out += c;
	}
	return true;
}

int
ImportTextDrawsFromSource(const std::string &source, std::vector<TextDraw> &out)
{
	std::vector<TdCall> calls;
	scanCalls(source, calls);
	std::map<std::string, size_t> byHandle;
	int added = 0;
	for(size_t i = 0; i < calls.size(); i++){
		TdCall &c = calls[i];
		bool player = c.name == "CreatePlayerTextDraw";
		if(c.name == "TextDrawCreate" || player){
			size_t first = player ? 1 : 0;
			TextDraw td;
			InitTextDraw(&td);
			td.perPlayer = player;
			float x, y;
			if(c.lhs.empty() || c.args.size() < first + 3 ||
			   !parseNumber(c.args[first], &x) || !parseNumber(c.args[first+1], &y) ||
			   !parseString(c.args[first+2], &td.text))
				continue;
			td.x = x;
			td.y = y;
			td.locked = c.locked;
			snprintf(td.name, sizeof(td.name), "%s", c.lhs.c_str());
			byHandle[c.lhs] = out.size();
			out.push_back(td);
			added++;
			continue;
		}
		// Setters: TextDrawX(t, ...) and PlayerTextDrawX(playerid, t, ...).
		bool playerSetter = c.name.compare(0, 14, "PlayerTextDraw") == 0;
		bool globalSetter = c.name.compare(0, 8, "TextDraw") == 0;
		if(!playerSetter && !globalSetter)
			continue;
		std::string op = c.name.substr(playerSetter ? 14 : 8);
		size_t h = playerSetter ? 1 : 0;
		if(c.args.size() <= h)
			continue;
		std::map<std::string, size_t>::iterator it = byHandle.find(handleName(c.args[h]));
		if(it == byHandle.end())
			continue;
		TextDraw &td = out[it->second];
		std::vector<float> v;
		for(size_t a = h + 1; a < c.args.size(); a++){
			float f = 0.0f;
			parseNumber(c.args[a], &f);
			v.push_back(f);
		}
		unsigned int colour = 0;
		bool haveColour = c.args.size() > h + 1 && parseColour(c.args[h+1], &colour);
		if(op == "LetterSize" && v.size() >= 2){ td.letterX = v[0]; td.letterY = v[1]; }
		else if(op == "TextSize" && v.size() >= 2){ td.sizeX = v[0]; td.sizeY = v[1]; }
		else if(op == "Alignment" && v.size() >= 1) td.alignment = (int)v[0];
		else if((op == "Color" || op == "Colour") && haveColour) td.colour = colour;
		else if(op == "UseBox" && v.size() >= 1) td.useBox = v[0] != 0.0f;
		else if((op == "BoxColor" || op == "BoxColour") && haveColour) td.boxColour = colour;
		else if(op == "SetShadow" && v.size() >= 1) td.shadow = (int)v[0];
		else if(op == "SetOutline" && v.size() >= 1) td.outline = (int)v[0];
		else if((op == "BackgroundColor" || op == "BackgroundColour") && haveColour) td.backgroundColour = colour;
		else if(op == "Font" && v.size() >= 1) td.font = (int)v[0];
		else if(op == "SetProportional" && v.size() >= 1) td.proportional = v[0] != 0.0f;
		else if(op == "SetSelectable" && v.size() >= 1) td.selectable = v[0] != 0.0f;
		else if(op == "SetPreviewModel" && v.size() >= 1) td.previewModel = (int)v[0];
		else if(op == "SetPreviewRot" && v.size() >= 3){
			td.previewRotX = v[0]; td.previewRotY = v[1]; td.previewRotZ = v[2];
			td.previewZoom = v.size() >= 4 ? v[3] : 1.0f;
		}else if((op == "SetPreviewVehCol" || op == "SetPreviewVehicleColours" ||
		          op == "SetPreviewVehicleColors") && v.size() >= 2){
			td.previewColour1 = (int)v[0];
			td.previewColour2 = (int)v[1];
		}
	}
	return added;
}
