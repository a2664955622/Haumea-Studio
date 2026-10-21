#include "haumea.h"
#include "rec.h"
#include "mapcheck.h"
#include "workspace.h"
#include "textdraws.h"
#include "encoding.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#else
#define strcasecmp _stricmp
#endif
#include <sys/stat.h>
#include <map>
#include <regex>
#include <set>

namespace {

// open.mp SDK values.hpp; SA-MP 0.3.7 has the same numbers.
const int OBJECTS_037 = 1000;	// OBJECT_POOL_SIZE_037, and SA-MP's MAX_OBJECTS
const int OBJECTS_OMP = 2000;	// OBJECT_POOL_SIZE, with artwork on and no 0.3.7 clients
const int VEHICLES = 2000;
const int PICKUPS = 4096;
const int LABELS = 1024;
const int ACTORS = 1000;
const int GANG_ZONES = 1024;
const int CLASSES = 320;
const int MAP_ICONS = 100;	// SetPlayerMapIcon iconid 0-99
const int CLIENT_OBJECTS = 1000;	// objects a 0.3.7 client creates at once
const float STREAMER_OBJECT_SD = 300.0f;
const int MIN_NAME = 3, MAX_NAME = 24;
const int GLOBAL_TEXTDRAWS = 2048, PLAYER_TEXTDRAWS = 256;	// GLOBAL_/PLAYER_TEXTDRAW_POOL_SIZE
const int MAX_TEXTDRAW_STR_LENGTH = 800;
const float TEXTDRAW_CANVAS_W = 640.0f, TEXTDRAW_CANVAS_H = 448.0f;

struct ServerConfig
{
	bool found;
	bool artwork;
	bool allow037;
	int maxPlayers;
};

bool
fileExists(const std::string &path)
{
	struct stat st;
	if(stat(path.c_str(), &st) != 0)
		return false;
#ifdef _WIN32
	return (st.st_mode & _S_IFMT) == _S_IFREG;
#else
	return S_ISREG(st.st_mode);
#endif
}

std::string
readFile(const std::string &path)
{
	std::string text;
	FILE *f = fopen(path.c_str(), "rb");
	if(f == nil)
		return text;
	char buf[4096];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		text.append(buf, n);
	fclose(f);
	return text;
}

// The few config.json values that change limits; open.mp's defaults otherwise.
ServerConfig
readServerConfig(void)
{
	ServerConfig config = { false, false, true, 50 };
	if(GetWorkspaceDir()[0] == '\0')
		return config;
	std::string text = readFile(WorkspacePath("config.json"));
	if(text.empty())
		return config;
	config.found = true;
	std::smatch m;
	if(std::regex_search(text, m, std::regex(R"re("max_players"\s*:\s*(\d+))re")))
		config.maxPlayers = atoi(m[1].str().c_str());
	if(std::regex_search(text, m, std::regex(R"re("artwork"\s*:\s*\{[^}]*"enable"\s*:\s*(true|false))re")))
		config.artwork = m[1] == "true";
	if(std::regex_search(text, m, std::regex(R"re("allow_037_clients"\s*:\s*(true|false))re")))
		config.allow037 = m[1] == "true";
	return config;
}

bool
validName(const char *name)
{
	int len = (int)strlen(name);
	if(len < MIN_NAME || len > MAX_NAME)
		return false;
	// open.mp's player pool: letters, digits and ][_$=()
	for(const char *p = name; *p; p++)
		if(!isalnum((unsigned char)*p) && !strchr("][_$=()", *p))
			return false;
	return true;
}

// {RRGGBB} embeds: a '{' that does not start one shows as text in game.
bool
badColourCode(const char *text)
{
	for(const char *p = text; *p; p++){
		if(*p != '{')
			continue;
		bool ok = strlen(p) >= 8 && p[7] == '}';
		for(int i = 1; ok && i <= 6; i++)
			ok = isxdigit((unsigned char)p[i]) != 0;
		if(!ok)
			return true;
		p += 7;
	}
	return false;
}

rw::Geometry*
instanceGeometry(ObjectInst *inst)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj == nil)
		return nil;
	if(obj->m_type == ObjectDef::ATOMIC){
		if(inst->m_rwObject)
			return ((rw::Atomic*)inst->m_rwObject)->geometry;
		return obj->m_atomics[0] ? obj->m_atomics[0]->geometry : nil;
	}
	rw::Clump *clump = inst->m_rwObject ? (rw::Clump*)inst->m_rwObject : obj->m_clump;
	if(clump)
		FORLIST(lnk, clump->atomics)
			return rw::Atomic::fromClump(lnk)->geometry;
	return nil;
}

struct Checker
{
	MapCheckResult *result;

	void add(int severity, const char *category, std::vector<int> insts, const char *fmt, ...)
	{
		char text[512];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(text, sizeof(text), fmt, ap);
		va_end(ap);
		MapCheckItem item;
		item.severity = severity;
		item.category = category;
		item.text = text;
		item.instances.swap(insts);
		result->items.push_back(item);
	}
	void add(int severity, const char *category, ObjectInst *inst, const char *fmt, ...)
	{
		char text[512];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(text, sizeof(text), fmt, ap);
		va_end(ap);
		std::vector<int> insts;
		if(inst)
			insts.push_back(inst->m_id);
		add(severity, category, insts, "%s", text);
	}
};

std::vector<int>
ids(const std::vector<ObjectInst*> &insts)
{
	std::vector<int> out;
	for(ObjectInst *inst : insts)
		out.push_back(inst->m_id);
	return out;
}

const char*
label(const ObjectInst *inst, char *buf, size_t size)
{
	const ObjectPawnProperties *p = GetObjectPawnProperties(inst);
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	int kind = GetSampElementKind(inst);
	const char *what = kind != SAMP_ELEMENT_OBJECT ? GetSampElementKindName(kind) :
		obj ? obj->m_name : "object";
	if(p->name[0])
		snprintf(buf, size, "%s (%s)", p->name, what);
	else
		snprintf(buf, size, "%s #%d", what, inst->m_id);
	return buf;
}

}	// namespace

const char*
MapCheckSeverityName(int severity)
{
	return severity == MAPCHECK_ERROR ? "Problem" : severity == MAPCHECK_WARNING ? "Warning" : "Note";
}

void
RunMapCheck(int pawnFormat, MapCheckResult *result)
{
	result->items.clear();
	result->pawnFormat = pawnFormat;
	Checker c = { result };
	bool streamer = pawnFormat == PAWN_CODE_STREAMER;
	ServerConfig config = readServerConfig();
	char name[160];

	std::vector<ObjectInst*> objects, vehicles, byKind[NUM_SAMP_ELEMENT_KINDS], customModels;
	std::map<std::string, std::vector<ObjectInst*>> variables;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted || !PawnObjectNeedsExport(inst))
			continue;
		int kind = GetSampElementKind(inst);
		if(inst->m_isVehicle)
			vehicles.push_back(inst);
		else if(kind == SAMP_ELEMENT_OBJECT)
			objects.push_back(inst);
		byKind[kind].push_back(inst);
		if(IsCustomModel(inst->m_objectId))
			customModels.push_back(inst);
		const ObjectPawnProperties *props = GetObjectPawnProperties(inst);
		if(props->name[0])
			variables[props->name].push_back(inst);
	}

	// --- Limits ---
	const char *LIMITS = "Limits";
	int objectLimit = OBJECTS_037;
	const char *objectWhy = "SA-MP 0.3.7 and open.mp with 0.3.7 clients allowed";
	if(config.found && config.artwork && !config.allow037){
		objectLimit = OBJECTS_OMP;
		objectWhy = "open.mp with artwork on and no 0.3.7 clients (config.json)";
	}
	if(!streamer){
		if((int)objects.size() > objectLimit)
			c.add(MAPCHECK_ERROR, LIMITS, ids(objects), "%d objects, but CreateObject stops at %d (%s). "
				"Objects past the limit are not created; save with Streamer code instead.",
				(int)objects.size(), objectLimit, objectWhy);
		else if((int)objects.size() > objectLimit * 9 / 10)
			c.add(MAPCHECK_WARNING, LIMITS, std::vector<int>(), "%d of %d objects (%s): the rest of the gamemode "
				"has little room left.", (int)objects.size(), objectLimit, objectWhy);
	}else if(objects.size() > CLIENT_OBJECTS){
		// The streamer creates what is near a player; find the busiest spot.
		// Objects in 300 m cells: a spot's neighbours are in the 3x3 cells around it.
		int worst = 0;
		ObjectInst *worstAt = nil;
		auto cellOf = [](float v){ return (int)floorf(v / STREAMER_OBJECT_SD); };
		std::map<std::pair<int, int>, std::vector<ObjectInst*>> cells;
		for(ObjectInst *inst : objects)
			cells[{ cellOf(inst->m_translation.x), cellOf(inst->m_translation.y) }].push_back(inst);
		size_t step = objects.size() > 4000 ? objects.size() / 4000 + 1 : 1;
		for(size_t i = 0; i < objects.size(); i += step){
			int n = 0;
			rw::V3d at = objects[i]->m_translation;
			int cx = cellOf(at.x), cy = cellOf(at.y);
			for(int dx = -1; dx <= 1; dx++)
			for(int dy = -1; dy <= 1; dy++){
				auto cell = cells.find({ cx + dx, cy + dy });
				if(cell == cells.end())
					continue;
				for(ObjectInst *other : cell->second){
					rw::V3d d = sub(other->m_translation, at);
					if(d.x*d.x + d.y*d.y + d.z*d.z < STREAMER_OBJECT_SD*STREAMER_OBJECT_SD)
						n++;
				}
			}
			if(n > worst){
				worst = n;
				worstAt = objects[i];
			}
		}
		if(worst > CLIENT_OBJECTS)
			c.add(MAPCHECK_WARNING, LIMITS, worstAt, "%d objects within %.0f m of one spot: a 0.3.7 client "
				"shows at most %d at once, so the streamer will leave some out there.",
				worst, STREAMER_OBJECT_SD, CLIENT_OBJECTS);
	}
	struct { int kind; int limit; bool streamerFree; const char *what; } pools[] = {
		{ SAMP_ELEMENT_PICKUP, PICKUPS, true, "pickups" },
		{ SAMP_ELEMENT_LABEL, LABELS, true, "3D text labels" },
		{ SAMP_ELEMENT_ACTOR, ACTORS, true, "actors" },
		{ SAMP_ELEMENT_GANGZONE, GANG_ZONES, false, "gang zones" },
		{ SAMP_ELEMENT_CLASS, CLASSES, false, "player classes" },
		{ SAMP_ELEMENT_MAPICON, MAP_ICONS, true, "map icons" },
	};
	for(auto &pool : pools){
		int n = (int)byKind[pool.kind].size();
		if(n > pool.limit && !(streamer && pool.streamerFree))
			c.add(MAPCHECK_ERROR, LIMITS, ids(byKind[pool.kind]), "%d %s, over the server's limit of %d; "
				"the extra ones are not created.", n, pool.what, pool.limit);
	}
	if((int)vehicles.size() > VEHICLES)
		c.add(MAPCHECK_ERROR, LIMITS, ids(vehicles), "%d vehicles, over the limit of %d.", (int)vehicles.size(), VEHICLES);
	int npcs = (int)byKind[SAMP_ELEMENT_NPC].size();
	if(npcs > 0){
		if(config.found && npcs > config.maxPlayers)
			c.add(MAPCHECK_ERROR, LIMITS, ids(byKind[SAMP_ELEMENT_NPC]), "%d NPCs, but config.json allows %d players "
				"and every NPC takes a player slot.", npcs, config.maxPlayers);
		else
			c.add(MAPCHECK_INFO, LIMITS, std::vector<int>(), "%d NPC(s) will take %d player slot(s)%s.", npcs, npcs,
				config.found ? "" : " (no config.json in the workspace to check max_players against)");
	}

	// --- Models ---
	const char *MODELS = "Models";
	const PawnImportReport &report = GetLastPawnImportReport();
	if(report.unknownModelObjects > 0){
		std::string list, custom;
		for(int id : report.unknownModels){
			// AddSimpleModel IDs live at CUSTOM_MODEL_BASE + 1000..30000 in the editor.
			bool isCustom = id >= CUSTOM_MODEL_BASE + 1000 && id <= CUSTOM_MODEL_BASE + 30000;
			std::string &to = isCustom ? custom : list;
			if(to.size() < 200)
				to += (to.empty() ? "" : ", ") + std::to_string(isCustom ? PawnModelId(id) : id);
		}
		c.add(MAPCHECK_ERROR, MODELS, std::vector<int>(), "%d object(s) with models the editor doesn't know were left "
			"out when the map was opened, and saving drops them.%s%s%s%s", report.unknownModelObjects,
			list.empty() ? "" : " IDs: ", list.c_str(),
			custom.empty() ? "" : list.empty() ? " Custom IDs not in artconfig.txt or AddSimpleModel: " :
				". Custom IDs not in artconfig.txt or AddSimpleModel: ", custom.c_str());
		// What to do about it.
		c.add(MAPCHECK_INFO, MODELS, std::vector<int>(), "To keep them: a SA-MP 0.3.DL or open.mp custom model "
			"goes in the workspace's models/artconfig.txt (AddSimpleModel) with its DFF and TXD; anything "
			"else needs a similar game or SA-MP object in its place (search the Object Browser).");
	}
	for(ObjectInst *inst : objects){
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		if(obj == nil || obj->m_cantLoad)
			c.add(MAPCHECK_ERROR, MODELS, inst, "%s: its model can't be loaded.", label(inst, name, sizeof(name)));
	}

	// --- Positions ---
	const char *PLACES = "Positions";
	std::vector<ObjectInst*> outside, offMap;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted || !PawnObjectNeedsExport(inst))
			continue;
		float x = fabsf(inst->m_translation.x), y = fabsf(inst->m_translation.y), z = fabsf(inst->m_translation.z);
		if(x > SA_MP_WORLD_BOUND || y > SA_MP_WORLD_BOUND || z > SA_MP_WORLD_BOUND)
			outside.push_back(inst);
		else if(x > SA_GAME_WATER_BOUND || y > SA_GAME_WATER_BOUND)
			offMap.push_back(inst);
	}
	if(!outside.empty())
		c.add(MAPCHECK_ERROR, PLACES, ids(outside), "%d element(s) outside SA-MP's world bounds (\xC2\xB1%d): "
			"the server clamps or ignores those positions.", (int)outside.size(), SA_MP_WORLD_BOUND);
	if(!offMap.empty())
		c.add(MAPCHECK_INFO, PLACES, ids(offMap), "%d element(s) beyond the San Andreas map (\xC2\xB1%d). "
			"They work, but the game's water, map and radar end there.", (int)offMap.size(), SA_GAME_WATER_BOUND);

	// --- Materials ---
	const char *MATERIALS = "Materials";
	for(ObjectInst *inst : objects){
		if(!HasObjectMaterialOverrides(inst))
			continue;
		rw::Geometry *geometry = instanceGeometry(inst);
		int count = geometry ? geometry->matList.numMaterials : -1;
		for(int slot = 0; slot < MAX_PAWN_MATERIALS; slot++){
			const ObjectMaterialOverride *m = GetObjectMaterialOverride(inst, slot);
			if(m == nil || (!m->active && !m->textEnabled))
				continue;
			if(count >= 0 && slot >= count)
				c.add(MAPCHECK_WARNING, MATERIALS, inst, "%s: material slot %d is set, but the model has %d slot(s), "
					"so it does nothing.", label(inst, name, sizeof(name)), slot, count);
			if(m->active && !m->textEnabled){
				// The TXD and texture as the client looks them up (game, SA-MP
				// or the workspace's custom models).
				char why[512];
				int check = CheckMaterialTexture(*m, why, sizeof(why));
				if(check != MATERIAL_TEXTURE_OK)
					c.add(check == MATERIAL_TEXTURE_ERROR ? MAPCHECK_ERROR :
						check == MATERIAL_TEXTURE_NOTE ? MAPCHECK_INFO : MAPCHECK_WARNING, MATERIALS, inst,
						"%s: material slot %d: %s", label(inst, name, sizeof(name)), slot, why);
			}
			if(m->textEnabled){
				if(badColourCode(m->text))
					c.add(MAPCHECK_WARNING, MATERIALS, inst, "%s: material text in slot %d has a '{' that isn't a "
						"{RRGGBB} colour code; it shows as text.", label(inst, name, sizeof(name)), slot);
				if((int)strlen(m->text) >= PAWN_MATERIAL_TEXT_LEN - 1)
					c.add(MAPCHECK_WARNING, MATERIALS, inst, "%s: material text in slot %d is at the %d-character "
						"limit and may have been cut.", label(inst, name, sizeof(name)), slot, PAWN_MATERIAL_TEXT_LEN - 1);
			}
		}
	}

	// --- Elements ---
	const char *ELEMENTS = "Elements";
	for(ObjectInst *inst : byKind[SAMP_ELEMENT_LABEL]){
		const ObjectPawnProperties *p = GetObjectPawnProperties(inst);
		if(p->labelText[0] == '\0')
			c.add(MAPCHECK_WARNING, ELEMENTS, inst, "%s has no text.", label(inst, name, sizeof(name)));
		if(badColourCode(p->labelText))
			c.add(MAPCHECK_WARNING, ELEMENTS, inst, "%s: a '{' that isn't a {RRGGBB} colour code shows as text.",
				label(inst, name, sizeof(name)));
		if((int)strlen(p->labelText) >= PAWN_LABEL_TEXT_LEN - 1)
			c.add(MAPCHECK_WARNING, ELEMENTS, inst, "%s is at the %d-character limit and may have been cut.",
				label(inst, name, sizeof(name)), PAWN_LABEL_TEXT_LEN - 1);
	}
	for(ObjectInst *inst : byKind[SAMP_ELEMENT_SOUND]){
		const ObjectPawnProperties *p = GetObjectPawnProperties(inst);
		if(p->soundKind == SAMP_SOUND_STREAM &&
		   strncmp(p->soundUrl, "http://", 7) != 0 && strncmp(p->soundUrl, "https://", 8) != 0)
			c.add(MAPCHECK_ERROR, ELEMENTS, inst, "%s: PlayAudioStreamForPlayer needs an http:// or https:// URL%s.",
				label(inst, name, sizeof(name)), p->soundUrl[0] ? "" : ", and it has none");
	}
	for(auto &v : variables)
		if(v.second.size() > 1)
			c.add(MAPCHECK_ERROR, ELEMENTS, ids(v.second), "%d elements share the variable name \"%s\": "
				"the Pawn code won't compile.", (int)v.second.size(), v.first.c_str());
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted || inst->m_pawnProperties == nil || inst->m_pawnProperties->attachParent < 0)
			continue;
		ObjectInst *parent = GetInstanceByID(inst->m_pawnProperties->attachParent);
		if(parent == nil || parent->m_isDeleted)
			c.add(MAPCHECK_ERROR, ELEMENTS, inst, "%s is attached to an object that was deleted.",
				label(inst, name, sizeof(name)));
	}

	// --- NPCs (open.mp) ---
	const char *NPCS = "NPCs";
	std::set<std::string> usedPaths;
	std::map<std::string, std::vector<ObjectInst*>> npcNames;
	std::map<std::pair<int32, int>, std::vector<ObjectInst*>> seats;	// (vehicle, seat)
	for(ObjectInst *inst : byKind[SAMP_ELEMENT_NPC]){
		const ObjectPawnProperties *p = GetObjectPawnProperties(inst);
		npcNames[p->npcName].push_back(inst);
		ObjectInst *vehicle = GetNpcVehicle(inst);
		if(p->npcVehicle >= 0 && vehicle == nil)
			c.add(MAPCHECK_ERROR, NPCS, inst, "%s sits in a vehicle that was deleted, so it is saved on foot.",
				label(inst, name, sizeof(name)));
		if(vehicle)
			seats[std::make_pair(vehicle->m_id, p->npcSeat)].push_back(inst);
		if(p->npcRecording[0]){
			int type = RecTypeOfName(p->npcRecording);
			if(type == REC_DRIVER && !(vehicle && p->npcSeat == 0))
				c.add(MAPCHECK_ERROR, NPCS, inst, "%s plays driver recording %s but isn't driving a vehicle: "
					"open.mp reports it playing, yet the NPC never moves. Put it in a vehicle as the driver.",
					label(inst, name, sizeof(name)), p->npcRecording);
			else if(type == REC_ONFOOT && vehicle)
				c.add(MAPCHECK_WARNING, NPCS, inst, "%s plays on-foot recording %s while in a vehicle; "
					"it needs to be on foot.", label(inst, name, sizeof(name)), p->npcRecording);
		}
		if(!validName(p->npcName))
			c.add(MAPCHECK_ERROR, NPCS, inst, "%s: \"%s\" isn't a valid player name (%d-%d letters, digits or ][_$=()), "
				"so NPC_Create fails.", label(inst, name, sizeof(name)), p->npcName, MIN_NAME, MAX_NAME);
		if(p->npcRecording[0]){
			std::string file = p->npcRecording;
			if(file.size() < 4 || strcasecmp(file.c_str() + file.size() - 4, ".rec") != 0)
				file += ".rec";
			if(GetWorkspaceDir()[0] && !fileExists(WorkspacePath(("npcmodes/recordings/" + file).c_str())))
				c.add(MAPCHECK_ERROR, NPCS, inst, "%s: recording %s isn't in the workspace's npcmodes/recordings.",
					label(inst, name, sizeof(name)), file.c_str());
		}else if(p->npcPlaysNode){
			char node[64];
			snprintf(node, sizeof(node), "scriptfiles/NPCs/nodes/NODES%d.DAT", p->npcNode);
			if(GetWorkspaceDir()[0] && !fileExists(WorkspacePath(node)))
				c.add(MAPCHECK_INFO, NPCS, inst, "%s walks node file %d, which isn't in the workspace (%s). "
					"open.mp downloads it on first use; offline servers need it copied there.",
					label(inst, name, sizeof(name)), p->npcNode, node);
		}else if(p->pathName[0]){
			usedPaths.insert(p->pathName);
			std::vector<ObjectInst*> points;
			GetNpcPathPoints(p->pathName, points);
			if(points.empty())
				c.add(MAPCHECK_ERROR, NPCS, inst, "%s follows path \"%s\", which has no points.",
					label(inst, name, sizeof(name)), p->pathName);
			else if(points.size() == 1)
				c.add(MAPCHECK_WARNING, NPCS, inst, "%s follows path \"%s\", which has one point: it walks there "
					"and stops.", label(inst, name, sizeof(name)), p->pathName);
		}
	}
	for(auto &seat : seats)
		if(seat.second.size() > 1)
			c.add(MAPCHECK_ERROR, NPCS, ids(seat.second), "%d NPCs sit in the same seat (%d) of one vehicle: "
				"they can't both sit there.", (int)seat.second.size(), seat.first.second);
	for(auto &n : npcNames)
		if(n.second.size() > 1 && n.first[0])
			c.add(MAPCHECK_ERROR, NPCS, ids(n.second), "%d NPCs are named \"%s\": names must be unique, so only "
				"one is created.", (int)n.second.size(), n.first.c_str());
	std::map<std::string, std::vector<ObjectInst*>> unusedPaths;
	for(ObjectInst *inst : byKind[SAMP_ELEMENT_PATHPOINT]){
		const ObjectPawnProperties *p = GetObjectPawnProperties(inst);
		if(usedPaths.find(p->pathName) == usedPaths.end())
			unusedPaths[p->pathName].push_back(inst);
	}
	for(auto &path : unusedPaths)
		c.add(MAPCHECK_INFO, NPCS, ids(path.second), "Path \"%s\" (%d point(s)) isn't followed by any NPC.",
			path.first.empty() ? "(no name)" : path.first.c_str(), (int)path.second.size());
	// open.mp walks a path once; repeating it needs the generated helper hooked up.
	std::vector<ObjectInst*> repeating;
	for(ObjectInst *inst : byKind[SAMP_ELEMENT_NPC])
		if(GetObjectPawnProperties(inst)->npcPathRepeat != NPC_PATH_ONCE)
			repeating.push_back(inst);
	if(!repeating.empty())
		c.add(MAPCHECK_INFO, NPCS, ids(repeating), "%d NPC(s) loop or walk back and forth along their path: "
			"call the map's <prefix>_OnNPCFinishMovePath(npcid, pathid) from your gamemode's "
			"OnNPCFinishMovePath, or they stop at the path's end.", (int)repeating.size());

	// --- Checkpoints (streamer only) ---
	const char *CHECKPOINTS = "Checkpoints";
	std::vector<ObjectInst*> plainCps, raceCps;
	std::map<std::string, std::vector<ObjectInst*>> races;
	for(ObjectInst *inst : byKind[SAMP_ELEMENT_CHECKPOINT]){
		const ObjectPawnProperties *p = GetObjectPawnProperties(inst);
		if(p->cpKind == SAMP_CHECKPOINT_RACE){
			raceCps.push_back(inst);
			races[p->cpRace].push_back(inst);
		}else
			plainCps.push_back(inst);
	}
	if(!streamer && !byKind[SAMP_ELEMENT_CHECKPOINT].empty())
		c.add(MAPCHECK_WARNING, CHECKPOINTS, ids(byKind[SAMP_ELEMENT_CHECKPOINT]), "%d checkpoint(s) are not written "
			"in native code (a player has one checkpoint at a time, set by the script). Save as Streamer code.",
			(int)byKind[SAMP_ELEMENT_CHECKPOINT].size());
	else if(plainCps.size() + raceCps.size() > 1)
		c.add(MAPCHECK_INFO, CHECKPOINTS, std::vector<int>(), "%d checkpoint(s) and %d race checkpoint(s): a player "
			"sees one of each at a time, so the streamer shows the nearest.", (int)plainCps.size(), (int)raceCps.size());
	for(auto &race : races){
		const char *raceName = race.first.empty() ? "(no name)" : race.first.c_str();
		if(race.second.size() == 1)
			c.add(MAPCHECK_WARNING, CHECKPOINTS, ids(race.second), "Race \"%s\" has one checkpoint: there is "
				"nothing to race to.", raceName);
		std::map<int, std::vector<ObjectInst*>> orders;
		for(ObjectInst *inst : race.second)
			orders[GetObjectPawnProperties(inst)->cpOrder].push_back(inst);
		for(auto &order : orders)
			if(order.second.size() > 1)
				c.add(MAPCHECK_ERROR, CHECKPOINTS, ids(order.second), "Race \"%s\": %d checkpoints share order %d, "
					"so which one comes first is undefined.", raceName, (int)order.second.size(), order.first);
	}

	// --- TextDraws ---
	const char *TEXTDRAWS = "TextDraws";
	const std::vector<TextDraw> &textDraws = GetTextDraws();
	int globalTds = 0, playerTds = 0;
	for(size_t i = 0; i < textDraws.size(); i++){
		const TextDraw &td = textDraws[i];
		(td.perPlayer ? playerTds : globalTds)++;
		const char *tdName = td.name[0] ? td.name : "(unnamed)";
		MapCheckItem item;
		item.category = TEXTDRAWS;
		item.textDraw = (int)i;
		char text[512];
		auto addTd = [&](int severity, const char *message){
			snprintf(text, sizeof(text), "TextDraw %s: %s", tdName, message);
			item.severity = severity;
			item.text = text;
			result->items.push_back(item);
		};
		if(td.x < 0.0f || td.x > TEXTDRAW_CANVAS_W || td.y < 0.0f || td.y > TEXTDRAW_CANVAS_H)
			addTd(MAPCHECK_WARNING, "its position is off the 640x448 canvas, so players won't see it.");
		if(td.font == TD_FONT_SPRITE){
			if(td.text.empty() || td.text.find(':') == std::string::npos)
				addTd(MAPCHECK_ERROR, "a sprite needs its text as TXD:texture.");
		}else if(td.font != TD_FONT_PREVIEW){
			if(td.text.empty())
				addTd(MAPCHECK_WARNING, "empty text can crash 0.3.7 clients; use \"_\" for nothing.");
			else if(badColourCode(td.text.c_str()))
				addTd(MAPCHECK_INFO, "has a '{' that isn't a {RRGGBB} colour code.");
		}
		if((int)td.text.size() > MAX_TEXTDRAW_STR_LENGTH)
			addTd(MAPCHECK_ERROR, "its text is longer than the 800-character limit.");
	}
	// An old map's own textdraws are created in game too, beside the file's.
	const std::vector<TextDraw> &mapTextDraws = GetMapTextDraws();
	for(size_t i = 0; i < mapTextDraws.size(); i++)
		(mapTextDraws[i].perPlayer ? playerTds : globalTds)++;
	if(!mapTextDraws.empty())
		c.add(MAPCHECK_INFO, TEXTDRAWS, std::vector<int>(), "%d textdraw(s) are still in the map, read-only: "
			"TextDraws > Move to a separate file to edit them.", (int)mapTextDraws.size());
	if(globalTds > GLOBAL_TEXTDRAWS)
		c.add(MAPCHECK_ERROR, TEXTDRAWS, std::vector<int>(), "%d global textdraws, over the limit of %d.",
			globalTds, GLOBAL_TEXTDRAWS);
	if(playerTds > PLAYER_TEXTDRAWS)
		c.add(MAPCHECK_ERROR, TEXTDRAWS, std::vector<int>(), "%d player textdraws, over the limit of %d per player.",
			playerTds, PLAYER_TEXTDRAWS);

	// --- Text encoding ---
	// SA-MP and open.mp clients draw a string's bytes as Windows-1252.
	const char *TEXT = "Text";
	bool cp1252 = GetPawnTextEncoding() == PAWN_TEXT_CP1252;
	std::vector<int> utf8Text;
	auto checkText = [&](ObjectInst *inst, int textDraw, const char *what, const char *text, bool shown){
		if(text == nil || IsAscii(text))
			return;
		if(!cp1252){
			if(shown && inst)
				utf8Text.push_back(inst->m_id);
			else if(shown)
				utf8Text.push_back(-1);
			return;
		}
		std::string missing = Cp1252Unencodable(text);
		if(missing.empty())
			return;
		char message[512];
		snprintf(message, sizeof(message), "%s%s has %s, which Windows-1252 (what SA-MP and open.mp clients "
			"draw) doesn't have: it is saved as '?'.", inst ? label(inst, name, sizeof(name)) : "", what,
			missing.c_str());
		if(inst){
			c.add(shown ? MAPCHECK_WARNING : MAPCHECK_INFO, TEXT, inst, "%s", message);
		}else{
			MapCheckItem item;
			item.category = TEXT;
			item.textDraw = textDraw;
			item.severity = MAPCHECK_WARNING;
			item.text = message;
			result->items.push_back(item);
		}
	};
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted || !PawnObjectNeedsExport(inst))
			continue;
		const ObjectPawnProperties *props = GetObjectPawnProperties(inst);
		if(props->elementKind == SAMP_ELEMENT_LABEL)
			checkText(inst, -1, "'s text", props->labelText, true);
		for(int slot = 0; slot < MAX_PAWN_MATERIALS; slot++){
			const ObjectMaterialOverride *m = GetObjectMaterialOverride(inst, slot);
			if(m && m->textEnabled)
				checkText(inst, -1, "'s material text", m->text, true);
		}
		checkText(inst, -1, "'s group", props->group, false);
		checkText(inst, -1, "'s note", props->comment, false);
	}
	for(size_t i = 0; i < textDraws.size(); i++){
		std::string what = std::string("TextDraw ") + (textDraws[i].name[0] ? textDraws[i].name : "(unnamed)");
		checkText(nil, (int)i, what.c_str(), textDraws[i].text.c_str(), true);
	}
	if(!utf8Text.empty()){
		std::vector<int> insts;
		for(int id : utf8Text)
			if(id >= 0)
				insts.push_back(id);
		c.add(MAPCHECK_WARNING, TEXT, insts, "%d text(s) with accents or symbols are saved as UTF-8, but SA-MP "
			"and open.mp clients draw Windows-1252, so they show garbled (\"Ã§\" for \"ç\"). Save As > "
			"Windows-1252 fixes it.", (int)utf8Text.size());
	}

	// --- Server version ---
	const char *SERVER = "Server";
	int ompOnly = (int)(byKind[SAMP_ELEMENT_NPC].size() + byKind[SAMP_ELEMENT_PATHPOINT].size());
	if(ompOnly > 0)
		c.add(MAPCHECK_WARNING, SERVER, ids(byKind[SAMP_ELEMENT_NPC]), "The map has NPCs or NPC paths: their NPC_* "
			"calls only compile and run on open.mp (with its NPCs component), not on a SA-MP 0.3.7 server.");
	if(!customModels.empty()){
		bool ready = config.found && config.artwork;
		c.add(ready ? MAPCHECK_INFO : MAPCHECK_WARNING, SERVER, ids(customModels), "%d element(s) use custom models: "
			"the server needs them in models/artconfig.txt (or AddSimpleModel) and artwork enabled%s; players "
			"need SA-MP 0.3.DL or an open.mp client.%s", (int)customModels.size(),
			config.found ? (config.artwork ? "" : ", but config.json has artwork.enable off") : "",
			config.found && config.allow037 ? " 0.3.7 clients are allowed and won't see them." : "");
	}

	result->numErrors = result->numWarnings = 0;
	for(const MapCheckItem &item : result->items){
		if(item.severity == MAPCHECK_ERROR) result->numErrors++;
		else if(item.severity == MAPCHECK_WARNING) result->numWarnings++;
	}
}

std::string
MapCheckSummary(const MapCheckResult &result)
{
	char buf[96];
	if(result.numErrors && result.numWarnings)
		snprintf(buf, sizeof(buf), "%d problem%s and %d warning%s", result.numErrors, result.numErrors == 1 ? "" : "s",
			result.numWarnings, result.numWarnings == 1 ? "" : "s");
	else if(result.numErrors)
		snprintf(buf, sizeof(buf), "%d problem%s", result.numErrors, result.numErrors == 1 ? "" : "s");
	else if(result.numWarnings)
		snprintf(buf, sizeof(buf), "%d warning%s", result.numWarnings, result.numWarnings == 1 ? "" : "s");
	else
		buf[0] = '\0';
	return buf;
}
