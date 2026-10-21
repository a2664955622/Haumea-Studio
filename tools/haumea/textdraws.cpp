#include "haumea.h"
#include "textdraws.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <map>

// TextDraws of the open map, their Pawn code, and reading it back.

static std::vector<TextDraw> gTextDraws;
static bool gEditPending;	// changed since the last undo step was recorded

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

void
TextDrawsChanged(void)
{
	gEditPending = true;
	BumpChangeSeq();
}

bool
TakeTextDrawsEdit(void)
{
	bool pending = gEditPending;
	gEditPending = false;
	return pending;
}

bool
TextDrawsEditPending(void)
{
	return gEditPending;
}

void
ResetTextDraws(void)
{
	gTextDraws.clear();
	gEditPending = false;
}

bool
HasTextDraws(void)
{
	return !gTextDraws.empty();
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
buildNames(const char *prefix, std::vector<std::string> &names)
{
	names.clear();
	std::map<std::string, int> used;
	for(size_t i = 0; i < gTextDraws.size(); i++){
		const TextDraw &td = gTextDraws[i];
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
	if(gTextDraws.empty())
		return;
	fprintf(f, "// Call %s_CreateTextDraws() from OnGameModeInit and %s_ShowTextDraws(playerid)\n"
		"// when a player should see the textdraws (it also creates the per-player ones).\n", prefix, prefix);
}

void
WriteTextDrawsPawnGlobals(FILE *f, const char *prefix)
{
	if(gTextDraws.empty())
		return;
	// open.mp's names and tags; SA-MP 0.3.7 gets them from these defines,
	// so the same code compiles without warnings on both.
	fputs("\n#if !defined TEXT_DRAW_FONT_0\n"
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
	std::vector<std::string> names;
	buildNames(prefix, names);
	for(size_t i = 0; i < gTextDraws.size(); i++)
		if(gTextDraws[i].perPlayer)
			fprintf(f, "new PlayerText:%s[MAX_PLAYERS];\n", names[i].c_str());
		else
			fprintf(f, "new Text:%s;\n", names[i].c_str());
	fputc('\n', f);
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
	fputs(");\n", f);
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

void
WriteTextDrawsPawnFunctions(FILE *f, const char *prefix)
{
	if(gTextDraws.empty())
		return;
	std::vector<std::string> names;
	buildNames(prefix, names);
	fprintf(f, "stock %s_CreateTextDraws()\n{\n", prefix);
	for(size_t i = 0; i < gTextDraws.size(); i++)
		if(!gTextDraws[i].perPlayer)
			writeTextDraw(f, gTextDraws[i], names[i]);
	fprintf(f, "}\n\n");
	fprintf(f, "stock %s_ShowTextDraws(playerid)\n{\n", prefix);
	for(size_t i = 0; i < gTextDraws.size(); i++)
		if(gTextDraws[i].perPlayer)
			writeTextDraw(f, gTextDraws[i], names[i]);
	for(size_t i = 0; i < gTextDraws.size(); i++)
		if(gTextDraws[i].perPlayer)
			fprintf(f, "\tPlayerTextDrawShow(playerid, %s[playerid]);\n", names[i].c_str());
		else
			fprintf(f, "\tTextDrawShowForPlayer(playerid, %s);\n", names[i].c_str());
	fprintf(f, "}\n\n");
}

// ---- Reading ----

struct TdCall {
	std::string lhs;	// "x = TextDrawCreate(...)": x, without [playerid]
	std::string name;
	std::vector<std::string> args;
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
ImportTextDrawsFromSource(const std::string &source)
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
			snprintf(td.name, sizeof(td.name), "%s", c.lhs.c_str());
			byHandle[c.lhs] = gTextDraws.size();
			gTextDraws.push_back(td);
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
		TextDraw &td = gTextDraws[it->second];
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
