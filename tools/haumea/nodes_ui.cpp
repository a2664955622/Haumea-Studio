#include "haumea.h"
#include "sapaths.h"
#include "workspace.h"
#include "icons.h"
#include "shortcuts.h"

#include <string.h>
#include <stdio.h>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

// The Nodes window: edits copies of NODES0-63.DAT for open.mp NPCs
// (NPC_PlayNode, NPC_OpenNode). Saving writes into a folder of your choice,
// such as a server's scriptfiles/NPCs/nodes, never into the game's files.

namespace SAPaths {

enum NodesMode {
	NODES_SELECT,
	NODES_ADD_VEHICLE,
	NODES_ADD_PED,
	NODES_LINK,
};

static int gMode = NODES_SELECT;
static bool gChain = true;
static char gFolder[1024];
static bool gFolderRead;
static int gGotoArea, gGotoPoint;

static const ImVec4 WARN_COLOR(1.0f, 0.6f, 0.4f, 1.0f);

static void
readFolderSetting(void)
{
	if(gFolderRead)
		return;
	gFolderRead = true;
	char path[1024];
	if(!GetHaumeaDataPath(path, sizeof(path), "nodes_folder.txt"))
		return;
	FILE *f = fopen(path, "r");
	if(f == nil)
		return;
	if(fgets(gFolder, sizeof(gFolder), f)){
		size_t n = strlen(gFolder);
		while(n > 0 && (gFolder[n-1] == '\n' || gFolder[n-1] == '\r'))
			gFolder[--n] = '\0';
	}
	fclose(f);
}

// Without a saved folder, the workspace's scriptfiles/NPCs/nodes.
static void
defaultFolderFromWorkspace(void)
{
	if(gFolder[0] == '\0' && GetWorkspaceDir()[0])
		snprintf(gFolder, sizeof(gFolder), "%s", WorkspacePath("scriptfiles/NPCs/nodes").c_str());
}

static void
writeFolderSetting(void)
{
	char path[1024];
	if(!GetHaumeaDataPath(path, sizeof(path), "nodes_folder.txt"))
		return;
	FILE *f = fopen(path, "w");
	if(f == nil)
		return;
	fprintf(f, "%s\n", gFolder);
	fclose(f);
}

static void
showNodeKind(bool vehicle)
{
	if(vehicle)
		gRenderSaCarPaths = true;
	else
		gRenderSaPedPaths = true;
}

static void
jumpToNode(Node *node)
{
	rw::V3d pos;
	if(!GetNodeInfo(node, nil, nil, nil, &pos))
		return;
	selectedNode = node;
	TheCamera.setTarget(pos);
	TheCamera.setDistanceFromTarget(25.0f);
}

bool
HandleNodesViewportClick(void)
{
	if(gMode == NODES_SELECT)
		return false;
	if(CPad::IsMButtonClicked(3) || CPad::IsMButtonClicked(2) || ShortcutPressed(SC_CANCEL)){
		gMode = NODES_SELECT;
		return true;
	}
	if(!CPad::IsMButtonClicked(1))
		return true;

	if(gMode == NODES_LINK){
		Node *target = hoveredNode;
		if(target == nil){
			Toast(TOAST_SELECTION, "Click a node to link it");
			return true;
		}
		if(selectedNode == nil || selectedNode == target){
			selectedNode = target;
			return true;
		}
		Node *from = selectedNode;
		if(AreNodesLinked(from, target)){
			UnlinkNodes(from, target);
			Toast(TOAST_SPAWN, "Unlinked the nodes");
		}else{
			const char *reason = nil;
			if(LinkNodes(from, target, &reason))
				Toast(TOAST_SPAWN, "Linked the nodes");
			else
				Toast(TOAST_SPAWN, "Can't link: %s", reason ? reason : "unknown");
		}
		// Linking doesn't move nodes in memory, so target is still valid.
		// The selection moves on, so a route is clicked node by node.
		selectedNode = target;
		return true;
	}

	bool vehicle = gMode == NODES_ADD_VEHICLE;
	rw::V3d hit, normal;
	GetPlacementSurfaceHit(&hit, &normal);
	Node *linkTo = gChain && selectedNode && IsVehicleNode(selectedNode) == vehicle ? selectedNode : nil;
	const char *reason = nil;
	Node *added = AddNode(hit, vehicle, linkTo, &reason);
	if(added == nil)
		Toast(TOAST_SPAWN, "Can't add a node: %s", reason ? reason : "unknown");
	else if(reason)
		Toast(TOAST_SPAWN, "Added the node, but couldn't link it: %s", reason);
	showNodeKind(vehicle);
	return true;
}

static void
drawModeButton(const char *label, int mode, const char *tooltip)
{
	bool active = gMode == mode;
	if(active)
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	if(ImGui::Button(label))
		gMode = active ? NODES_SELECT : mode;
	if(active)
		ImGui::PopStyleColor();
	ImGui::SetItemTooltip("%s", tooltip);
}

static void
drawSelectedNode(void)
{
	Node *node = selectedNode;
	int area = 0, point = 0;
	bool vehicle = false;
	rw::V3d pos;
	if(!GetNodeInfo(node, &area, &point, &vehicle, &pos)){
		ImGui::TextDisabled("No node selected. Click one in the viewport.");
		return;
	}
	ImGui::Text("%s node %d in NODES%d.DAT", vehicle ? "Vehicle" : "Ped", point, area);
	ImGui::SetItemTooltip("In Pawn: NPC_OpenNode(%d), then NPC_PlayNode(npcid, %d, ...).\n"
		"The point id is the node's place in its file; adding or deleting\n"
		"nodes in that file renumbers the ones after it.", area, area);

	float p[3] = { pos.x, pos.y, pos.z };
	if(ImGui::DragFloat3("Position", p, 0.125f, -3000.0f, 3000.0f, "%.3f")){
		rw::V3d np = { p[0], p[1], p[2] };
		SetSelectedNodePosition(np, false);
	}
	if(ImGui::IsItemDeactivatedAfterEdit())
		CommitSelectedNodeEdit();
	ImGui::SetItemTooltip("Stored in steps of 0.125. A node can't leave its 750x750 area;\n"
		"press W to drag it with the gizmo instead.");

	Node *links[16];
	int numLinks = GetNodeLinks(node, links, 16);
	ImGui::Text("Links: %d / 15", numLinks);
	for(int i = 0; i < numLinks; i++){
		int la = 0, lp = 0;
		GetNodeInfo(links[i], &la, &lp, nil, nil);
		ImGui::PushID(i);
		char label[48];
		snprintf(label, sizeof(label), "%d:%d", la, lp);
		if(ImGui::SmallButton(label))
			jumpToNode(links[i]);
		ImGui::SetItemTooltip("Select and go to this node.");
		ImGui::SameLine();
		if(ImGui::SmallButton(ICON_FA_XMARK " Unlink")){
			UnlinkNodes(node, links[i]);
			ImGui::PopID();
			return;
		}
		if(la != area){
			ImGui::SameLine();
			ImGui::TextDisabled("(other file)");
			ImGui::SetItemTooltip("An NPC reaching this link leaves NODES%d.DAT;\nopen.mp then reports a node change.", area);
		}
		ImGui::PopID();
	}

	if(ImGui::Button(ICON_FA_TRASH " Delete node"))
		DeleteNode(node);
	ImGui::SetItemTooltip("Removes the node and its links. Later nodes in NODES%d.DAT move up one point id.", area);
}

void
DrawNodesWindow(bool *open)
{
	if(open == nil || !*open)
		return;
	readFolderSetting();
	defaultFolderFromWorkspace();
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 24.0f, 0.0f), ImGuiCond_FirstUseEver);
	if(!ImGui::Begin(ICON_FA_LOCATION_DOT " Nodes###nodes", open)){
		ImGui::End();
		return;
	}
	if(!*open)
		gMode = NODES_SELECT;

	UiBadge("open.mp only", "open.mp NPCs walk and drive along NODES0-63.DAT with NPC_PlayNode.\n"
		"SA-MP and the game itself don't read the files you save here.");

	if(!isSA() || !NodesLoaded()){
		ImGui::TextColored(WARN_COLOR, "The game's NODES*.DAT files couldn't be loaded.");
		ImGui::End();
		return;
	}

	ImGui::Checkbox("Vehicle nodes", &gRenderSaCarPaths);
	ImGui::SameLine();
	ImGui::Checkbox("Ped nodes", &gRenderSaPedPaths);
	ImGui::SameLine();
	ImGui::Checkbox("Area grid", &gRenderSaAreaGrid);
	ImGui::SetItemTooltip("Each 750x750 square is one NODES<id>.DAT file.");

	const char *source = GetFolderSource();
	ImGui::TextDisabled("Editing: %s", source ? source : "a copy of the game's nodes");
	if(source)
		ImGui::SetItemTooltip("Files missing from that folder came from the game.");

	ImGui::SeparatorText("Tools");
	drawModeButton(ICON_FA_ARROW_POINTER " Select", NODES_SELECT,
		"Click nodes to select them; W moves the selected node.");
	ImGui::SameLine();
	drawModeButton(ICON_FA_PLUS " Vehicle", NODES_ADD_VEHICLE,
		"Click the ground to add vehicle nodes. Right-click or Esc stops.");
	ImGui::SameLine();
	drawModeButton(ICON_FA_PLUS " Ped", NODES_ADD_PED,
		"Click the ground to add ped nodes. Right-click or Esc stops.");
	ImGui::SameLine();
	drawModeButton(ICON_FA_LINK " Link", NODES_LINK,
		"Click one node, then another, to link them.\n"
		"Clicking two linked nodes unlinks them. Right-click or Esc stops.");
	if(gMode == NODES_ADD_VEHICLE || gMode == NODES_ADD_PED){
		ImGui::Checkbox("Link each new node to the previous one", &gChain);
		showNodeKind(gMode == NODES_ADD_VEHICLE);
	}else if(gMode == NODES_LINK){
		ImGui::TextDisabled(selectedNode ? "Click the node to link to." : "Click the first node.");
		ImGui::SetItemTooltip("Vehicle nodes link to vehicle nodes, ped to ped.");
	}

	ImGui::SeparatorText("Selected node");
	drawSelectedNode();

	if(ImGui::CollapsingHeader("Go to node")){
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
	if(ImGui::InputInt("File##goarea", &gGotoArea))
		gGotoArea = clamp(gGotoArea, 0, 63);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
	if(ImGui::InputInt("Point##gopoint", &gGotoPoint))
		gGotoPoint = clamp(gGotoPoint, 0, 65535);
	ImGui::SameLine();
	if(ImGui::Button("Go")){
		Node *node = FindNode(gGotoArea, gGotoPoint);
		if(node){
			jumpToNode(node);
			showNodeKind(IsVehicleNode(node));
		}else
			Toast(TOAST_SELECTION, "NODES%d.DAT has no point %d", gGotoArea, gGotoPoint);
	}
	int veh = 0, ped = 0, lnk = 0;
	GetAreaCounts(gGotoArea, &veh, &ped, &lnk);
	ImGui::TextDisabled("NODES%d.DAT: %d vehicle, %d ped nodes (points 0-%d are vehicle)",
		gGotoArea, veh, ped, veh > 0 ? veh-1 : 0);
	}

	ImGui::SeparatorText("Files");
	int dirty = GetDirtyAreaCount();
	if(dirty > 0)
		ImGui::TextColored(WARN_COLOR, "%d file(s) changed and not saved", dirty);
	else
		ImGui::TextDisabled("No unsaved changes");

	ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 5.0f);
	if(ImGui::InputTextWithHint("##nodesfolder", "server/scriptfiles/NPCs/nodes", gFolder, sizeof(gFolder)))
		writeFolderSetting();
	ImGui::SameLine();
	if(ImGui::Button("Browse...") && PickFolderDialog(gFolder, sizeof(gFolder),
	   "Folder for NODES*.DAT (a server's scriptfiles/NPCs/nodes)", gFolder))
		writeFolderSetting();
	bool gameFolder = gFolder[0] && IsGameNodesFolder(gFolder);
	if(gameFolder)
		ImGui::TextColored(WARN_COLOR, "That's the game's own data. Pick a server's scriptfiles/NPCs/nodes.");

	if(gFolder[0] == '\0' || gameFolder)
		ImGui::BeginDisabled();
	if(ImGui::Button(ICON_FA_FLOPPY_DISK " Save")){
		int saved = 0;
		char err[512];
		if(SaveAreasToFolder(gFolder, &saved, err, sizeof(err)))
			Toast(TOAST_SAVE, "Saved %d node files to %s", saved, gFolder);
		else
			Toast(TOAST_SAVE, "Couldn't save the nodes: %s", err);
	}
	ImGui::SetItemTooltip("Writes all 64 NODES*.DAT files to this folder. open.mp loads them when the\n"
		"server starts.");
	ImGui::SameLine();
	if(ImGui::Button(ICON_FA_FOLDER_OPEN " Load")){
		if(dirty > 0)
			ImGui::OpenPopup("Discard node changes?");
		else{
			int loaded = 0;
			char err[512];
			if(LoadAreasFromFolder(gFolder, &loaded, err, sizeof(err)))
				Toast(TOAST_SAVE, "Loaded %d node files", loaded);
			else
				Toast(TOAST_SAVE, "Couldn't load the nodes: %s", err);
		}
	}
	ImGui::SetItemTooltip("Edit the NODES*.DAT files already in that folder.");
	if(gFolder[0] == '\0' || gameFolder)
		ImGui::EndDisabled();
	// On a narrow panel the last button goes to the next line instead of off the edge.
	const char *gameLabel = ICON_FA_ROTATE_LEFT " Game nodes";
	float gameWidth = ImGui::CalcTextSize(gameLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	ImGui::SameLine();
	if(ImGui::GetContentRegionAvail().x < gameWidth)
		ImGui::NewLine();
	if(ImGui::Button(gameLabel)){
		if(dirty > 0)
			ImGui::OpenPopup("Discard node changes?##game");
		else
			ReloadGameNodes();
	}
	ImGui::SetItemTooltip("Start again from the game's own nodes.");

	if(ImGui::BeginPopupModal("Discard node changes?", nil, ImGuiWindowFlags_AlwaysAutoResize)){
		ImGui::Text("%d changed file(s) haven't been saved.", dirty);
		if(ImGui::Button("Discard and load")){
			int loaded = 0;
			char err[512];
			if(LoadAreasFromFolder(gFolder, &loaded, err, sizeof(err)))
				Toast(TOAST_SAVE, "Loaded %d node files", loaded);
			else
				Toast(TOAST_SAVE, "Couldn't load the nodes: %s", err);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if(ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	if(ImGui::BeginPopupModal("Discard node changes?##game", nil, ImGuiWindowFlags_AlwaysAutoResize)){
		ImGui::Text("%d changed file(s) haven't been saved.", dirty);
		if(ImGui::Button("Discard")){
			ReloadGameNodes();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if(ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::End();
}

}
