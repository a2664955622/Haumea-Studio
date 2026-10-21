// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "shortcuts.h"
#include "imgui/imgui_internal.h"

#include <stdio.h>
#include <string.h>

namespace {

enum {
	CTRL = ImGuiMod_Ctrl,
	SHIFT = ImGuiMod_Shift,
	ALT = ImGuiMod_Alt,
	MODS = CTRL | SHIFT | ALT,
};

struct ShortcutDef
{
	ShortcutInfo info;
	ShortcutChord defaults[SHORTCUT_CHORDS];
};

// In the order of ShortcutAction.
const ShortcutDef defs[SC_NUM_ACTIONS] = {
	{ { "new_map", "File", "New map", SHORTCUT_EDITOR, false }, { { ImGuiKey_N, CTRL } } },
	{ { "open_map", "File", "Open a Pawn map", SHORTCUT_EDITOR, false }, { { ImGuiKey_O, CTRL } } },
	{ { "save_map", "File", "Save the map", SHORTCUT_EDITOR, false }, { { ImGuiKey_S, CTRL } } },
	{ { "save_map_as", "File", "Save the map as", SHORTCUT_EDITOR, false }, { { ImGuiKey_S, CTRL | SHIFT } } },
	{ { "export_prefab", "File", "Export selection as prefab", SHORTCUT_EDITOR, false }, { { ImGuiKey_E, CTRL | SHIFT } } },
	{ { "import_prefab", "File", "Import prefab", SHORTCUT_EDITOR, false }, { { ImGuiKey_I, CTRL | SHIFT } } },
	{ { "test_in_game", "File", "Test in game", SHORTCUT_EDITOR, false }, { { ImGuiKey_F5, 0 } } },

	{ { "undo", "Edit", "Undo", SHORTCUT_EDITOR, true }, { { ImGuiKey_Z, CTRL } } },
	{ { "redo", "Edit", "Redo", SHORTCUT_EDITOR, true }, { { ImGuiKey_Y, CTRL }, { ImGuiKey_Z, CTRL | SHIFT } } },
	{ { "copy", "Edit", "Copy selection", SHORTCUT_EDITOR, false }, { { ImGuiKey_C, CTRL } } },
	{ { "cut", "Edit", "Cut selection", SHORTCUT_EDITOR, false }, { { ImGuiKey_X, CTRL } } },
	{ { "paste", "Edit", "Paste in front of the camera", SHORTCUT_EDITOR, false }, { { ImGuiKey_V, CTRL } } },
	{ { "paste_in_place", "Edit", "Paste where it was copied", SHORTCUT_EDITOR, false }, { { ImGuiKey_V, CTRL | SHIFT } } },
	{ { "duplicate", "Edit", "Duplicate selection", SHORTCUT_EDITOR, false }, { { ImGuiKey_D, CTRL } } },
	{ { "delete", "Edit", "Delete selection", SHORTCUT_EDITOR, false }, { { ImGuiKey_Delete, 0 }, { ImGuiKey_Backspace, 0 } } },
	{ { "rename", "Edit", "Rename the selected object's variable (Map Objects)", SHORTCUT_PANEL, false }, { { ImGuiKey_F2, 0 } } },
	{ { "group_selection", "Edit", "Put the selection in a group", SHORTCUT_EDITOR, false }, { { ImGuiKey_G, CTRL } } },
	{ { "cancel", "Edit", "Leave place, prefab, brush, path or TextDraw mode", SHORTCUT_EDITOR, false }, { { ImGuiKey_Escape, 0 } } },

	{ { "gizmo_move", "Transform", "Move gizmo", SHORTCUT_EDITOR, false }, { { ImGuiKey_W, 0 } } },
	{ { "gizmo_rotate", "Transform", "Rotate gizmo", SHORTCUT_EDITOR, false }, { { ImGuiKey_E, 0 }, { ImGuiKey_Q, 0 } } },
	{ { "gizmo_space", "Transform", "Switch local / world axes", SHORTCUT_EDITOR, false }, { { ImGuiKey_L, 0 } } },
	{ { "snap_to_ground", "Transform", "Drop selection to the ground", SHORTCUT_EDITOR, false }, { { ImGuiKey_G, 0 } } },
	{ { "align_to_ground", "Transform", "Drop and align to the ground's slope", SHORTCUT_EDITOR, false }, { { ImGuiKey_G, SHIFT } } },
	{ { "object_snap", "Transform", "Object snap on / off", SHORTCUT_EDITOR, false }, { { ImGuiKey_N, 0 } } },
	{ { "focus_selection", "Transform", "Focus the camera on the selection", SHORTCUT_EDITOR, false }, { { ImGuiKey_F, 0 } } },

	{ { "outline_objects", "View", "Outline every map object", SHORTCUT_EDITOR, false }, { { ImGuiKey_O, 0 } } },
	{ { "viewer_camera", "View", "Viewer camera (longer draw distance)", SHORTCUT_EDITOR, false }, { { ImGuiKey_C, 0 } } },
	{ { "hide_selection", "View", "Hide the selection in the viewport", SHORTCUT_EDITOR, false }, { { ImGuiKey_H, 0 } } },
	{ { "isolate_selection", "View", "Isolate the selection (hide everything else)", SHORTCUT_EDITOR, false }, { { ImGuiKey_H, SHIFT } } },
	{ { "show_all", "View", "Show everything hidden", SHORTCUT_EDITOR, false }, { { ImGuiKey_H, ALT } } },
	{ { "player_view", "View", "Player view: see the map as a player would", SHORTCUT_EDITOR, false }, { { ImGuiKey_P, 0 } } },
	{ { "fov_in", "View", "Zoom in (narrower FOV, like the wheel)", SHORTCUT_EDITOR, true }, { { ImGuiKey_Equal, CTRL }, { ImGuiKey_KeypadAdd, 0 } } },
	{ { "fov_out", "View", "Zoom out (wider FOV, like the wheel)", SHORTCUT_EDITOR, true }, { { ImGuiKey_Minus, CTRL }, { ImGuiKey_KeypadSubtract, 0 } } },
	{ { "fov_reset", "View", "Reset FOV", SHORTCUT_EDITOR, false }, { { ImGuiKey_0, CTRL } } },
	{ { "fly_faster", "View", "Faster fly speed", SHORTCUT_EDITOR, true }, { { ImGuiKey_Equal, CTRL | SHIFT } } },
	{ { "fly_slower", "View", "Slower fly speed", SHORTCUT_EDITOR, true }, { { ImGuiKey_Minus, CTRL | SHIFT } } },
	{ { "ui_scale_up", "View", "Bigger interface (like Ctrl+wheel)", SHORTCUT_EDITOR, true }, { { ImGuiKey_Equal, CTRL | ALT } } },
	{ { "ui_scale_down", "View", "Smaller interface (like Ctrl+wheel)", SHORTCUT_EDITOR, true }, { { ImGuiKey_Minus, CTRL | ALT } } },
	{ { "ui_scale_reset", "View", "Reset interface size", SHORTCUT_EDITOR, false }, { { ImGuiKey_0, CTRL | ALT } } },
	{ { "scroll_up", "View", "Scroll the panel under the mouse up", SHORTCUT_PANEL, true }, { { ImGuiKey_PageUp, 0 } } },
	{ { "scroll_down", "View", "Scroll the panel under the mouse down", SHORTCUT_PANEL, true }, { { ImGuiKey_PageDown, 0 } } },

	{ { "window_browser", "Windows", "Object Browser", SHORTCUT_EDITOR, false }, { { ImGuiKey_1, CTRL } } },
	{ { "window_object_info", "Windows", "Object Info", SHORTCUT_EDITOR, false }, { { ImGuiKey_2, CTRL } } },
	{ { "window_map_objects", "Windows", "Map Objects", SHORTCUT_EDITOR, false }, { { ImGuiKey_3, CTRL } } },
	{ { "window_tools", "Windows", "Tools", SHORTCUT_EDITOR, false }, { { ImGuiKey_4, CTRL } } },
	{ { "window_view", "Windows", "View", SHORTCUT_EDITOR, false }, { { ImGuiKey_5, CTRL } } },
	{ { "window_time_weather", "Windows", "Time & Weather", SHORTCUT_EDITOR, false }, { { ImGuiKey_6, CTRL } } },
	{ { "window_camera", "Windows", "Camera", SHORTCUT_EDITOR, false }, { { ImGuiKey_7, CTRL } } },
	{ { "window_changes", "Windows", "Changes", SHORTCUT_EDITOR, false }, { { ImGuiKey_8, CTRL } } },
	{ { "window_pawn_code", "Windows", "Live Pawn Code", SHORTCUT_EDITOR, false }, { { ImGuiKey_9, CTRL } } },
	{ { "window_textures", "Windows", "Texture Browser", SHORTCUT_EDITOR, false }, { { ImGuiKey_T, CTRL } } },
	{ { "window_nodes", "Windows", "Nodes (open.mp)", SHORTCUT_EDITOR, false }, { { ImGuiKey_F3, 0 } } },
	{ { "window_shortcuts", "Windows", "Keyboard Shortcuts", SHORTCUT_EDITOR, false }, { { ImGuiKey_F1, 0 } } },
	{ { "window_log", "Windows", "Log", SHORTCUT_EDITOR, false }, { { ImGuiKey_F12, 0 } } },
	{ { "window_preferences", "Windows", "Preferences", SHORTCUT_EDITOR, false }, { { ImGuiKey_Comma, CTRL } } },
	{ { "window_map_check", "Windows", "Map Check", SHORTCUT_EDITOR, false }, { { ImGuiKey_F6, 0 } } },
};

ShortcutChord chords[SC_NUM_ACTIONS][SHORTCUT_CHORDS];
bool chordsLoaded;
bool gBlocked;
bool gCapturing;
char labels[SC_NUM_ACTIONS][2 * 48];

void
ensureLoaded(void)
{
	if(chordsLoaded)
		return;
	for(int i = 0; i < SC_NUM_ACTIONS; i++)
		for(int s = 0; s < SHORTCUT_CHORDS; s++)
			chords[i][s] = defs[i].defaults[s];
	chordsLoaded = true;
	labels[0][0] = '\0';
}

void
updateLabel(int action)
{
	char a[48], b[48];
	ShortcutChordName(chords[action][0], a, sizeof(a));
	ShortcutChordName(chords[action][1], b, sizeof(b));
	if(a[0] && b[0])
		snprintf(labels[action], sizeof(labels[action]), "%s / %s", a, b);
	else
		snprintf(labels[action], sizeof(labels[action]), "%s", a[0] ? a : b);
}

void
updateLabels(void)
{
	for(int i = 0; i < SC_NUM_ACTIONS; i++)
		updateLabel(i);
}

// Shorter than ImGui's names for the punctuation keys.
const struct { int key; const char *name; } keyNames[] = {
	{ ImGuiKey_Equal, "=" }, { ImGuiKey_Minus, "-" }, { ImGuiKey_Comma, "," },
	{ ImGuiKey_Period, "." }, { ImGuiKey_Slash, "/" }, { ImGuiKey_Semicolon, ";" },
	{ ImGuiKey_Apostrophe, "'" }, { ImGuiKey_LeftBracket, "[" }, { ImGuiKey_RightBracket, "]" },
	{ ImGuiKey_Backslash, "\\" }, { ImGuiKey_GraveAccent, "`" }, { ImGuiKey_Delete, "Del" },
	{ ImGuiKey_Escape, "Esc" }, { ImGuiKey_PageUp, "PgUp" }, { ImGuiKey_PageDown, "PgDn" },
	{ ImGuiKey_KeypadAdd, "Num+" }, { ImGuiKey_KeypadSubtract, "Num-" }, { ImGuiKey_KeypadMultiply, "Num*" },
	{ ImGuiKey_KeypadDivide, "Num/" }, { ImGuiKey_KeypadDecimal, "Num." }, { ImGuiKey_KeypadEnter, "NumEnter" },
	{ ImGuiKey_KeypadEqual, "Num=" }, { ImGuiKey_Keypad0, "Num0" }, { ImGuiKey_Keypad1, "Num1" },
	{ ImGuiKey_Keypad2, "Num2" }, { ImGuiKey_Keypad3, "Num3" }, { ImGuiKey_Keypad4, "Num4" },
	{ ImGuiKey_Keypad5, "Num5" }, { ImGuiKey_Keypad6, "Num6" }, { ImGuiKey_Keypad7, "Num7" },
	{ ImGuiKey_Keypad8, "Num8" }, { ImGuiKey_Keypad9, "Num9" },
};

const char*
keyName(int key)
{
	for(size_t i = 0; i < sizeof(keyNames) / sizeof(keyNames[0]); i++)
		if(keyNames[i].key == key)
			return keyNames[i].name;
	return ImGui::GetKeyName((ImGuiKey)key);
}

bool
isModifierKey(int key)
{
	return key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
		key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
		key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt ||
		key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper ||
		key == ImGuiKey_CapsLock;
}

int
currentMods(void)
{
	return ImGui::GetIO().KeyMods & MODS;
}

bool
chordPressed(ShortcutChord chord, bool repeat)
{
	return chord.key != 0 && currentMods() == chord.mods &&
		ImGui::IsKeyPressed((ImGuiKey)chord.key, repeat);
}

bool
sameChord(ShortcutChord a, ShortcutChord b)
{
	return a.key != 0 && a.key == b.key && a.mods == b.mods;
}

}	// namespace

const ShortcutInfo*
GetShortcutInfo(int action)
{
	return action >= 0 && action < SC_NUM_ACTIONS ? &defs[action].info : nil;
}

ShortcutChord
GetShortcutChord(int action, int slot)
{
	ensureLoaded();
	return chords[action][slot];
}

ShortcutChord
GetDefaultShortcutChord(int action, int slot)
{
	return defs[action].defaults[slot];
}

void
SetShortcutChord(int action, int slot, ShortcutChord chord)
{
	ensureLoaded();
	chords[action][slot] = chord;
	updateLabel(action);
}

void
ResetShortcuts(void)
{
	chordsLoaded = false;
	ensureLoaded();
	updateLabels();
}

bool
ShortcutIsDefault(int action)
{
	ensureLoaded();
	for(int s = 0; s < SHORTCUT_CHORDS; s++)
		if(chords[action][s].key != defs[action].defaults[s].key ||
		   chords[action][s].mods != defs[action].defaults[s].mods)
			return false;
	return true;
}

void
ShortcutsBeginFrame(bool blocked)
{
	ensureLoaded();
	ImGuiIO &io = ImGui::GetIO();
	gBlocked = blocked || gCapturing || io.WantCaptureKeyboard || io.WantTextInput ||
		ImGui::GetActiveID() != 0;
}

void
ShortcutsSetCapturing(bool capturing)
{
	gCapturing = capturing;
	if(capturing)
		gBlocked = true;
}

bool
ShortcutPressed(int action)
{
	ensureLoaded();
	if(gBlocked || action < 0 || action >= SC_NUM_ACTIONS)
		return false;
	const ShortcutInfo &info = defs[action].info;
	if(info.context == SHORTCUT_VIEWPORT && ImGui::GetIO().WantCaptureMouse)
		return false;
	for(int s = 0; s < SHORTCUT_CHORDS; s++)
		if(chordPressed(chords[action][s], info.repeat))
			return true;
	return false;
}

const char*
ShortcutLabel(int action)
{
	ensureLoaded();
	if(labels[0][0] == '\0')
		updateLabels();
	return labels[action];
}

void
ShortcutChordName(ShortcutChord chord, char *buf, int size)
{
	if(chord.key == 0){
		buf[0] = '\0';
		return;
	}
	snprintf(buf, size, "%s%s%s%s",
		chord.mods & CTRL ? "Ctrl+" : "",
		chord.mods & SHIFT ? "Shift+" : "",
		chord.mods & ALT ? "Alt+" : "",
		keyName(chord.key));
}

bool
ParseShortcutChord(const char *text, ShortcutChord *chord)
{
	chord->key = 0;
	chord->mods = 0;
	if(strcmp(text, "none") == 0)
		return true;
	static const struct { const char *prefix; int mod; } prefixes[] = {
		{ "Ctrl+", CTRL }, { "Shift+", SHIFT }, { "Alt+", ALT },
	};
	for(bool found = true; found;){
		found = false;
		for(size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++){
			size_t n = strlen(prefixes[i].prefix);
			// "Ctrl+" alone would leave no key; "+" is never a key name.
			if(strncmp(text, prefixes[i].prefix, n) == 0 && text[n]){
				chord->mods |= prefixes[i].mod;
				text += n;
				found = true;
			}
		}
	}
	for(int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++){
		if(isModifierKey(key))
			continue;
		if(strcmp(keyName(key), text) == 0 || strcmp(ImGui::GetKeyName((ImGuiKey)key), text) == 0){
			chord->key = key;
			return true;
		}
	}
	chord->mods = 0;
	return false;
}

bool
ShortcutReadPressedChord(ShortcutChord *chord)
{
	for(int key = ImGuiKey_Keyboard_BEGIN; key < ImGuiKey_Keyboard_END; key++){
		if(isModifierKey(key))
			continue;
		if(ImGui::IsKeyPressed((ImGuiKey)key, false)){
			chord->key = key;
			chord->mods = currentMods();
			return true;
		}
	}
	return false;
}

int
ShortcutConflict(int action, int slot)
{
	ensureLoaded();
	ShortcutChord chord = chords[action][slot];
	for(int i = 0; i < SC_NUM_ACTIONS; i++)
		for(int s = 0; s < SHORTCUT_CHORDS; s++)
			if((i != action || s != slot) && sameChord(chords[i][s], chord))
				return i;
	return -1;
}

bool
ShortcutParseSetting(const char *value)
{
	ensureLoaded();
	char id[64], text[SHORTCUT_CHORDS][64];
	if(sscanf(value, "%63s %63s %63s", id, text[0], text[1]) != 3)
		return false;
	for(int i = 0; i < SC_NUM_ACTIONS; i++){
		if(strcmp(defs[i].info.id, id) != 0)
			continue;
		ShortcutChord parsed[SHORTCUT_CHORDS];
		for(int s = 0; s < SHORTCUT_CHORDS; s++)
			if(!ParseShortcutChord(text[s], &parsed[s]))
				return false;
		for(int s = 0; s < SHORTCUT_CHORDS; s++)
			chords[i][s] = parsed[s];
		labels[0][0] = '\0';
		return true;
	}
	return false;
}

void
ShortcutWriteSettings(void *file)
{
	ensureLoaded();
	FILE *f = (FILE*)file;
	for(int i = 0; i < SC_NUM_ACTIONS; i++){
		if(ShortcutIsDefault(i))
			continue;
		char text[SHORTCUT_CHORDS][48];
		for(int s = 0; s < SHORTCUT_CHORDS; s++){
			ShortcutChordName(chords[i][s], text[s], sizeof(text[s]));
			if(text[s][0] == '\0')
				strcpy(text[s], "none");
		}
		fprintf(f, "shortcut %s %s %s\n", defs[i].info.id, text[0], text[1]);
	}
}
