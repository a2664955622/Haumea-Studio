#include "haumea.h"
#include "workspace.h"
#include "shortcuts.h"
#include "imgui/imgui_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#ifdef RW_GL3
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif
#ifndef _WIN32
#include <unistd.h>
#endif

// --ui-script <file>: drives the editor through its own input layer, for
// tests where no input tool can reach the window (Wayland, CI). Events go in
// through sk::EventHandler, so they reach ImGui, CPad and the shortcuts the
// way real ones do. ImGui items are found by window and label through ImGui's
// test-engine hooks. Exits with 0 when every assert passed, else 1.
//
// One command per line ('#' starts a comment; quote arguments with spaces):
//   wait N                      frames
//   wait_until <assert>         up to 600 frames
//   key Ctrl+Shift+H            press and release a chord
//   shortcut <id|name>          the chord of a shortcut-table action
//   hold KEY / release KEY
//   move X Y                    pixels, or 50% 40% of the window
//   click [X Y] [left|right|middle]
//   drag X1 Y1 X2 Y2 [frames]
//   wheel N
//   click_object N|model=M [right]  a map object, where it is on screen
//   camera X Y Z TX TY TZ / mark_camera
//   click_item 'Window' 'Label' [N] [right]   '*' for any window; N: the Nth match, -1 the last
//   hover_item 'Window' 'Label'
//   set_item 'Window' 'Label' "text" [N] [tab] types into a field ('Colour/##X' = red)
//   close_popup                         clicks the empty end of the menu bar
//   reopen                              opens the map file again
//   click_world X Y Z [shift|right]    looks at a world point and clicks it
//   scroll_until 'Window' 'Label'      scrolls the window until the item shows ('*': the open popup)
//   dump_items [filter]                 lists what ImGui drew last frame
//   drag_item 'Window' 'Label' DX DY [N]
//   drag_item_to 'Window' 'Label' 'Window' 'Label'   drops one item on another
//   type "text"
//   open <map.pwn>              opens a map as File > Open would
//   grab <file.png>
//   assert <what> [op value]    see uiAssert below
//   log "text" / fail "text" / exit

static bool gActive;
static std::vector<std::string> gLines;
static size_t gLine;
static int gFails, gPasses;
static int gFrame;
static FILE *gLog;

// ---- Items seen through the test-engine hooks ----

struct UiItem {
	// seed: the ID stack top, to match items ImGui gives no label (closed combos);
	// parent and grand: the two below it, for parts of a widget ('Colour/##X', 'Size/#1')
	ImGuiID id, seed, parent, grand;
	ImRect bb, clip;	// clip: the visible part of its window when it was drawn
	std::string window, rootWindow, label, rawLabel;
};
static std::vector<UiItem> gItems, gPrevItems;
static std::map<ImGuiID, size_t> gItemIndex;

static std::string
visibleTitle(const char *name)
{
	std::string s = name ? name : "";
	size_t hash = s.find("##");
	if(hash != std::string::npos)
		s.resize(hash);
	return s;
}

void
ImGuiTestEngineHook_ItemAdd(ImGuiContext *ctx, ImGuiID id, const ImRect &bb, const ImGuiLastItemData*)
{
	if(!gActive || id == 0)
		return;
	ImGuiWindow *w = ctx->CurrentWindow;
	UiItem item;
	item.id = id;
	item.seed = item.parent = item.grand = 0;
	item.bb = bb;
	item.clip = bb;
	if(w){
		item.clip = w->ClipRect;
		int n = w->IDStack.Size;
		item.seed = n > 0 ? w->IDStack[n-1] : 0;
		item.parent = n > 1 ? w->IDStack[n-2] : 0;
		item.grand = n > 2 ? w->IDStack[n-3] : 0;
		item.window = visibleTitle(w->Name);
		item.rootWindow = visibleTitle(w->RootWindow ? w->RootWindow->Name : w->Name);
	}
	gItemIndex[id] = gItems.size();
	gItems.push_back(item);
}

void
ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char *label, ImGuiItemStatusFlags)
{
	if(!gActive || label == nil)
		return;
	std::map<ImGuiID, size_t>::iterator it = gItemIndex.find(id);
	if(it != gItemIndex.end()){
		gItems[it->second].label = visibleTitle(label);
		gItems[it->second].rawLabel = label;
	}
}

void
ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...)
{
}

const char*
ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID)
{
	return nil;
}

// Text without leading icon glyphs and spaces.
static std::string
plainLabel(const std::string &s)
{
	// Icon glyphs, "+" and similar marks before the words.
	size_t i = 0;
	while(i < s.size() && ((unsigned char)s[i] >= 0x80 || !isalnum((unsigned char)s[i])))
		i++;
	return s.substr(i);
}

static bool
windowMatches(const UiItem &item, const std::string &window)
{
	if(window == "*" || window.empty())
		return true;
	return plainLabel(item.window).find(window) != std::string::npos ||
		plainLabel(item.rootWindow).find(window) != std::string::npos;
}

static const UiItem *findItem(const std::string &window, const std::string &label, int nth = 1);

// 'Widget/part': a part of a widget that pushes its label as an ID, like a
// colour channel ('Colour/##X') or a DragFloat2 component ('Size (X, Y)/#1').
static const UiItem*
findPart(const std::string &window, const std::string &widget, const std::string &part, int nth)
{
	int seen = 0;
	bool index = part.size() > 1 && part[0] == '#' && isdigit((unsigned char)part[1]);
	int i = index ? atoi(part.c_str() + 1) : 0;
	for(size_t k = 0; k < gPrevItems.size(); k++){
		const UiItem &it = gPrevItems[k];
		if(!windowMatches(it, window))
			continue;
		bool match;
		if(index)
			match = it.parent == ImHashStr(widget.c_str(), 0, it.grand) &&
				it.seed == ImHashData(&i, sizeof(i), it.parent);
		else
			match = (it.seed == ImHashStr(widget.c_str(), 0, it.parent) &&
				(it.rawLabel == part || plainLabel(it.label) == part ||
				 (it.label.empty() && ImHashStr(part.c_str(), 0, it.seed) == it.id))) ||
				// A field one ID deeper: InputInt("##value") inside PushID("Colour 1").
				(it.rawLabel == part && it.parent == ImHashStr(widget.c_str(), 0, it.grand));
		if(match && ++seen == nth)
			return &it;
	}
	return nil;
}

static const UiItem*
findItem(const std::string &window, const std::string &label, int nth)
{
	size_t slash = label.rfind('/');
	if(slash != std::string::npos && slash > 0 && slash + 1 < label.size()){
		const UiItem *part = findPart(window, label.substr(0, slash), label.substr(slash + 1), nth);
		if(part || label[slash+1] == '#')
			return part;
	}
	const UiItem *contains = nil;
	// A label starting with "##" is an ID: matched against the raw label.
	if(label.compare(0, 2, "##") == 0){
		int seen = 0;
		for(size_t i = 0; i < gPrevItems.size(); i++){
			const UiItem &it = gPrevItems[i];
			// The whole ID: '##tile' is not '##tile_size'.
			size_t at = it.rawLabel.find(label), end = at + label.size();
			bool named = it.label.empty() && it.rawLabel.empty() ?
				ImHashStr(label.c_str(), 0, it.seed) == it.id :
				at != std::string::npos && (end == it.rawLabel.size() || it.rawLabel[end] == '#');
			if(windowMatches(it, window) && named && ++seen == nth)
				return &it;
		}
		return nil;
	}
	// A negative N counts from the last one drawn.
	if(nth > 1 || nth < 0){
		std::vector<const UiItem*> matches;
		for(size_t i = 0; i < gPrevItems.size(); i++){
			const UiItem &it = gPrevItems[i];
			bool named = plainLabel(it.label) == label || it.label == label ||
				(it.label.empty() && ImHashStr(label.c_str(), 0, it.seed) == it.id);
			if(windowMatches(it, window) && named)
				matches.push_back(&it);
		}
		int at = nth > 0 ? nth - 1 : (int)matches.size() + nth;
		return at >= 0 && at < (int)matches.size() ? matches[at] : nil;
	}
	for(size_t i = 0; i < gPrevItems.size(); i++){
		const UiItem &it = gPrevItems[i];
		if(!windowMatches(it, window))
			continue;
		if(it.label.empty()){
			if(ImHashStr(label.c_str(), 0, it.seed) == it.id)
				return &it;
			continue;
		}
		std::string plain = plainLabel(it.label);
		if(plain == label || it.label == label)
			return &it;
		if(!contains && plain.find(label) != std::string::npos)
			contains = &it;
	}
	return contains;
}

// ---- Logging ----

static void
say(const char *fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	printf("[ui-script] %s\n", buf);
	fflush(stdout);
	if(gLog){
		fprintf(gLog, "%s\n", buf);
		fflush(gLog);
	}
}

// After a failed lookup: what the script could have clicked, to see why.
static void
dumpSeen(const std::string &window)
{
	int shown = 0, total = 0;
	for(size_t i = 0; i < gPrevItems.size(); i++){
		const UiItem &it = gPrevItems[i];
		if(it.label.empty() || !windowMatches(it, window))
			continue;
		if(shown++ < 40)
			say("  seen '%s' in '%s' at %.0f,%.0f", it.label.c_str(), it.window.c_str(), it.bb.Min.x, it.bb.Min.y);
		total++;
	}
	say("  (%d labelled item(s) in '%s', %d in all)", total, window.c_str(), (int)gPrevItems.size());
}

static std::string gLastToast;
static rw::V3d gMarkedCamera;
static bool gObjectFramed;

void
UiScriptNoteToast(const char *text)
{
	if(gActive){
		gLastToast = text ? text : "";
		say("toast: %s", gLastToast.c_str());
	}
}

// ---- Input ----

struct KeyName { const char *name; int key; };
static const KeyName keyNames[] = {
	{ "esc", sk::KEY_ESC }, { "escape", sk::KEY_ESC }, { "enter", sk::KEY_ENTER }, { "return", sk::KEY_ENTER },
	{ "tab", sk::KEY_TAB }, { "backspace", sk::KEY_BACKSP }, { "delete", sk::KEY_DEL }, { "del", sk::KEY_DEL },
	{ "insert", sk::KEY_INS }, { "home", sk::KEY_HOME }, { "end", sk::KEY_END },
	{ "pageup", sk::KEY_PGUP }, { "pagedown", sk::KEY_PGDN },
	{ "up", sk::KEY_UP }, { "down", sk::KEY_DOWN }, { "left", sk::KEY_LEFT }, { "right", sk::KEY_RIGHT },
	{ "uparrow", sk::KEY_UP }, { "downarrow", sk::KEY_DOWN }, { "leftarrow", sk::KEY_LEFT }, { "rightarrow", sk::KEY_RIGHT },
	{ "space", ' ' }, { "equal", '=' }, { "minus", '-' }, { "period", '.' }, { "slash", '/' },
	{ "ctrl", sk::KEY_LCTRL }, { "shift", sk::KEY_LSHIFT }, { "alt", sk::KEY_LALT },
	{ "keypadadd", sk::KEY_PADADD }, { "keypadsubtract", sk::KEY_PADSUBTRACT },
	{ "keypadmultiply", sk::KEY_PADMULTIPLY }, { "keypaddivide", sk::KEY_PADDIVIDE },
	{ "keypaddecimal", sk::KEY_PADDECIMAL }, { "keypadenter", sk::KEY_PADENTER }, { "keypadequal", sk::KEY_PADEQUAL },
};

static int
keyFromName(std::string name)
{
	std::string n;
	for(size_t i = 0; i < name.size(); i++)
		if(name[i] != ' ')
			n += (char)tolower((unsigned char)name[i]);
	if(n.size() == 1){
		char c = n[0];
		return isalpha((unsigned char)c) ? toupper(c) : c;
	}
	if(n.size() >= 2 && n[0] == 'f' && isdigit((unsigned char)n[1])){
		int f = atoi(n.c_str() + 1);
		if(f >= 1 && f <= 12) return sk::KEY_F1 + f - 1;
	}
	for(const char *prefix : { "keypad", "numpad", "num", "pad" }){
		size_t len = strlen(prefix);
		if(n.compare(0, len, prefix) == 0 && n.size() == len + 1 && isdigit((unsigned char)n[len]))
			return sk::KEY_PAD0 + (n[len] - '0');
	}
	if(n == "numpadadd" || n == "padadd" || n == "num+") return sk::KEY_PADADD;
	if(n == "numpadsubtract" || n == "padsubtract" || n == "num-") return sk::KEY_PADSUBTRACT;
	for(const KeyName &k : keyNames)
		if(n == k.name)
			return k.key;
	return -1;
}

static void
sendKey(int key, bool down)
{
	sk::EventHandler(down ? sk::KEYDOWN : sk::KEYUP, &key);
}

static int gMouseX, gMouseY, gButtons;

static void
sendMouse(void)
{
	sk::MouseState ms = {};
	ms.posx = gMouseX;
	ms.posy = gMouseY;
	ms.buttons = gButtons;
	sk::EventHandler(sk::MOUSEMOVE, &ms);
	sk::EventHandler(sk::MOUSEBTN, &ms);
}

static int
buttonBit(const std::string &b)
{
	return b == "right" ? 4 : b == "middle" ? 2 : 1;
}

// ---- Script ----

// $NAME and ${NAME} become environment variables (the test runner's paths).
static std::string
expand(const std::string &in)
{
	std::string out;
	for(size_t i = 0; i < in.size(); i++){
		if(in[i] != '$' || i + 1 >= in.size()){
			out += in[i];
			continue;
		}
		bool brace = in[i+1] == '{';
		size_t j = i + 1 + (brace ? 1 : 0), start = j;
		while(j < in.size() && (isalnum((unsigned char)in[j]) || in[j] == '_')) j++;
		std::string name = in.substr(start, j - start);
		const char *v = name.empty() ? nil : getenv(name.c_str());
		if(name.empty()){
			out += in[i];
			continue;
		}
		out += v ? v : "";
		i = j - (brace ? 0 : 1);
	}
	return out;
}

static std::vector<std::string>
tokens(const std::string &rawLine)
{
	std::string line = expand(rawLine);
	std::vector<std::string> out;
	size_t i = 0;
	while(i < line.size()){
		while(i < line.size() && isspace((unsigned char)line[i])) i++;
		if(i >= line.size() || line[i] == '#') break;
		std::string t;
		if(line[i] == '\'' || line[i] == '"'){
			char q = line[i++];
			while(i < line.size() && line[i] != q){
				if(line[i] == '\\' && i + 1 < line.size()) i++;
				t += line[i++];
			}
			i++;
		}else
			while(i < line.size() && !isspace((unsigned char)line[i])) t += line[i++];
		out.push_back(t);
	}
	return out;
}

static int
coordinate(const std::string &s, bool vertical)
{
	ImGuiIO &io = ImGui::GetIO();
	if(!s.empty() && s.back() == '%')
		return (int)(atof(s.c_str()) / 100.0f * (vertical ? io.DisplaySize.y : io.DisplaySize.x));
	return atoi(s.c_str());
}

// Chord "Ctrl+Shift+H": the modifiers and the key.
static bool
parseChord(const std::string &text, std::vector<int> &keys)
{
	keys.clear();
	size_t start = 0;
	std::string t = text;
	while(start <= t.size()){
		size_t plus = t.find('+', start);
		// A trailing "+" is the plus key itself ("Ctrl++" or "Num+").
		if(plus == t.size() - 1 && plus > start && (t[plus-1] == '+' || t[plus-1] == 'm'))
			plus = std::string::npos;
		std::string part = t.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
		if(part.empty() && plus != std::string::npos){
			part = "+";
			plus++;
		}
		int key = part == "+" ? sk::KEY_PADADD : keyFromName(part);
		if(key < 0)
			return false;
		keys.push_back(key);
		if(plus == std::string::npos)
			break;
		start = plus + 1;
	}
	return !keys.empty();
}

// A shortcut-table action by id ("duplicate") or name ("Duplicate").
static bool
shortcutChordText(const std::string &name, std::string &chord)
{
	for(int a = 0; a < SC_NUM_ACTIONS; a++){
		const ShortcutInfo *info = GetShortcutInfo(a);
		if(info == nil || (rw::strcmp_ci(info->id, name.c_str()) != 0 && rw::strcmp_ci(info->name, name.c_str()) != 0))
			continue;
		char buf[64];
		ShortcutChordName(GetShortcutChord(a, 0), buf, sizeof(buf));
		chord = buf;
		return !chord.empty();
	}
	return false;
}

static int
countObjects(void)
{
	int n = 0;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst && !inst->m_isDeleted && !IsPawnWorldObject(inst) && IsPawnObjectEditable(inst))
			n++;
	}
	return n;
}

static int
countSelected(void)
{
	int n = 0;
	for(CPtrNode *p = selection.first; p; p = p->next)
		n++;
	return n;
}

static bool
compare(double a, const std::string &op, double b)
{
	if(op == "==") return fabs(a - b) < 0.001;
	if(op == "!=") return fabs(a - b) >= 0.001;
	if(op == ">") return a > b;
	if(op == ">=") return a >= b;
	if(op == "<") return a < b;
	if(op == "<=") return a <= b;
	if(op == "~") return fabs(a - b) < 0.05;
	return false;
}

static std::string
pawnText(bool streamer)
{
	// Named like the open map, so the variables read as in the saved file.
	std::string name = GetPawnMapPath();
	size_t slash = name.find_last_of("/\\");
	if(slash != std::string::npos)
		name = name.substr(slash + 1);
	if(name.empty())
		name = "ui-script-export.pwn";
	std::string path = std::string(GetConfigDir()) + "/" + name;
	int skipped = 0;
	ExportPawnCode(path.c_str(), streamer ? PAWN_CODE_STREAMER : PAWN_CODE_NATIVE, false, &skipped);
	std::string text;
	FILE *f = fopen(path.c_str(), "rb");
	if(f){
		char buf[4096];
		size_t n;
		while((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
		fclose(f);
	}
	return text;
}

static bool
windowOpen(const std::string &title)
{
	ImGuiContext &g = *GImGui;
	for(int i = 0; i < g.Windows.Size; i++){
		ImGuiWindow *w = g.Windows[i];
		if(w->WasActive && !w->Hidden && plainLabel(visibleTitle(w->Name)) == title)
			return true;
	}
	return false;
}

// assert objects|selected|textdraws <op> N
// assert selected.model|selected.x|selected.y|selected.z <op> N
// assert selected.texture <slot> "name"
// assert pawn_contains "text" [streamer]   (and pawn_lacks)
// assert pawn_count "text" <op> N             (occurrences in the native Pawn)
// assert toast_contains "text"
// assert window "Title"     / no_window "Title"
// assert item 'Window' 'Label'   / no_item
static bool
uiAssert(const std::vector<std::string> &a, size_t first, std::string &why)
{
	if(a.size() <= first){
		why = "nothing to assert";
		return false;
	}
	const std::string &what = a[first];
	auto arg = [&](size_t k) -> std::string { return a.size() > first + k ? a[first + k] : std::string(); };
	ObjectInst *sel = selection.first ? (ObjectInst*)selection.first->item : nil;
	char buf[256];
	if(what == "objects" || what == "selected" || what == "textdraws" ||
	   what == "selected.model" || what == "selected.x" || what == "selected.y" || what == "selected.z"){
		double v = 0;
		if(what == "objects") v = countObjects();
		else if(what == "selected") v = countSelected();
		else if(what == "textdraws") v = GetTextDraws().size();
		else if(sel == nil){ why = "nothing selected"; return false; }
		else if(what == "selected.model") v = PawnModelId(sel->m_objectId);
		else if(what == "selected.x") v = sel->m_translation.x;
		else if(what == "selected.y") v = sel->m_translation.y;
		else v = sel->m_translation.z;
		double want = atof(arg(2).c_str());
		snprintf(buf, sizeof(buf), "%s is %g", what.c_str(), v);
		why = buf;
		return compare(v, arg(1), want);
	}
	if(what.compare(0, 9, "textdraw.") == 0){
		// textdraw.N.x|y|letter_x|letter_y|size_x|size_y
		std::vector<TextDraw> &tds = GetTextDraws();
		size_t dot = what.find('.', 9);
		int index = atoi(what.c_str() + 9);
		std::string field = dot == std::string::npos ? "" : what.substr(dot + 1);
		if(index < 0 || index >= (int)tds.size()){ why = "no such textdraw"; return false; }
		const TextDraw &td = tds[index];
		double v = field == "x" ? td.x : field == "y" ? td.y : field == "letter_x" ? td.letterX :
			field == "letter_y" ? td.letterY : field == "size_x" ? td.sizeX : td.sizeY;
		snprintf(buf, sizeof(buf), "%s is %g", what.c_str(), v);
		why = buf;
		return compare(v, arg(1), atof(arg(2).c_str()));
	}
	if(what == "file_contains"){
		std::string text;
		FILE *f = fopen(arg(1).c_str(), "rb");
		if(f){
			char b[4096];
			size_t n;
			while((n = fread(b, 1, sizeof(b), f)) > 0) text.append(b, n);
			fclose(f);
		}
		bool found = text.find(arg(2)) != std::string::npos;
		why = f ? (found ? "found" : "not in the file") : "can't read " + arg(1);
		return found;
	}
	if(what == "gizmo"){
		std::string mode = gGizmoMode == GIZMO_ROTATE ? "rotate" : "move";
		why = "gizmo is " + mode;
		return mode == arg(1);
	}
	if(what == "hidden" || what == "fov" || what == "ui_scale" || what == "camera_moved"){
		double v = 0;
		if(what == "hidden"){
			v = CountHiddenInstances();
		}else if(what == "fov") v = TheCamera.m_fov;
		else if(what == "ui_scale") v = GetUiScale();
		else v = rw::length(rw::sub(TheCamera.m_position, gMarkedCamera));
		snprintf(buf, sizeof(buf), "%s is %g", what.c_str(), v);
		why = buf;
		return compare(v, arg(1), atof(arg(2).c_str()));
	}
	if(what == "selected.texture"){
		if(sel == nil){ why = "nothing selected"; return false; }
		const ObjectMaterialOverride *m = GetObjectMaterialOverride(sel, atoi(arg(1).c_str()));
		std::string tex = m && m->active ? m->textureName : "";
		why = "slot texture is \"" + tex + "\"";
		return tex == arg(2);
	}
	if(what == "pawn_count"){
		std::string text = pawnText(false), needle = arg(1);
		int count = 0;
		for(size_t at = needle.empty() ? std::string::npos : text.find(needle); at != std::string::npos;
		    at = text.find(needle, at + needle.size()))
			count++;
		why = "\"" + needle + "\" occurs " + std::to_string(count) + " time(s)";
		return compare(count, arg(2), atof(arg(3).c_str()));
	}
	if(what == "pawn_contains" || what == "pawn_lacks"){
		std::string text = pawnText(arg(2) == "streamer");
		bool found = text.find(arg(1)) != std::string::npos;
		why = found ? "found" : "not found";
		return what == "pawn_contains" ? found : !found;
	}
	if(what == "toast_contains"){
		why = "last toast: " + gLastToast;
		return gLastToast.find(arg(1)) != std::string::npos;
	}
	if(what == "window" || what == "no_window"){
		bool open = windowOpen(arg(1));
		why = open ? "open" : "not open";
		return what == "window" ? open : !open;
	}
	if(what == "item" || what == "no_item"){
		bool found = findItem(arg(1), arg(2)) != nil;
		why = found ? "found" : "not found";
		return what == "item" ? found : !found;
	}
	why = "unknown assert " + what;
	return false;
}

// Low-level steps a command expands into, one per frame.
struct Step {
	enum { MOVE, DOWN, UP, KEYDOWN, KEYUP, CHAR, WHEEL, ITEM, NOP } type;
	int x, y, value;
	std::string window, label;
	bool right;
	int nth;
};
static std::vector<Step> gSteps;
static size_t gStep;
static int gWaitFrames;
static std::string gScrollWindow, gScrollLabel;
static int gScrollFrames;
static std::vector<std::string> gWaitUntil;
static int gWaitUntilFrames;
static std::string gGrabPath;
static int gDragItemDx, gDragItemDy;
static bool gDragItem;
static std::string gDragToWindow, gDragToLabel;	// drag_item_to: the item to drop on

static bool
runStep(const Step &s)
{
	switch(s.type){
	case Step::MOVE: gMouseX = s.x; gMouseY = s.y; sendMouse(); break;
	case Step::DOWN: gButtons |= s.value; sendMouse(); break;
	case Step::UP: gButtons &= ~s.value; sendMouse(); break;
	case Step::KEYDOWN: sendKey(s.value, true); break;
	case Step::KEYUP: sendKey(s.value, false); break;
	case Step::CHAR: sk::EventHandler(sk::CHARINPUT, (void*)(uintptr)s.value); break;
	case Step::WHEEL: {
		sk::MouseState ms = {};
		ms.posx = gMouseX; ms.posy = gMouseY; ms.buttons = gButtons; ms.wheelDelta = (float)s.value;
		sk::EventHandler(sk::MOUSEWHEEL, &ms);
		break;
	}
	case Step::ITEM: {
		const UiItem *item = findItem(s.window, s.label, s.nth ? s.nth : 1);
		if(item == nil){
			say("line %d: FAIL no item '%s' in window '%s'", (int)gLine, s.label.c_str(), s.window.c_str());
			dumpSeen(s.window);
			gFails++;
			return false;
		}
		gMouseX = (int)((item->bb.Min.x + item->bb.Max.x) * 0.5f);
		gMouseY = (int)((item->bb.Min.y + item->bb.Max.y) * 0.5f);
		say("line %d: '%s' in '%s' at %d, %d", (int)gLine, plainLabel(item->label).c_str(),
			plainLabel(item->window).c_str(), gMouseX, gMouseY);
		sendMouse();
		break;
	}
	case Step::NOP: break;
	}
	return true;
}

static void
addStep(int type, int x = 0, int y = 0, int value = 0)
{
	Step s = {};
	s.type = (decltype(s.type))type;
	s.x = x; s.y = y; s.value = value;
	gSteps.push_back(s);
}

static void
addChord(const std::vector<int> &keys)
{
	for(size_t i = 0; i < keys.size(); i++) addStep(Step::KEYDOWN, 0, 0, keys[i]);
	addStep(Step::NOP);
	for(size_t i = keys.size(); i-- > 0;) addStep(Step::KEYUP, 0, 0, keys[i]);
	addStep(Step::NOP);
}

static void finish(void);

// Turns the next script line into steps, or runs it at once.
static void
nextCommand(void)
{
	while(gLine < gLines.size()){
		std::string line = gLines[gLine++];
		std::vector<std::string> a = tokens(line);
		if(a.empty())
			continue;
		const std::string &cmd = a[0];
		say("line %d: %s", (int)gLine, line.c_str());
		gSteps.clear();
		gStep = 0;
		if(cmd == "wait"){
			gWaitFrames = a.size() > 1 ? atoi(a[1].c_str()) : 1;
			return;
		}else if(cmd == "wait_until"){
			gWaitUntil = a;
			gWaitUntilFrames = 600;
			return;
		}else if(cmd == "key" || cmd == "shortcut" || cmd == "hold" || cmd == "release"){
			std::string chord = a.size() > 1 ? a[1] : "";
			if(cmd == "shortcut" && !shortcutChordText(chord, chord)){
				say("line %d: FAIL no shortcut '%s'", (int)gLine, a.size() > 1 ? a[1].c_str() : "");
				gFails++;
				continue;
			}
			std::vector<int> keys;
			if(!parseChord(chord, keys)){
				say("line %d: FAIL unknown key '%s'", (int)gLine, chord.c_str());
				gFails++;
				continue;
			}
			if(cmd == "hold") for(int k : keys) addStep(Step::KEYDOWN, 0, 0, k);
			else if(cmd == "release") for(int k : keys) addStep(Step::KEYUP, 0, 0, k);
			else addChord(keys);
			return;
		}else if(cmd == "move" && a.size() >= 3){
			addStep(Step::MOVE, coordinate(a[1], false), coordinate(a[2], true));
			return;
		}else if(cmd == "click"){
			int bit = buttonBit(a.size() == 2 ? a[1] : a.size() >= 4 ? a[3] : "left");
			if(a.size() >= 3)
				addStep(Step::MOVE, coordinate(a[1], false), coordinate(a[2], true));
			addStep(Step::NOP);
			addStep(Step::DOWN, 0, 0, bit);
			addStep(Step::UP, 0, 0, bit);
			addStep(Step::NOP);
			return;
		}else if(cmd == "drag" && a.size() >= 5){
			int x1 = coordinate(a[1], false), y1 = coordinate(a[2], true);
			int x2 = coordinate(a[3], false), y2 = coordinate(a[4], true);
			int frames = a.size() > 5 ? atoi(a[5].c_str()) : 10;
			addStep(Step::MOVE, x1, y1);
			addStep(Step::DOWN, 0, 0, 1);
			for(int f = 1; f <= frames; f++)
				addStep(Step::MOVE, x1 + (x2 - x1) * f / frames, y1 + (y2 - y1) * f / frames);
			addStep(Step::UP, 0, 0, 1);
			addStep(Step::NOP);
			return;
		}else if(cmd == "wheel" && a.size() >= 2){
			addStep(Step::WHEEL, 0, 0, atoi(a[1].c_str()));
			addStep(Step::NOP);
			return;
		}else if((cmd == "click_item" || cmd == "hover_item" || cmd == "drag_item") && a.size() >= 3){
			Step s = {};
			s.type = Step::ITEM;
			s.window = a[1];
			s.label = a[2];
			// An optional number picks the Nth item with that label
			// (after dx dy for drag_item).
			for(size_t k = cmd == "drag_item" ? 5 : 3; k < a.size(); k++)
				if(isdigit((unsigned char)a[k][0]) || (a[k][0] == '-' && isdigit((unsigned char)a[k][1])))
					s.nth = atoi(a[k].c_str());
			gSteps.push_back(s);
			addStep(Step::NOP);
			if(cmd == "click_item"){
				int bit = 1;
				for(size_t k = 3; k < a.size(); k++)
					if(a[k] == "right") bit = 4;
				addStep(Step::DOWN, 0, 0, bit);
				addStep(Step::UP, 0, 0, bit);
				addStep(Step::NOP);
			}else if(cmd == "drag_item" && a.size() >= 5){
				gDragItem = true;
				gDragItemDx = atoi(a[3].c_str());
				gDragItemDy = atoi(a[4].c_str());
			}
			return;
		}else if(cmd == "drag_item_to" && a.size() >= 5){
			Step s = {};
			s.type = Step::ITEM;
			s.window = a[1];
			s.label = a[2];
			gSteps.push_back(s);
			addStep(Step::NOP);
			gDragItem = true;
			gDragToWindow = a[3];
			gDragToLabel = a[4];
			return;
		}else if(cmd == "set_item" && a.size() >= 4){
			// set_item 'Window' 'Label' "text" [N] [tab]: double-click the field,
			// select all, type, Enter (Tab instead for a multiline field, where
			// Enter is a new line). Works for text, number, drag and colour fields.
			Step s = {};
			s.type = Step::ITEM;
			s.window = a[1];
			s.label = a[2];
			s.nth = 1;
			bool tab = false;
			for(size_t k = 4; k < a.size(); k++){
				if(a[k] == "tab") tab = true;
				else if(isdigit((unsigned char)a[k][0])) s.nth = atoi(a[k].c_str());
			}
			gSteps.push_back(s);
			addStep(Step::NOP);
			addStep(Step::DOWN, 0, 0, 1);
			addStep(Step::UP, 0, 0, 1);
			addStep(Step::DOWN, 0, 0, 1);
			addStep(Step::UP, 0, 0, 1);
			addStep(Step::NOP);
			addStep(Step::NOP);
			addChord({ sk::KEY_LCTRL, 'A' });
			for(unsigned char c : a[3])
				addStep(Step::CHAR, 0, 0, c);
			addStep(Step::NOP);
			addChord({ tab ? sk::KEY_TAB : sk::KEY_ENTER });
			return;
		}else if(cmd == "close_popup"){
			// Click the empty end of the menu bar: closes an open combo or menu.
			ImGuiIO &io = ImGui::GetIO();
			addStep(Step::MOVE, (int)io.DisplaySize.x - 4, 6);
			addStep(Step::NOP);
			addStep(Step::DOWN, 0, 0, 1);
			addStep(Step::UP, 0, 0, 1);
			addStep(Step::NOP);
			return;
		}else if(cmd == "reopen"){
			// Open the map file again, as saved.
			std::string path = GetPawnMapPath();
			if(path.empty() || !OpenPawnMapFile(path.c_str())){
				say("line %d: FAIL could not reopen '%s'", (int)gLine, path.c_str());
				gFails++;
			}
			gWaitFrames = 2;
			return;
		}else if(cmd == "click_object" && a.size() >= 2){
			// The Nth object of the map (from 0), clicked where it is on screen.
			// click_object N (newest first) or click_object model=M
			bool byModel = a[1].compare(0, 6, "model=") == 0;
			int want = atoi(a[1].c_str() + (byModel ? 6 : 0)), n = 0;
			ObjectInst *target = nil;
			for(CPtrNode *p = instances.first; p && !target; p = p->next){
				ObjectInst *inst = (ObjectInst*)p->item;
				if(inst == nil || inst->m_isDeleted || IsPawnWorldObject(inst) || !IsPawnObjectEditable(inst))
					continue;
				if(byModel ? PawnModelId(inst->m_objectId) == want : n++ == want)
					target = inst;
			}
			if(target == nil){
				say("line %d: FAIL the map has no object %d", (int)gLine, want);
				gFails++;
				continue;
			}
			if(!gObjectFramed){
				// Look at it from close by first, so nothing stands in front.
				ObjectDef *obj = GetObjectDef(target->m_objectId);
				float r = obj && obj->m_colModel ? std::max(obj->m_colModel->boundingSphere.radius, 1.0f) : 2.0f;
				TheCamera.m_target = target->m_translation;
				TheCamera.m_position = rw::add(target->m_translation, rw::V3d{ -1.6f * r, -1.6f * r, 1.4f * r });
				gObjectFramed = true;
				gLine--;	// run this line again once the view has moved
				gWaitFrames = 3;
				return;
			}
			gObjectFramed = false;
			rw::V3d screen;
			float sw, sh;
			if(!Sprite::CalcScreenCoors(target->m_translation, &screen, &sw, &sh, false)){
				say("line %d: FAIL object %d is not on screen", (int)gLine, want);
				gFails++;
				continue;
			}
			int bit = a.size() > 2 && a[2] == "right" ? 4 : 1;
			say("line %d: object at %.0f, %.0f (window %.0fx%.0f)", (int)gLine, screen.x, screen.y,
				ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
			addStep(Step::MOVE, (int)screen.x, (int)screen.y);
			addStep(Step::NOP);
			addStep(Step::DOWN, 0, 0, bit);
			addStep(Step::UP, 0, 0, bit);
			addStep(Step::NOP);
			return;
		}else if(cmd == "click_world" && a.size() >= 4){
			// click_world X Y Z [shift|right]: look at the point from close by, then click it,
			// to place an element or pick what is there.
			rw::V3d p = { (float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str()) };
			if(!gObjectFramed){
				TheCamera.m_target = p;
				TheCamera.m_position = rw::add(p, rw::V3d{ -8.0f, -8.0f, 7.0f });
				gObjectFramed = true;
				gLine--;
				gWaitFrames = 3;
				return;
			}
			gObjectFramed = false;
			rw::V3d screen;
			float sw, sh;
			if(!Sprite::CalcScreenCoors(p, &screen, &sw, &sh, false)){
				say("line %d: FAIL %s %s %s is not on screen", (int)gLine, a[1].c_str(), a[2].c_str(), a[3].c_str());
				gFails++;
				continue;
			}
			bool shift = a.size() > 4 && a[4] == "shift";
			int bit = a.size() > 4 && a[4] == "right" ? 4 : 1;
			addStep(Step::MOVE, (int)screen.x, (int)screen.y);
			if(shift)
				addStep(Step::KEYDOWN, 0, 0, sk::KEY_LSHIFT);
			addStep(Step::NOP);
			addStep(Step::DOWN, 0, 0, bit);
			addStep(Step::UP, 0, 0, bit);
			if(shift)
				addStep(Step::KEYUP, 0, 0, sk::KEY_LSHIFT);
			addStep(Step::NOP);
			return;
		}else if(cmd == "scroll_until" && a.size() >= 3){
			gScrollWindow = a[1];
			gScrollLabel = a[2];
			gScrollFrames = 120;
			return;
		}else if(cmd == "dump_items"){
			// What ImGui drew last frame, for writing scripts.
			std::string filter = a.size() > 1 ? a[1] : "";
			for(size_t i = 0; i < gPrevItems.size(); i++){
				const UiItem &it = gPrevItems[i];
				std::string line = "'" + plainLabel(it.window) + "' '" + it.rawLabel + "'";
				if(filter.empty() || line.find(filter) != std::string::npos)
					say("  item %s at %.0f,%.0f", line.c_str(), it.bb.Min.x, it.bb.Min.y);
			}
			continue;
		}else if(cmd == "mark_camera"){
			gMarkedCamera = TheCamera.m_position;
			continue;
		}else if(cmd == "camera" && a.size() >= 7){
			// camera X Y Z TX TY TZ: look at a spot, for click_object.
			TheCamera.m_position = { (float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str()) };
			TheCamera.m_target = { (float)atof(a[4].c_str()), (float)atof(a[5].c_str()), (float)atof(a[6].c_str()) };
			gWaitFrames = 3;
			return;
		}else if(cmd == "type" && a.size() >= 2){
			for(unsigned char c : a[1])
				addStep(Step::CHAR, 0, 0, c);
			addStep(Step::NOP);
			return;
		}else if(cmd == "open" && a.size() >= 2){
			if(!OpenPawnMapFile(a[1].c_str())){
				say("line %d: FAIL could not open %s", (int)gLine, a[1].c_str());
				gFails++;
			}
			gWaitFrames = 2;
			return;
		}else if(cmd == "grab" && a.size() >= 2){
			gGrabPath = a[1];
			gWaitFrames = 2;
			return;
		}else if(cmd == "assert"){
			std::string why;
			if(uiAssert(a, 1, why)){
				gPasses++;
				say("line %d: ok (%s)", (int)gLine, why.c_str());
			}else{
				gFails++;
				say("line %d: FAIL %s", (int)gLine, why.c_str());
			}
			continue;
		}else if(cmd == "log"){
			continue;
		}else if(cmd == "fail"){
			gFails++;
			continue;
		}else if(cmd == "exit"){
			gLine = gLines.size();
			break;
		}else{
			say("line %d: FAIL unknown command '%s'", (int)gLine, cmd.c_str());
			gFails++;
			continue;
		}
	}
	if(gLine >= gLines.size() && gSteps.empty() && gWaitFrames <= 0 && gWaitUntil.empty())
		finish();
}

static void
finish(void)
{
	say("%s: %d assert(s) passed, %d failure(s)", gFails ? "FAILED" : "PASSED", gPasses, gFails);
	if(gLog)
		fclose(gLog);
	fflush(stdout);
	// Skip shutdown: nothing of a test run should be saved.
	_exit(gFails ? 1 : 0);
}

static bool
privateConfig(void)
{
	// Only with a config folder of its own: a script must never change the
	// user's settings, recent maps or shortcuts.
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if(xdg == nil || xdg[0] == '\0')
		return false;
	if(home && (std::string(xdg) == std::string(home) + "/.config" || std::string(xdg) == std::string(home) + "/.config/"))
		return false;
	return true;
}

bool
UiScriptActive(void)
{
	return gActive;
}

void
UiScriptInit(void)
{
	const char *path = nil;
	for(int i = 1; i + 1 < sk::args.argc; i++)
		if(strcmp(sk::args.argv[i], "--ui-script") == 0)
			path = sk::args.argv[i+1];
	if(path == nil)
		return;
	if(!privateConfig()){
		printf("[ui-script] refusing to run: set XDG_CONFIG_HOME to a private folder\n");
		fflush(stdout);
		_exit(2);
	}
	FILE *f = fopen(path, "r");
	if(f == nil){
		printf("[ui-script] can't read %s\n", path);
		fflush(stdout);
		_exit(2);
	}
	char line[2048];
	while(fgets(line, sizeof(line), f)){
		line[strcspn(line, "\r\n")] = '\0';
		gLines.push_back(line);
	}
	fclose(f);
	// The log goes to the current folder, not next to the script in the source tree.
	const char *base = strrchr(path, '/');
	gLog = fopen((std::string(base ? base + 1 : path) + ".log").c_str(), "w");
	gActive = true;
	say("running %s (%d lines)", path, (int)gLines.size());
}

// Once a frame, before the editor reads input.
void
UiScriptTick(void)
{
	static bool inited;
	if(!inited){
		inited = true;
		UiScriptInit();
	}
	if(!gActive)
		return;
	GImGui->TestEngineHookItems = true;
	gPrevItems.swap(gItems);
	gItems.clear();
	gItemIndex.clear();
	gFrame++;
	if(gFrame < 30)
		return;	// let the first frames settle
	static bool seenItems, reported;
	seenItems = seenItems || !gPrevItems.empty();
	if(seenItems && !reported){
		reported = true;
		ImGuiIO &io = ImGui::GetIO();
		say("window %.0fx%.0f, framebuffer scale %.2f, UI scale %.2f",
			io.DisplaySize.x, io.DisplaySize.y, io.DisplayFramebufferScale.x, GetUiScale());
	}
	if(!seenItems && gFrame > 600){
		say("FAIL ImGui reports no items: this editor was built without IMGUI_ENABLE_TEST_ENGINE "
			"; objects built before it was added need rebuilding (touch vendor/imgui-docking/*.cpp)");
		gFails++;
		finish();
	}
	if(!seenItems)
		return;	// still loading: nothing drawn yet
	if(gFrame > 60 * 60 * 10){
		say("FAIL the script ran for too long");
		gFails++;
		finish();
	}
	if(gWaitFrames > 0){
		gWaitFrames--;
		return;
	}
	if(!gScrollLabel.empty()){
		// scroll_until: down a step a frame until the item is drawn whole,
		// not cut off by the window's edge where a click would miss it.
		const UiItem *target = findItem(gScrollWindow, gScrollLabel);
		if(target && target->clip.Contains(target->bb)){
			gScrollLabel.clear();
		}else if(--gScrollFrames <= 0){
			say("line %d: FAIL never found '%s' in '%s'", (int)gLine, gScrollLabel.c_str(), gScrollWindow.c_str());
			dumpSeen(gScrollWindow);
			gFails++;
			gScrollLabel.clear();
		}else{
			ImGuiContext &g = *GImGui;
			// '*' scrolls the open popup (a combo's list).
			if(gScrollWindow == "*" && g.OpenPopupStack.Size > 0 && g.OpenPopupStack.back().Window){
				ImGuiWindow *w = g.OpenPopupStack.back().Window;
				ImGui::SetScrollY(w, std::min(w->Scroll.y + 60.0f, w->ScrollMax.y));
			}
			for(int i = 0; i < g.Windows.Size; i++){
				ImGuiWindow *w = g.Windows[i];
				std::string title = plainLabel(visibleTitle(w->Name));
				if(w->WasActive && !title.empty() && title.find(gScrollWindow) != std::string::npos)
					ImGui::SetScrollY(w, std::min(w->Scroll.y + 60.0f, w->ScrollMax.y));
			}
			return;
		}
	}
	if(!gWaitUntil.empty()){
		std::string why;
		if(uiAssert(gWaitUntil, 1, why)){
			gWaitUntil.clear();
		}else if(--gWaitUntilFrames <= 0){
			say("line %d: FAIL timed out waiting: %s", (int)gLine, why.c_str());
			gFails++;
			gWaitUntil.clear();
		}else
			return;
	}
	if(gStep < gSteps.size()){
		// An item still being laid out (a tab just opened, a list filling)
		// gets up to a second to show up before the lookup fails.
		static int itemWait;
		const Step &next = gSteps[gStep];
		if(next.type == Step::ITEM && itemWait < 60 &&
		   findItem(next.window, next.label, next.nth ? next.nth : 1) == nil){
			itemWait++;
			return;
		}
		itemWait = 0;
		if(!runStep(gSteps[gStep])){
			gSteps.clear();
			gStep = 0;
		}else if(gSteps[gStep].type == Step::ITEM && gDragItem){
			// drag_item: press on the item, move by the offset, let go.
			gDragItem = false;
			int x = gMouseX, y = gMouseY;
			if(!gDragToLabel.empty()){
				// drag_item_to: the offset to the other item's middle.
				const UiItem *to = findItem(gDragToWindow, gDragToLabel);
				if(to == nil){
					say("line %d: FAIL no item '%s' in window '%s' to drop on", (int)gLine,
						gDragToLabel.c_str(), gDragToWindow.c_str());
					gFails++;
				}else{
					gDragItemDx = (int)((to->bb.Min.x + to->bb.Max.x) * 0.5f) - x;
					gDragItemDy = (int)((to->bb.Min.y + to->bb.Max.y) * 0.5f) - y;
				}
				gDragToLabel.clear();
			}
			std::vector<Step> rest;
			Step d = {}; d.type = Step::DOWN; d.value = 1; rest.push_back(d);
			for(int f = 1; f <= 10; f++){
				Step m = {}; m.type = Step::MOVE;
				m.x = x + gDragItemDx * f / 10; m.y = y + gDragItemDy * f / 10;
				rest.push_back(m);
			}
			// Rest over the drop point a few frames: ImGui drops on what was
			// hovered before the button went up.
			for(int f = 0; f < 3; f++)
				rest.push_back(rest.back());
			Step u = {}; u.type = Step::UP; u.value = 1; rest.push_back(u);
			gSteps.insert(gSteps.begin() + gStep + 1, rest.begin(), rest.end());
			gStep++;
		}else
			gStep++;
		return;
	}
	nextCommand();
}

// After the frame is drawn: frame grabs.
void
UiScriptAfterRender(void)
{
	if(!gActive || gGrabPath.empty())
		return;
#ifdef RW_GL3
	int vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);
	int w = vp[2], h = vp[3];
	rw::Image *image = rw::Image::create(w, h, 24);
	image->allocate();
	std::vector<uint8> px(w * h * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
	for(int y = 0; y < h; y++)
		memcpy(image->pixels + y * image->stride, &px[(h - 1 - y) * w * 3], w * 3);
	rw::writePNG(image, gGrabPath.c_str());
	image->destroy();
	say("grabbed %s (%dx%d)", gGrabPath.c_str(), w, h);
#else
	say("grab is only available in the GL3 build");
#endif
	gGrabPath.clear();
}
