// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

// The start screen (startscreen.cpp): workspaces, their maps and the game
// folder, at launch and from File > Switch Workspace.

// At launch, after loading: not for tests, scripts, the agent, --open, or
// with "Open the last workspace automatically" on.
bool StartScreenWantedAtLaunch(void);
void ShowStartScreen(void);
bool StartScreenOpen(void);
// Inside an ImGui frame. Before the game is loaded (first run) it is the
// setup window and Continue loads the game; after, a modal over the editor.
void DrawStartScreen(bool gameLoaded);

// A folder in the system's file manager (Explorer, Finder, xdg-open).
void OpenInFileManager(const std::string &path);
