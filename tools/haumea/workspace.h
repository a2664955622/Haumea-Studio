#pragma once

#include <string>
#include <vector>

// Where Haumea finds things. Haumea runs from its own folder: the game folder
// (GTA San Andreas) is only ever read, the workspace is the SA-MP/open.mp
// server being mapped, and settings and editor data live in the config
// folder (~/.config/haumea, %APPDATA%\Haumea).

// Reads --game/--workspace, the saved settings and the old drop-in layout,
// and moves <GTA>/haumea_data to the config folder once. Call first.
void InitWorkspace(void);

const char *GetGameDir(void);	// absolute; empty until one is chosen
const char *GetWorkspaceDir(void);	// absolute; empty when none is set
const char *GetConfigDir(void);	// absolute, created on demand

// game/<relative>, case-corrected like the game's own loader. Absolute paths
// are returned as they are.
std::string GamePath(const char *relative);
bool GamePath(const char *relative, char *dst, size_t size);
// workspace/<relative> (no case correction), or "" without a workspace.
std::string WorkspacePath(const char *relative);
// The workspace folder for relative if it exists, else the workspace itself,
// else "": a default folder for file dialogs.
std::string WorkspaceDefaultDir(const char *relative);
// Whether path is inside the game folder, which Haumea never writes to.
bool IsInGameDir(const char *path);

// A GTA San Andreas folder has gta_sa.exe, or models/gta3.img and data/.
bool CheckGameDir(const char *dir, std::string *problem);
// A server folder has gamemodes/, filterscripts/, scriptfiles/ or a server
// executable. Only a warning: any folder can be the workspace.
bool CheckWorkspaceDir(const char *dir, std::string *warning);

// Saved for the next start; the game folder is loaded once, so a new one
// applies after a restart.
bool SetGameDir(const char *dir, std::string *problem);
void SetWorkspaceDir(const char *dir);	// nil or "" for none
const std::vector<std::string> &GetRecentWorkspaces(void);

// True for --pawn-roundtrip and --no-save-settings: developer and test runs
// read the settings but never write them (settings, recent maps, window
// state, autosaves, the workspace).
bool SettingsAreReadOnly(void);

// Linux file dialogs run zenity or kdialog. Without either this shows a
// toast saying so and returns false; elsewhere it is always true.
bool FileDialogAvailable(void);
// Native folder picker; false when cancelled.
bool PickFolderDialog(char *dst, size_t size, const char *title, const char *startDir);

// First-run setup: true while no valid game folder is known. Draw the setup
// window inside an ImGui frame until it returns false.
bool WorkspaceSetupNeeded(void);
void DrawWorkspaceSetup(void);
// Preferences / File > Workspace widgets. Returns true when the game folder
// changed and Haumea needs a restart.
bool uiWorkspaceSettings(void);
