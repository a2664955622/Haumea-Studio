// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "i18n.h"
#include "telemetry.h"
#include "workspace.h"
#include "startscreen.h"
#include "icons.h"
#include "version.h"
#include "versionutil.h"
#include "updater.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <algorithm>
#ifdef _WIN32
#include <direct.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

// The start screen: pick a workspace (the server being mapped) and a map in
// it, make a new workspace, or change the game folder. It replaces the
// first-run window and shows on every launch unless "Open the last
// workspace automatically" is on; File > Switch Workspace brings it back.

static bool gOpen;
static bool gOpenRequested;	// open the modal on the next frame
static bool gShownThisRun;	// first run: chosen before loading, so not again after
static std::string gSelected;	// the workspace whose maps are listed
static std::string gSelectedMap;
static char gGameField[1024];
static bool gRestartNeeded;
static bool gLoaded;	// the game is loaded: maps can open
static float gBodyBottom;	// screen y where the two lists end, above the game folder

// What is left of the lists' height from here.
static ImVec2
listSize(void)
{
	return ImVec2(0.0f, std::max(ImGui::GetFrameHeight() * 2.0f, gBodyBottom - ImGui::GetCursorScreenPos().y));
}
static const char *TITLE = N_("Welcome to Haumea Studio");

// ---- When ----

static bool
hasArgument(const char *name)
{
	for(int i = 1; i < sk::args.argc; i++)
		if(strcmp(sk::args.argv[i], name) == 0)
			return true;
	return false;
}

bool
StartScreenWantedAtLaunch(void)
{
	static int wanted = -1;
	if(wanted < 0){
		// Tests, scripts, the agent and a map given on the command line go
		// straight to the editor, as before. HAUMEA_START_SCREEN=1 shows it
		// anyway (its own UI script), 0 never.
		const char *env = getenv("HAUMEA_START_SCREEN");
		if(SettingsAreReadOnly())	// command-line modes (--pawn-roundtrip...), whatever the env
			wanted = 0;
		else if(env && env[0])
			wanted = !gShownThisRun && atoi(env) != 0;
		else
			wanted = !gShownThisRun && !GetOpenLastWorkspace() && !IsAutomatedRun() && !UiScriptActive() &&
				!hasArgument("--agent-socket") && !hasArgument("--open") && !hasArgument("--textdraw-roundtrip");
	}
	return wanted == 1;
}

void
ShowStartScreen(void)
{
	gOpenRequested = true;
}

bool
StartScreenOpen(void)
{
	return gOpen || gOpenRequested;
}

// ---- Files ----

static std::string
baseName(const std::string &path)
{
	size_t end = path.find_last_not_of("/\\");
	if(end == std::string::npos)
		return path;
	size_t slash = path.find_last_of("/\\", end);
	return path.substr(slash == std::string::npos ? 0 : slash + 1, end - (slash == std::string::npos ? 0 : slash + 1) + 1);
}

static bool
isDir(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static long long
modifiedTime(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 ? (long long)st.st_mtime : 0;
}

static bool
makeDir(const std::string &path)
{
#ifdef _WIN32
	return _mkdir(path.c_str()) == 0 || isDir(path);
#else
	return mkdir(path.c_str(), 0755) == 0 || isDir(path);
#endif
}

// "today", "3 days ago", or the date.
static std::string
whenText(long long when)
{
	if(when <= 0)
		return "";
	long long days = ((long long)time(nil) - when) / 86400;
	if(days <= 0) return _("today");
	if(days == 1) return _("yesterday");
	if(days < 30){
		char ago[64];
		snprintf(ago, sizeof(ago), _("%d days ago"), (int)days);
		return ago;
	}
	time_t t = (time_t)when;
	char buf[32];
	strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&t));
	return buf;
}

void
OpenInFileManager(const std::string &path)
{
#ifdef _WIN32
	ShellExecuteA(nil, "open", path.c_str(), nil, nil, SW_SHOWNORMAL);
#else
	std::string quoted = "'";
	for(char c : path)
		quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
	quoted += "'";
#ifdef __APPLE__
	std::string command = "open " + quoted + " >/dev/null 2>&1 &";
#else
	std::string command = "xdg-open " + quoted + " >/dev/null 2>&1 &";
#endif
	if(system(command.c_str()) != 0)
		log("StartScreen: could not open %s\n", path.c_str());
#endif
}

// Under dir, on a path boundary.
static bool
isInside(const std::string &path, const std::string &dir)
{
	if(dir.empty() || path.size() <= dir.size() || path.compare(0, dir.size(), dir) != 0)
		return false;
	char c = path[dir.size()];
	return c == '/' || c == '\\';
}

// ---- The game folder ----

// SA-MP's client in the game folder: its version string ("0.3.7-R5",
// "0.3.DL-R1"), "" when it has none, or nil-equivalent false without it.
static bool
readSampClientVersion(std::string *version)
{
	version->clear();
	FILE *f = fopen(GamePath("samp.dll").c_str(), "rb");
	if(f == nil)
		return false;
	std::string data;
	char buffer[65536];
	size_t n;
	while((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
		data.append(buffer, n);
	fclose(f);
	// The longest of the "0.3.x" strings the client prints, e.g. "SA-MP 0.3.7-R5 Started".
	for(size_t at = data.find("0.3."); at != std::string::npos; at = data.find("0.3.", at + 4)){
		size_t end = at;
		while(end < data.size() && end - at < 16 && (isalnum((unsigned char)data[end]) || data[end] == '.' || data[end] == '-'))
			end++;
		std::string candidate = data.substr(at, end - at);
		bool known = candidate.compare(0, 5, "0.3.7") == 0 || candidate.compare(0, 6, "0.3.DL") == 0;
		if(known && candidate.size() > version->size())
			*version = candidate;
	}
	return true;
}

// Read once per game folder: samp.dll is a few MB.
static bool
sampClientVersion(std::string *version)
{
	static std::string forGame, cached;
	static bool found;
	if(forGame != GetGameDir()){
		forGame = GetGameDir();
		found = readSampClientVersion(&cached);
	}
	*version = cached;
	return found;
}

// Custom models the workspace's models/artconfig.txt adds.
static int
readCustomModelCount(const std::string &workspace)
{
	if(workspace.empty())
		return 0;
	FILE *f = fopen((workspace + "/models/artconfig.txt").c_str(), "r");
	if(f == nil)
		return 0;
	int count = 0;
	char line[1024];
	while(fgets(line, sizeof(line), f)){
		const char *p = line;
		while(*p == ' ' || *p == '\t') p++;
		if(strncmp(p, "AddSimpleModel", 14) == 0 || strncmp(p, "AddCharModel", 12) == 0)
			count++;
	}
	fclose(f);
	return count;
}

// The same, read again every few seconds rather than every frame.
static int
countCustomModels(const std::string &workspace)
{
	static std::string forWorkspace;
	static int cached;
	static double readAt = -100.0;
	if(forWorkspace == workspace && ImGui::GetTime() - readAt < 3.0)
		return cached;
	forWorkspace = workspace;
	readAt = ImGui::GetTime();
	cached = readCustomModelCount(workspace);
	return cached;
}

static void
gameSection(bool gameLoaded)
{
	ImGui::SeparatorText(TI(ICON_FA_GAMEPAD, "GTA San Andreas"));
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(_("Browse...")).x -
		ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().ItemSpacing.x);
	ImGui::InputText("##game", gGameField, sizeof(gGameField));
	ImGui::SameLine();
	ImGui::PushID("game");
	bool browse = ImGui::Button(_("Browse..."));
	ImGui::PopID();
	if(browse){
		char picked[1024];
		if(PickFolderDialog(picked, sizeof(picked), _("GTA San Andreas folder"), gGameField))
			snprintf(gGameField, sizeof(gGameField), "%s", picked);
	}
	std::string problem;
	bool ok = CheckGameDir(gGameField, &problem);
	bool changed = gameLoaded && ok && strcmp(gGameField, GetGameDir()) != 0;
	if(!ok){
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
		ImGui::TextWrapped("%s", problem.c_str());
		ImGui::PopStyleColor();
	}
	else if(changed){
		if(ImGui::Button(_("Use this game folder"))){
			SetGameDir(gGameField, nil);
			gRestartNeeded = true;
		}
	}else if(gameLoaded){
		// What the editor found in the folder it loaded.
		std::string version;
		if(sampClientVersion(&version))
			ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.0f), TI(ICON_FA_CHECK, "SA-MP %s found"),
				version.empty() ? _("client") : version.c_str());
		else
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), _("No SA-MP client: SA-MP's own objects won't show."));
		if(sampClientVersion(&version) && version.empty()){
			ImGui::SameLine();
			ImGui::TextDisabled(_("(version unknown)"));
		}
		ImGui::SameLine();
		int custom = countCustomModels(gSelected);
		ImGui::TextDisabled(_("|  0.3.DL custom models in this workspace: %d"), custom);
		ImGui::SetItemTooltip(_("From the workspace's models/artconfig.txt."));
	}else
		ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.0f), TI(ICON_FA_CHECK, "GTA San Andreas found."));
	if(gRestartNeeded){
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), _("The new game folder loads when Haumea restarts."));
		ImGui::SameLine();
		if(ImGui::SmallButton(_("Restart now")))
			RestartHaumea();
	}
}

// ---- Workspaces ----

static bool gNewWorkspace;
static char gNewParent[1024], gNewName[128];

static void
newWorkspaceForm(void)
{
	ImGui::TextDisabled(_("A server folder with gamemodes, filterscripts and scriptfiles."));
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(_("Browse...")).x -
		ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().ItemSpacing.x);
	ImGui::InputTextWithHint("##parent", _("In this folder"), gNewParent, sizeof(gNewParent));
	ImGui::SameLine();
	ImGui::PushID("parent");
	bool browse = ImGui::Button(_("Browse..."));
	ImGui::PopID();
	if(browse){
		char picked[1024];
		if(PickFolderDialog(picked, sizeof(picked), _("Where the workspace goes"), gNewParent))
			snprintf(gNewParent, sizeof(gNewParent), "%s", picked);
	}
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##name", _("Name, e.g. my-server"), gNewName, sizeof(gNewName));
	bool nameOk = gNewName[0] && strpbrk(gNewName, "/\\:*?\"<>|") == nil && strcmp(gNewName, ".") && strcmp(gNewName, "..");
	bool parentOk = isDir(gNewParent);
	std::string path = std::string(gNewParent) + "/" + gNewName;
	if(gNewName[0] && !nameOk)
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), _("A name can't have / \\ : * ? \" < > |"));
	else if(gNewParent[0] && !parentOk)
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), _("That folder doesn't exist."));
	else if(nameOk && parentOk && isDir(path))
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), _("%s exists: it is used as it is."), gNewName);
	ImGui::BeginDisabled(!nameOk || !parentOk);
	if(ImGui::Button(_("Create workspace"))){
		bool made = makeDir(path);
		static const char *folders[] = { "gamemodes", "filterscripts", "scriptfiles" };
		for(const char *folder : folders)
			made = made && makeDir(path + "/" + folder);
		if(made){
			SetWorkspaceDir(path.c_str());
			gSelected = GetWorkspaceDir();
			gNewWorkspace = false;
			gNewName[0] = '\0';
		}else
			Toast(TOAST_SAVE, _("Could not create %s"), path.c_str());
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::PushID("new");
	if(ImGui::Button(_("Cancel")))
		gNewWorkspace = false;
	ImGui::PopID();
}

static void
workspaceList(void)
{
	if(ImGui::Button(LI(ICON_FA_PLUS, "New workspace...")))
		gNewWorkspace = !gNewWorkspace;
	ImGui::SameLine();
	if(ImGui::Button(LI(ICON_FA_FOLDER_OPEN, "Open folder as workspace..."))){
		char picked[1024];
		if(PickFolderDialog(picked, sizeof(picked), _("Server folder"), gSelected.c_str())){
			SetWorkspaceDir(picked);
			gSelected = GetWorkspaceDir();
		}
	}
	if(gNewWorkspace)
		newWorkspaceForm();

	std::vector<RecentWorkspace> recent = GetRecentWorkspaceList();
	ImGui::BeginChild("workspaces", listSize(), ImGuiChildFlags_Borders);
	if(recent.empty())
		ImGui::TextDisabled(_("No workspaces yet: make one or open a server folder."));
	float buttons = ImGui::GetFrameHeight() * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
	for(size_t i = 0; i < recent.size(); i++){
		const RecentWorkspace &w = recent[i];
		ImGui::PushID(w.path.c_str());
		bool missing = !isDir(w.path);
		std::string name = baseName(w.path);
		std::string label = (w.pinned ? std::string(ICON_FA_STAR " ") : std::string()) + name;
		ImVec2 start = ImGui::GetCursorPos();
		if(ImGui::Selectable("##row", gSelected == w.path, ImGuiSelectableFlags_AllowDoubleClick |
			ImGuiSelectableFlags_AllowOverlap, ImVec2(ImGui::GetContentRegionAvail().x - buttons,
			ImGui::GetTextLineHeightWithSpacing() * 2.0f))){
			gSelected = w.path;
			gSelectedMap.clear();
			if(ImGui::IsMouseDoubleClicked(0) && !missing && gLoaded){
				SetWorkspaceDir(w.path.c_str());
				gOpen = false;
			}
		}
		ImGui::SetItemTooltip(gLoaded ? _("%s\nDouble-click to open it.") : "%s", w.path.c_str());
		ImGui::SetCursorPos(ImVec2(start.x + ImGui::GetStyle().ItemSpacing.x, start.y));
		ImGui::TextUnformatted(label.c_str());
		if(missing){
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), _("(missing)"));
		}else if(w.path == GetWorkspaceDir()){
			ImGui::SameLine();
			ImGui::TextDisabled(_("(open)"));
		}
		ImGui::SetCursorPosX(start.x + ImGui::GetStyle().ItemSpacing.x);
		std::string when = whenText(w.lastOpened);
		ImGui::TextDisabled("%s%s%s", w.path.c_str(), when.empty() ? "" : "  ·  ", when.c_str());
		float right = ImGui::GetWindowContentRegionMax().x - buttons;
		ImGui::SetCursorPos(ImVec2(right, start.y));
		if(!w.pinned)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		if(ImGui::Button(w.pinned ? ICON_FA_STAR "##unpin" : ICON_FA_STAR "##pin"))
			PinRecentWorkspace(w.path.c_str(), !w.pinned);
		if(!w.pinned)
			ImGui::PopStyleColor();
		ImGui::SetItemTooltip(w.pinned ? _("Unpin") : _("Pin: keep it at the top of the list"));
		ImGui::SameLine();
		ImGui::BeginDisabled(missing);
		if(ImGui::Button(ICON_FA_FOLDER_OPEN "##folder"))
			OpenInFileManager(w.path);
		ImGui::EndDisabled();
		ImGui::SetItemTooltip(_("Open the folder"));
		ImGui::SameLine();
		if(ImGui::Button(ICON_FA_XMARK "##remove")){
			RemoveRecentWorkspace(w.path.c_str());
			if(gSelected == w.path)
				gSelected.clear();
		}
		ImGui::SetItemTooltip(_("Remove from the list (the folder stays)"));
		// An item for the row's whole height, so the list grows with it.
		ImGui::SetCursorPos(start);
		ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeightWithSpacing() * 2.0f));
		ImGui::PopID();
	}
	ImGui::EndChild();
}

// ---- Maps ----

static void
openSelected(const std::string &map)
{
	if(!gSelected.empty() && gSelected != GetWorkspaceDir())
		SetWorkspaceDir(gSelected.c_str());
	if(!map.empty())
		HandleMapFileDrop(map.c_str());
	gOpen = false;
}

static void
mapList(bool gameLoaded)
{
	std::vector<std::string> maps;
	const std::vector<std::string> &recent = GetRecentMapPaths();
	for(const std::string &path : recent)
		if(isInside(path, gSelected))
			maps.push_back(path);
	if(gSelected.empty())
		ImGui::TextDisabled(_("Pick a workspace to see its maps."));
	else if(maps.empty())
		ImGui::TextDisabled(_("No recent maps in %s yet."), baseName(gSelected).c_str());
	else if(!gameLoaded)
		ImGui::TextDisabled(_("Maps open once the game folder is loaded."));
	float tile = ImGui::GetFontSize() * 7.5f;
	float avail = ImGui::GetContentRegionAvail().x;
	int perRow = std::max(1, (int)((avail + ImGui::GetStyle().ItemSpacing.x) / (tile + ImGui::GetStyle().ItemSpacing.x)));
	for(size_t i = 0; i < maps.size() && gameLoaded; i++){
		const std::string &path = maps[i];
		ImGui::PushID(path.c_str());
		if(i % perRow)
			ImGui::SameLine();
		ImGui::BeginGroup();
		ImVec2 at = ImGui::GetCursorScreenPos();
		bool selected = gSelectedMap == path;
		std::string name = baseName(path);
		if(ImGui::Selectable(("##" + name).c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(tile, tile))){
			gSelectedMap = path;
			if(ImGui::IsMouseDoubleClicked(0))
				openSelected(path);
		}
		ImGui::SetItemTooltip(_("%s\nDouble-click to open it."), path.c_str());
		ImDrawList *dl = ImGui::GetWindowDrawList();
		rw::Texture *thumb = GetPrefabThumbnailTexture(path.c_str());
		ImVec2 pad(4.0f, 4.0f);
		if(thumb)
			dl->AddImage((ImTextureID)thumb, ImVec2(at.x + pad.x, at.y + pad.y), ImVec2(at.x + tile - pad.x, at.y + tile - pad.y));
		else{
			dl->AddRectFilled(ImVec2(at.x + pad.x, at.y + pad.y), ImVec2(at.x + tile - pad.x, at.y + tile - pad.y),
				IM_COL32(34, 34, 34, 255));
			ImVec2 icon = ImGui::CalcTextSize(ICON_FA_FILE);
			dl->AddText(ImVec2(at.x + (tile - icon.x) * 0.5f, at.y + (tile - icon.y) * 0.5f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_FA_FILE);
		}
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile);
		ImGui::TextUnformatted(name.c_str());
		ImGui::TextDisabled("%s", whenText(modifiedTime(path)).c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
		ImGui::PopID();
	}
}

// ---- The screen ----

static void
contents(bool gameLoaded)
{
	gLoaded = gameLoaded;
	if(gSelected.empty() && !GetRecentWorkspaceList().empty())
		gSelected = GetWorkspaceDir()[0] ? GetWorkspaceDir() : GetRecentWorkspaceList()[0].path;
	ImGui::Text(_("Haumea Studio %s"), HaumeaVersionDisplay());
	ImGui::SameLine();
	ImGui::TextDisabled(_("the all-in-one editor for open.mp and SA-MP"));
	ImGui::TextDisabled(_("A workspace is the open.mp or SA-MP server you map for; its maps and models live there."));
	// An update, or what the last one did: the updater's banner, in here
	// rather than dimmed behind this window.
	if(UpdaterHasNews()){
		ImGui::Spacing();
		UpdaterDrawInline();
		if(UpdaterWhatsNewRequested()){
			ImGui::BeginChild("whatsnew", ImVec2(0, ImGui::GetFontSize() * 8.0f), ImGuiChildFlags_Borders);
			UpdaterDrawWhatsNewInline();
			ImGui::EndChild();
		}
	}
	TelemetryDrawConsent();
	ImGui::Spacing();

	// The game folder's heading, field and status line, then the buttons.
	float footer = ImGui::GetFrameHeightWithSpacing() * 3.0f + ImGui::GetTextLineHeightWithSpacing() * 2.0f +
		ImGui::GetStyle().ItemSpacing.y * 3.0f;
	float height = std::max(ImGui::GetFontSize() * 10.0f, ImGui::GetContentRegionAvail().y - footer);
	gBodyBottom = ImGui::GetCursorScreenPos().y + height;
	if(ImGui::BeginTable("start", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0, height))){
		ImGui::TableSetupColumn("Workspaces", ImGuiTableColumnFlags_WidthStretch, 0.45f);
		ImGui::TableSetupColumn("Maps", ImGuiTableColumnFlags_WidthStretch, 0.55f);
		ImGui::TableNextColumn();
		ImGui::SeparatorText(TI(ICON_FA_GLOBE, "Workspaces"));
		workspaceList();
		ImGui::TableNextColumn();
		if(gSelected.empty())
			ImGui::SeparatorText(TI(ICON_FA_FILE, "Maps"));
		else{
			char heading[256];
			snprintf(heading, sizeof(heading), TI(ICON_FA_FILE, "Maps in %s"), baseName(gSelected).c_str());
			ImGui::SeparatorText(heading);
		}
		ImGui::BeginChild("maps", listSize());
		mapList(gameLoaded);
		ImGui::EndChild();
		ImGui::EndTable();
	}

	gameSection(gameLoaded);
	ImGui::Spacing();

	std::string problem;
	bool gameOk = gameLoaded || CheckGameDir(gGameField, &problem);
	ImGui::BeginDisabled(!gameOk);
	const char *open = !gameLoaded || gSelected.empty() ? _("Continue") :
		gSelectedMap.empty() ? _("Open workspace") : _("Open map");
	if(ImGui::Button(open, ImVec2(ImGui::GetFontSize() * 9.0f, 0))){
		if(!gameLoaded)
			FinishWorkspaceSetup(gGameField, gSelected.c_str());
		openSelected(gameLoaded ? gSelectedMap : std::string());
	}
	ImGui::SameLine();
	if(ImGui::Button(_("Continue without a workspace"))){
		gSelected.clear();
		if(!gameLoaded)
			FinishWorkspaceSetup(gGameField, "");
		else
			SetWorkspaceDir(nil);
		gOpen = false;
	}
	ImGui::EndDisabled();
	if(gameLoaded){
		ImGui::SameLine();
		if(ImGui::Button(_("Skip")))
			gOpen = false;
		ImGui::SetItemTooltip(_("Keep the workspace and map as they are (%s)."), GetWorkspaceDir()[0] ?
			GetWorkspaceDir() : _("no workspace"));
	}
	ImGui::SameLine();
	bool openLast = GetOpenLastWorkspace();
	if(ImGui::Checkbox(_("Open the last workspace automatically"), &openLast))
		SetOpenLastWorkspace(openLast);
	ImGui::SetItemTooltip(_("Skip this screen at start. File > Switch Workspace shows it again."));
}

void
DrawStartScreen(bool gameLoaded)
{
	static bool filled;
	if(!filled){
		filled = true;
		snprintf(gGameField, sizeof(gGameField), "%s", GetSuggestedGameDir());
	}
	ImGuiIO &io = ImGui::GetIO();
	ImVec2 size(std::min(io.DisplaySize.x - 40.0f, ImGui::GetFontSize() * 62.0f),
		std::min(io.DisplaySize.y - 40.0f, ImGui::GetFontSize() * 40.0f));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
	if(!gameLoaded){
		// First run, on the splash before loading: the game folder is needed.
		gShownThisRun = true;
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(size, ImGuiCond_Always);
		ImGui::Begin(L(TITLE), nil, flags);
		contents(false);
		ImGui::End();
		return;
	}
	if(gOpenRequested){
		gOpenRequested = false;
		gOpen = true;
		gShownThisRun = true;
		gSelected = GetWorkspaceDir();
		gSelectedMap.clear();
		ImGui::OpenPopup(L(TITLE));
	}
	if(!gOpen){
		TelemetryStartScreenClosed();
		return;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	if(!ImGui::BeginPopupModal(L(TITLE), nil, flags)){
		gOpen = false;
		return;
	}
	contents(true);
	if(!gOpen || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive())){
		gOpen = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
