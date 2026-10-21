// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "selftest.h"
#include "editoricons.h"
#include "sapaths.h"
#include "textdraws.h"
#include "maptools.h"
#include "mapcheck.h"
#include "playerview.h"

#include <chrono>
#include <stdio.h>
#include <string.h>
#include <functional>

// Developer self-tests, run from the command line on an opened map. Each
// one writes "PASS name" / "FAIL name: why" lines and a final summary.

namespace {

FILE *gReport;
int gPassed, gFailed;

void
report(bool ok, const char *name, const char *why = "")
{
	(ok ? gPassed : gFailed)++;
	fprintf(gReport, "%s %s%s%s\n", ok ? "PASS" : "FAIL", name, ok || !why[0] ? "" : ": ", ok ? "" : why);
	fflush(gReport);
}

std::string
pawn(void)
{
	std::string code;
	BuildPawnCode(GetPawnMapPath(), (PawnCodeFormat)GetPawnMapFormat(), false, code);
	return code;
}

// The first exported element of a kind (and, for objects, not a vehicle).
ObjectInst*
findElement(int kind, bool vehicle = false, const char *name = nil)
{
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted || !PawnObjectNeedsExport(inst) || GetSampElementKind(inst) != kind ||
		   inst->m_isVehicle != vehicle)
			continue;
		const ObjectPawnProperties *props = GetObjectPawnProperties(inst);
		if(name && strcmp(props->npcName, name) != 0 && strcmp(props->name, name) != 0)
			continue;
		return inst;
	}
	return nil;
}

void
selectOnly(std::initializer_list<ObjectInst*> insts)
{
	ClearSelection();
	for(ObjectInst *inst : insts)
		if(inst)
			inst->Select();
}

// Applies an edit and checks it is exactly one undo step: undo gives the
// Pawn (and node data) from before byte for byte, redo the Pawn from after.
// Undoes it again at the end, so every test starts from the opened map.
void
undoTest(const char *name, std::function<bool(void)> edit, bool expectChange = true)
{
	std::string before = pawn();
	uint64 nodesBefore = SAPaths::NodesFingerprint();
	int position = GetUndoPosition();
	if(!edit()){
		report(false, name, "the edit could not be applied (missing element?)");
		return;
	}
	SAPaths::UpdateNodesUndo();
	std::string after = pawn();
	uint64 nodesAfter = SAPaths::NodesFingerprint();
	int steps = GetUndoPosition() - position;
	bool changed = after != before || nodesAfter != nodesBefore;
	if(!expectChange){
		report(steps == 0 && !changed, name, steps != 0 ? "recorded an undo step" : "changed the saved map");
		return;
	}
	if(!changed){
		report(false, name, "the edit changed nothing");
		return;
	}
	if(steps != 1){
		char why[64];
		snprintf(why, sizeof(why), "%d undo steps instead of 1", steps);
		report(false, name, why);
		for(int i = 0; i < steps; i++)
			Undo();
		return;
	}
	Undo();
	if(pawn() != before || SAPaths::NodesFingerprint() != nodesBefore){
		report(false, name, "undo did not restore the map exactly");
		return;
	}
	Redo();
	if(pawn() != after || SAPaths::NodesFingerprint() != nodesAfter){
		report(false, name, "redo did not give the edited map back");
		Undo();
		return;
	}
	Undo();
	report(true, name);
}

bool
editProperties(ObjectInst *inst, std::function<void(ObjectPawnProperties&)> change)
{
	if(inst == nil)
		return false;
	ObjectPawnProperties props = *GetObjectPawnProperties(inst);
	change(props);
	SetObjectPawnProperties(inst, props);
	return true;
}

double
seconds(std::function<void(void)> fn)
{
	auto start = std::chrono::steady_clock::now();
	fn();
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}	// namespace

int
RunUndoSelfTest(FILE *out)
{
	gReport = out;
	gPassed = gFailed = 0;
	SAPaths::EnsureLoaded();
	SAPaths::UpdateNodesUndo();	// takes the baseline

	ObjectInst *walker = findElement(SAMP_ELEMENT_NPC, false, "SWalker");
	ObjectInst *dancer = findElement(SAMP_ELEMENT_NPC, false, "SDancer");
	ObjectInst *point = findElement(SAMP_ELEMENT_PATHPOINT);
	ObjectInst *actor = findElement(SAMP_ELEMENT_ACTOR);
	ObjectInst *sound = findElement(SAMP_ELEMENT_SOUND);
	ObjectInst *label = findElement(SAMP_ELEMENT_LABEL);
	ObjectInst *object = findElement(SAMP_ELEMENT_OBJECT);
	ObjectInst *vehicle = nil;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted && inst->m_isVehicle && inst->m_vehiclePlate[0])
			vehicle = inst;
	}
	ObjectInst *plainCp = nil, *raceCp = nil;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted || GetSampElementKind(inst) != SAMP_ELEMENT_CHECKPOINT)
			continue;
		if(GetObjectPawnProperties(inst)->cpKind == SAMP_CHECKPOINT_RACE)
			raceCp = inst;
		else
			plainCp = inst;
	}
	std::vector<ObjectInst*> objects;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted && PawnObjectNeedsExport(inst) && !inst->m_isVehicle &&
		   GetSampElementKind(inst) == SAMP_ELEMENT_OBJECT && GetObjectPawnProperties(inst)->attachParent < 0)
			objects.push_back(inst);
	}

	undoTest("npc fields", [&]{ return editProperties(walker, [](ObjectPawnProperties &p){
		snprintf(p.npcName, sizeof(p.npcName), "Renamed");
		p.npcArmour = 25.0f;
		p.npcWeapon = 24;
	}); });
	undoTest("npc movement mode", [&]{ return editProperties(walker, [](ObjectPawnProperties &p){
		p.npcMoveType = 1;
		p.npcMoveSpeed = 0.3f;
		p.npcPathReversed = !p.npcPathReversed;
	}); });
	undoTest("npc switches to nodes", [&]{ return editProperties(walker, [](ObjectPawnProperties &p){
		p.npcPlaysNode = true;
		p.npcNode = 12;
		p.npcNodePoint = 4;
	}); });
	undoTest("path point order", [&]{
		// The first point goes to the end, so the path is walked differently.
		if(point == nil) return false;
		std::vector<ObjectInst*> points;
		GetNpcPathPoints(GetObjectPawnProperties(point)->pathName, points);
		if(points.size() < 2) return false;
		int last = GetObjectPawnProperties(points.back())->pathOrder;
		return editProperties(points.front(), [&](ObjectPawnProperties &p){ p.pathOrder = last + 1; });
	});
	undoTest("path point to another path", [&]{ return editProperties(point, [](ObjectPawnProperties &p){
		snprintf(p.pathName, sizeof(p.pathName), "other_path");
	}); });
	undoTest("vehicle colours, plate, paintjob, parts", [&]{
		if(vehicle == nil)
			return false;
		VehicleState before = GetVehicleState(vehicle);
		VehicleState s = before;
		s.color1 = 3;
		s.color2 = 5;
		s.paintjob = 0;
		snprintf(s.plate, sizeof(s.plate), "UNDO");
		memset(s.mods, 0, sizeof(s.mods));
		SetVehicleState(vehicle, s);
		UndoRecordVehicle(vehicle, before);
		return true;
	});
	undoTest("sound place", [&]{ return editProperties(sound, [](ObjectPawnProperties &p){
		p.soundRadius += 15.0f;
		p.soundId = 1139;
	}); });
	undoTest("checkpoint size", [&]{ return editProperties(plainCp, [](ObjectPawnProperties &p){
		p.cpSize = 6.0f;
	}); });
	undoTest("race checkpoint order", [&]{ return editProperties(raceCp, [](ObjectPawnProperties &p){
		p.cpOrder += 3;
	}); });
	undoTest("actor animation", [&]{ return editProperties(actor, [](ObjectPawnProperties &p){
		snprintf(p.animLib, sizeof(p.animLib), "PED");
		snprintf(p.animName, sizeof(p.animName), "IDLE_chat");
		p.animLoop = true;
	}); });
	undoTest("npc animation cleared", [&]{ return editProperties(dancer, [](ObjectPawnProperties &p){
		p.animLib[0] = p.animName[0] = '\0';
	}); });
	undoTest("label text", [&]{ return editProperties(label, [](ObjectPawnProperties &p){
		snprintf(p.labelText, sizeof(p.labelText), "Changed {FF0000}text");
	}); });
	{
		// The textdraw file has its own undo; the map's is not touched.
		int position = GetUndoPosition();
		NewTextDrawFile();
		TextDraw td;
		InitTextDraw(&td);
		td.text = "New";
		GetTextDraws().push_back(td);
		TextDrawsChanged();
		UpdateTextDrawsUndo();
		GetTextDraws()[0].text = "Undo me";
		GetTextDraws()[0].x += 10.0f;
		TextDrawsChanged();
		UpdateTextDrawsUndo();
		bool ok = UndoTextDraws() && GetTextDraws().size() == 1 && GetTextDraws()[0].text == "New" &&
			UndoTextDraws() && GetTextDraws().empty() &&
			RedoTextDraws() && RedoTextDraws() && GetTextDraws()[0].text == "Undo me" &&
			GetUndoPosition() == position;
		report(ok, "textdraw file undo", "the textdraw steps did not undo and redo on their own");
		NewTextDrawFile();
	}
	undoTest("material", [&]{
		if(object == nil)
			return false;
		ObjectMaterialOverride m = *GetObjectMaterialOverride(object, 1);
		m.textEnabled = true;
		snprintf(m.text, sizeof(m.text), "Undo");
		SetObjectMaterialOverride(object, 1, m, true);
		return true;
	});
	std::string error;
	undoTest("map tool: array", [&]{
		if(objects.size() < 2) return false;
		selectOnly({ objects[0], objects[1] });
		MapArrayParams params = {};
		params.count = 2;
		params.offset = { 0.0f, 6.0f, 0.0f };
		return MapToolsArray(params, &error) > 0;
	});
	undoTest("map tool: mirror", [&]{
		if(objects.size() < 2) return false;
		selectOnly({ objects[0], objects[1] });
		return MapToolsMirror(0, nil, false, &error) > 0;
	});
	undoTest("map tool: mirror a copy", [&]{
		if(objects.size() < 2) return false;
		selectOnly({ objects[0], objects[1] });
		float at = objects[0]->m_translation.x + 10.0f;
		return MapToolsMirror(0, &at, true, &error) > 0;
	});
	undoTest("map tool: align", [&]{
		if(objects.size() < 2) return false;
		selectOnly({ objects[0], objects[1] });
		return MapToolsAlign(1, 1, &error) > 0;
	});
	undoTest("place object", [&]{
		SetSpawnObjectId(1271);
		SpawnPlaceObject({ 1700.0f, -1890.0f, 13.5f });
		return true;
	});
	ObjectDef *custom = nil;
	for(int id = CUSTOM_MODEL_BASE + 1000; id <= CUSTOM_MODEL_BASE + 30000 && custom == nil; id++)
		if(IsCustomModel(id) && GetObjectDef(id))
			custom = GetObjectDef(id);
	if(custom){
		undoTest("place custom model", [&]{
			SetSpawnObjectId(custom->m_id);
			SpawnPlaceObject({ 1705.0f, -1890.0f, 13.5f });
			return true;
		});
	}else
		fprintf(gReport, "SKIP place custom model: the workspace has no custom models\n");
	undoTest("duplicate", [&]{
		selectOnly({ object });
		return object && DuplicateSelected() > 0;
	});
	undoTest("delete", [&]{
		selectOnly({ object, actor });
		DeleteSelected();
		return object != nil;
	});
	undoTest("hide is not an edit", [&]{
		selectOnly({ object });
		HideSelected();
		return true;
	}, false);
	undoTest("isolate is not an edit", [&]{
		selectOnly({ object });
		IsolateSelected();
		return true;
	}, false);
	ShowAllInstances();

	// The Nodes tool, on the game's own node files.
	if(SAPaths::NodesLoaded()){
		// Two neighbours of one kind (vehicle nodes link only to vehicle nodes):
		// 15:3 and 15:4 in the game, 15:4 and 15:5 in the test game's synthetic files.
		SAPaths::Node *a = nil, *b = nil;
		for(int point = 3; a == nil && point < 40; point++){
			SAPaths::Node *n = SAPaths::FindNode(15, point), *next = SAPaths::FindNode(15, point + 1);
			if(n && next && SAPaths::IsVehicleNode(n) == SAPaths::IsVehicleNode(next)){
				a = n;
				b = next;
			}
		}
		SAPaths::Node *added = nil;
		undoTest("nodes: add", [&]{
			const char *reason = nil;
			added = SAPaths::AddNode({ 1700.0f, -1700.0f, 13.5f }, false, nil, &reason);
			return added != nil;
		});
		undoTest("nodes: link", [&]{
			if(a == nil || b == nil) return false;
			const char *reason = nil;
			return SAPaths::AreNodesLinked(a, b) ? SAPaths::UnlinkNodes(a, b) : SAPaths::LinkNodes(a, b, &reason);
		});
		undoTest("nodes: move", [&]{
			SAPaths::selectedNode = SAPaths::FindNode(15, 3);
			rw::V3d pos;
			if(!SAPaths::GetSelectedNodePosition(&pos)) return false;
			pos.x += 0.5f;
			bool moved = SAPaths::SetSelectedNodePosition(pos, false);
			SAPaths::CommitSelectedNodeEdit();
			return moved;
		});
		undoTest("nodes: delete", [&]{
			SAPaths::Node *node = SAPaths::FindNode(15, 5);
			return node && SAPaths::DeleteNode(node);
		});
	}else
		fprintf(gReport, "SKIP nodes: the game's node files aren't loaded\n");

	// Object snap with several objects moving: B, not the leader A, is 0.2 m
	// short of T, so the selection snaps by B (only the leader's box was tried).
	std::vector<ObjectInst*> snappable;
	for(CPtrNode *p = instances.first; p && snappable.size() < 3; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		ObjectDef *def = GetObjectDef(inst->m_objectId);
		// Not editor icons (labels, sounds...): those are never snap targets.
		if(!inst->m_isDeleted && PawnObjectNeedsExport(inst) && def && def->m_colModel &&
		   GetObjectPawnProperties(inst)->attachParent < 0 && !InstUsesEditorIcon(inst))
			snappable.push_back(inst);
	}
	if(snappable.size() == 3){
		ObjectInst *a = snappable[0], *b = snappable[1], *t = snappable[2];
		auto boxOf = [](ObjectInst *inst){ return GetObjectDef(inst->m_objectId)->m_colModel->boundingBox; };
		auto put = [](ObjectInst *inst, rw::V3d pos){
			inst->m_translation = pos;
			inst->m_rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
			inst->m_area = 0;	// the same interior, or they aren't snap targets
			inst->UpdateMatrix();
			RemoveInstFromSectors(inst);
			InsertInstIntoSectors(inst);
		};
		rw::V3d tPos = { 2900.0f, 2900.0f, 50.0f };
		put(t, tPos);
		put(b, { tPos.x + boxOf(t).min.x - boxOf(b).max.x - 0.2f, tPos.y, tPos.z });
		put(a, { tPos.x - 40.0f, tPos.y, tPos.z });
		selectOnly({ a, b });
		float distance = gObjectSnapDistance;
		gObjectSnapDistance = 0.5f;
		rw::V3d none = { 0.0f, 0.0f, 0.0f }, d = none;
		bool leaderOnly = FindSelectionSnapDelta({ a }, { a->m_translation }, none, &d);
		bool both = FindSelectionSnapDelta({ a, b }, { a->m_translation, b->m_translation }, none, &d);
		gObjectSnapDistance = distance;
		char why[128];
		snprintf(why, sizeof(why), "leader alone %s, both %s with dx %.3f",
			leaderOnly ? "snapped" : "didn't", both ? "snapped" : "didn't", d.x);
		report(!leaderOnly && both && fabsf(d.x - 0.2f) < 0.01f, "snap: a selection snaps by any member", why);
		ClearSelection();
	}else
		fprintf(gReport, "SKIP snap: fewer than three objects with collision\n");

	fprintf(gReport, "undo self-test: %d passed, %d failed\n", gPassed, gFailed);
	fflush(gReport);
	return gFailed;
}

int
RunProfile(FILE *out, double openSeconds)
{
	fprintf(out, "open %.3f s\n", openSeconds);
	int objects = 0;
	for(CPtrNode *p = instances.first; p; p = p->next)
		if(PawnObjectNeedsExport((ObjectInst*)p->item))
			objects++;
	fprintf(out, "elements %d\n", objects);
	std::string code;
	fprintf(out, "save (build Pawn) %.3f s\n", seconds([&]{
		BuildPawnCode(GetPawnMapPath(), (PawnCodeFormat)GetPawnMapFormat(), false, code);
	}));
	MapCheckResult check;
	double checkSeconds = seconds([&]{ RunMapCheck(GetPawnMapFormat(), &check); });
	fprintf(out, "map check %.3f s (%d items)\n", checkSeconds, (int)check.items.size());
	fprintf(out, "select all map objects %.3f s\n", seconds([&]{ SelectAllPawnObjects(); }));
	fprintf(out, "clear selection %.3f s\n", seconds([&]{ ClearSelection(); }));
	bool wasOn = gPlayerView;
	gPlayerView = true;
	fprintf(out, "player view update %.4f s\n", seconds([&]{ PlayerViewUpdate(); }));
	gPlayerView = wasOn;
	PlayerViewUpdate();
	fprintf(out, "count hidden %.4f s\n", seconds([&]{ CountHiddenInstances(); }));
	fflush(out);
	return 0;
}
