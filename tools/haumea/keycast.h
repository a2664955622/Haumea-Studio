// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdio.h>
#include <string>

// Keycast: an overlay over the 3D view with the keys and mouse buttons the
// editor receives (for tutorials and recordings) and what Namaka is doing.
// While a text field has the keyboard only shortcut combos show, so typed
// text never ends up in a recording.

// Namaka's current action, already translated, without a "Namaka:" prefix.
// key: what the line is about ("place", "camera"); a call with the same key
// (or, without one, the same text) within 1.5 s of that line's last update
// replaces its text and restarts its fade instead of adding a line.
void KeycastAgentAction(const char *text, const char *key = nullptr);

// Once per frame, while the editor UI is built: reads the input, draws.
void KeycastFrame(void);

bool KeycastEnabled(void);	// Show pressed keys
void KeycastSetEnabled(bool on);

// What the overlay shows now, a line each, as drawn (UI scripts).
std::string KeycastShownText(void);

bool KeycastLoadSetting(const char *key, const char *value);	// false: not a keycast setting
void KeycastSaveSettings(FILE *f);
bool KeycastPreferences(void);	// Preferences > General; true when a setting changed
