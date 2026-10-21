#include "haumea.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

// Maps from other tools, opened like Texture Studio's .db: each is turned
// into the Pawn calls an exported map would have and handed to the Pawn
// importer. What SA-MP can't show is listed in the open report.
//
// MTA:SA .map: objects keep their rotation as it is. MTA turns objects with
// the game's own CMatrix::SetRotate (its "ZXY" order), and SA-MP's
// CObject::RotateMatrix builds the very same matrix, so the angles carry
// over unchanged. Vehicles and peds only keep their heading (rotZ).
//
// IPL text (inst sections): the quaternion is stored as the game's IPL
// loader keeps it and turned into SA-MP's angles like any map object.

static bool gLastUsesStreamer;

bool
LastForeignMapUsesStreamer(void)
{
	return gLastUsesStreamer;
}

bool
IsForeignMapFile(const char *path)
{
	size_t n = path ? strlen(path) : 0;
	return (n > 4 && rw::strncmp_ci(path + n - 4, ".map", 4) == 0) ||
		(n > 4 && rw::strncmp_ci(path + n - 4, ".ipl", 4) == 0);
}

static bool
readWholeFile(const char *path, std::string &out)
{
	FILE *f = fopen(path, "rb");
	if(f == nil)
		return false;
	char buf[65536];
	size_t n;
	out.clear();
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out.append(buf, n);
	fclose(f);
	return true;
}

static std::string
pawnString(const std::string &s)
{
	std::string out = "\"";
	for(size_t i = 0; i < s.size(); i++){
		if(s[i] == '"' || s[i] == '\\')
			out += '\\';
		out += s[i];
	}
	return out + "\"";
}

static void
note(std::map<std::string, int> &dropped, const std::string &what, int count = 1)
{
	dropped[what] += count;
}

static void
reportDropped(const std::map<std::string, int> &dropped)
{
	for(std::map<std::string, int>::const_iterator it = dropped.begin(); it != dropped.end(); ++it){
		char line[256];
		snprintf(line, sizeof(line), "%d %s", it->second, it->first.c_str());
		AddPawnImportNote(line);
		log("ImportForeignMap: left out %s\n", line);
	}
}

// ---- MTA:SA .map ----

struct XmlElement {
	std::string tag;
	std::map<std::string, std::string> attrs;
	std::string attr(const char *name, const char *fallback = "") const {
		std::map<std::string, std::string>::const_iterator it = attrs.find(name);
		return it == attrs.end() ? fallback : it->second;
	}
	float num(const char *name, float fallback = 0.0f) const {
		std::string v = attr(name);
		return v.empty() ? fallback : (float)atof(v.c_str());
	}
	bool has(const char *name) const { return attrs.count(name) != 0; }
};

static std::string
xmlUnescape(const std::string &s)
{
	std::string out;
	for(size_t i = 0; i < s.size(); i++){
		if(s[i] != '&'){
			out += s[i];
			continue;
		}
		size_t end = s.find(';', i);
		if(end == std::string::npos){ out += s[i]; continue; }
		std::string e = s.substr(i + 1, end - i - 1);
		if(e == "amp") out += '&';
		else if(e == "lt") out += '<';
		else if(e == "gt") out += '>';
		else if(e == "quot") out += '"';
		else if(e == "apos") out += '\'';
		else if(!e.empty() && e[0] == '#') out += (char)strtol(e.c_str() + (e[1] == 'x' ? 2 : 1), nil, e[1] == 'x' ? 16 : 10);
		else { out += s.substr(i, end - i + 1); }
		i = end;
	}
	return out;
}

// Start tags with their attributes; .map files are flat.
static void
parseXml(const std::string &src, std::vector<XmlElement> &out)
{
	size_t i = 0;
	while((i = src.find('<', i)) != std::string::npos){
		if(src.compare(i, 4, "<!--") == 0){
			size_t end = src.find("-->", i);
			i = end == std::string::npos ? src.size() : end + 3;
			continue;
		}
		if(i + 1 < src.size() && (src[i+1] == '/' || src[i+1] == '?' || src[i+1] == '!')){
			i++;
			continue;
		}
		size_t p = i + 1;
		XmlElement e;
		while(p < src.size() && (isalnum((unsigned char)src[p]) || src[p] == '_' || src[p] == '-' || src[p] == ':'))
			e.tag += src[p++];
		while(p < src.size() && src[p] != '>'){
			while(p < src.size() && isspace((unsigned char)src[p])) p++;
			std::string name;
			while(p < src.size() && !isspace((unsigned char)src[p]) && src[p] != '=' && src[p] != '>' && src[p] != '/')
				name += src[p++];
			while(p < src.size() && isspace((unsigned char)src[p])) p++;
			if(p < src.size() && src[p] == '='){
				p++;
				while(p < src.size() && isspace((unsigned char)src[p])) p++;
				char q = p < src.size() ? src[p] : 0;
				if(q == '"' || q == '\''){
					size_t end = src.find(q, p + 1);
					if(end == std::string::npos) end = src.size();
					e.attrs[name] = xmlUnescape(src.substr(p + 1, end - p - 1));
					p = end + 1;
				}
			}else if(name.empty())
				p++;
		}
		if(!e.tag.empty())
			out.push_back(e);
		i = p;
	}
}

// Weapon IDs to the models SA-MP pickups use.
static int
weaponModel(int weapon)
{
	static const int models[47] = {
		0, 331, 333, 334, 335, 336, 337, 338, 339, 341, 321, 322, 323, 324, 325, 326, 342, 343, 344,
		0, 0, 0, 346, 347, 348, 349, 350, 351, 352, 353, 355, 356, 372, 357, 358, 359, 360, 361, 362,
		363, 364, 365, 366, 367, 368, 369, 371,
	};
	return weapon >= 0 && weapon < 47 ? models[weapon] : 0;
}

static void
splitNumbers(const std::string &s, std::vector<int> &out)
{
	out.clear();
	const char *p = s.c_str();
	while(*p){
		while(*p && !(isdigit((unsigned char)*p) || *p == '-')) p++;
		if(!*p) break;
		out.push_back((int)strtol(p, (char**)&p, 10));
	}
}

// The nearest SA-MP colour to an RGB colour (MTA saves vehicles' RGB colours).
static int
nearestVehicleColour(int r, int g, int b)
{
	int best = 0, bestDist = 1 << 30;
	for(int i = 0; i < 256; i++){
		uint32 c = GetSampVehicleColour(i);
		int dr = (int)(c >> 24 & 0xFF) - r, dg = (int)(c >> 16 & 0xFF) - g, db = (int)(c >> 8 & 0xFF) - b;
		int d = dr*dr + dg*dg + db*db;
		if(d < bestDist){ bestDist = d; best = i; }
	}
	return best;
}

static bool
convertMtaMap(const std::string &src, std::string &pawn, std::map<std::string, int> &dropped)
{
	std::vector<XmlElement> elements;
	parseXml(src, elements);
	bool streamer = false;
	for(size_t i = 0; i < elements.size(); i++)
		if(elements[i].tag == "object" &&
		   (elements[i].num("interior") != 0.0f || elements[i].num("dimension") != 0.0f))
			streamer = true;
	gLastUsesStreamer = streamer;

	std::string body, removals;
	char line[512];
	int vehicles = 0;
	for(size_t i = 0; i < elements.size(); i++){
		const XmlElement &e = elements[i];
		float x = e.num("posX"), y = e.num("posY"), z = e.num("posZ");
		if(e.tag == "object"){
			int model = atoi(e.attr("model").c_str());
			if(streamer)
				snprintf(line, sizeof(line), "\tCreateDynamicObject(%d, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %d, %d);\n",
					model, x, y, z, e.num("rotX"), e.num("rotY"), e.num("rotZ"),
					(int)e.num("dimension"), (int)e.num("interior"));
			else
				snprintf(line, sizeof(line), "\tCreateObject(%d, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f);\n",
					model, x, y, z, e.num("rotX"), e.num("rotY"), e.num("rotZ"));
			body += line;
			if(e.has("scale") && e.num("scale", 1.0f) != 1.0f) note(dropped, "object scale(s) (SA-MP objects can't be scaled)");
			if(e.has("alpha") && e.num("alpha", 255.0f) < 255.0f) note(dropped, "object alpha value(s) (SA-MP objects are opaque)");
			std::string ds = e.attr("doublesided");
			if(ds == "true" || ds == "1") note(dropped, "double-sided object(s) (no SA-MP equivalent)");
			std::string col = e.attr("collisions");
			if(col == "false" || col == "0") note(dropped, "object(s) without collisions (SA-MP objects always collide)");
		}else if(e.tag == "vehicle"){
			int model = atoi(e.attr("model").c_str());
			std::vector<int> colours;
			splitNumbers(e.attr("color"), colours);
			int c1 = -1, c2 = -1;
			if(colours.size() >= 6){
				c1 = nearestVehicleColour(colours[0], colours[1], colours[2]);
				c2 = nearestVehicleColour(colours[3], colours[4], colours[5]);
			}else if(colours.size() >= 2){
				c1 = colours[0];
				c2 = colours[1];
			}else if(colours.size() == 1)
				c1 = colours[0];
			if(e.num("rotX") != 0.0f && fabsf(e.num("rotX") - 360.0f) > 1.0f) note(dropped, "vehicle tilt(s) (SA-MP vehicles only keep their heading)");
			char name[32];
			snprintf(name, sizeof(name), "Haumea_mta_vehicle_%d", vehicles++);
			snprintf(line, sizeof(line), "\tnew %s = CreateVehicle(%d, %.6f, %.6f, %.6f, %.6f, %d, %d, -1);\n",
				name, model, x, y, z, e.num("rotZ"), c1, c2);
			body += line;
			int interior = (int)e.num("interior");
			if(interior > 0){
				snprintf(line, sizeof(line), "\tLinkVehicleToInterior(%s, %d);\n", name, interior);
				body += line;
			}
			std::string paintjob = e.attr("paintjob");
			if(!paintjob.empty() && isdigit((unsigned char)paintjob[0]) && atoi(paintjob.c_str()) <= 2){
				snprintf(line, sizeof(line), "\tChangeVehiclePaintjob(%s, %d);\n", name, atoi(paintjob.c_str()));
				body += line;
			}
			std::vector<int> upgrades;
			splitNumbers(e.attr("upgrades"), upgrades);
			for(size_t u = 0; u < upgrades.size(); u++){
				snprintf(line, sizeof(line), "\tAddVehicleComponent(%s, %d);\n", name, upgrades[u]);
				body += line;
			}
			if(!e.attr("plate").empty())
				body += "\tSetVehicleNumberPlate(" + std::string(name) + ", " + pawnString(e.attr("plate").substr(0, 32)) + ");\n";
			if(e.num("dimension") != 0.0f) note(dropped, "vehicle dimension(s) (vehicles are written to world 0)");
		}else if(e.tag == "ped"){
			snprintf(line, sizeof(line), "\tCreateActor(%d, %.6f, %.6f, %.6f, %.6f);\n",
				atoi(e.attr("model").c_str()), x, y, z, e.num("rotZ"));
			body += line;
		}else if(e.tag == "pickup"){
			std::string type = e.attr("type");
			int model = 0;
			if(type == "health") model = 1240;
			else if(type == "armor" || type == "armour") model = 1242;
			else if(type == "weapon") model = weaponModel(atoi(e.attr("weapon", e.attr("amount").c_str()).c_str()));
			else if(type == "custom") model = atoi(e.attr("amount").c_str());
			else if(!type.empty() && isdigit((unsigned char)type[0])) model = weaponModel(atoi(type.c_str()));
			if(model <= 0){
				note(dropped, "pickup(s) of a kind SA-MP has no model for (" + (type.empty() ? std::string("none") : type) + ")");
				continue;
			}
			snprintf(line, sizeof(line), "\tCreatePickup(%d, 2, %.6f, %.6f, %.6f, -1);\n", model, x, y, z);
			body += line;
		}else if(e.tag == "removeWorldObject"){
			float radius = e.num("radius", 0.25f);
			int model = atoi(e.attr("model").c_str());
			int lod = atoi(e.attr("lodModel").c_str());
			snprintf(line, sizeof(line), "\tRemoveBuildingForPlayer(playerid, %d, %.6f, %.6f, %.6f, %.2f);\n",
				model, x, y, z, radius);
			removals += line;
			if(lod > 0 && lod != model){
				snprintf(line, sizeof(line), "\tRemoveBuildingForPlayer(playerid, %d, %.6f, %.6f, %.6f, %.2f);\n",
					lod, x, y, z, radius);
				removals += line;
			}
		}else if(e.tag == "marker")
			note(dropped, "marker(s) (no SA-MP equivalent outside scripts)");
		else if(e.tag != "map" && e.tag != "meta" && e.tag != "settings" && e.tag != "setting" &&
		        e.tag != "info" && e.tag != "include" && e.tag != "file" && e.tag != "script" &&
		        e.tag != "config")
			note(dropped, "<" + e.tag + "> element(s) (gamemode data SA-MP maps don't have)");
	}
	pawn = streamer ? "#include <a_samp>\n#include <streamer>\n\n" : "#include <a_samp>\n\n";
	if(!removals.empty())
		pawn += "public OnPlayerConnect(playerid)\n{\n" + removals + "\treturn 1;\n}\n\n";
	pawn += "stock MtaMap()\n{\n" + body + "}\n";
	return true;
}

// ---- IPL text ----

static bool
convertIplText(const std::string &src, std::string &pawn, std::map<std::string, int> &dropped)
{
	struct Inst { int id; int interior; float x, y, z; rw::Quat q; int lod; };
	std::vector<Inst> insts;
	bool inInst = false;
	size_t pos = 0;
	while(pos < src.size()){
		size_t end = src.find('\n', pos);
		if(end == std::string::npos) end = src.size();
		std::string line = src.substr(pos, end - pos);
		pos = end + 1;
		size_t hash = line.find('#');
		if(hash != std::string::npos) line.resize(hash);
		std::string t;
		for(size_t i = 0; i < line.size(); i++)
			if(!isspace((unsigned char)line[i])) t += (char)tolower((unsigned char)line[i]);
		if(t == "inst"){ inInst = true; continue; }
		if(t == "end"){ inInst = false; continue; }
		if(!inInst || t.empty())
			continue;
		for(size_t i = 0; i < line.size(); i++)
			if(line[i] == ',') line[i] = ' ';
		Inst inst;
		char name[64];
		inst.lod = -1;
		int n = sscanf(line.c_str(), "%d %63s %d %f %f %f %f %f %f %f %d", &inst.id, name, &inst.interior,
			&inst.x, &inst.y, &inst.z, &inst.q.x, &inst.q.y, &inst.q.z, &inst.q.w, &inst.lod);
		if(n < 10){
			note(dropped, "inst line(s) that couldn't be read");
			continue;
		}
		insts.push_back(inst);
	}
	// LOD entries only stand in for others at a distance; SA-MP has no use for them.
	std::vector<bool> isLod(insts.size(), false);
	for(size_t i = 0; i < insts.size(); i++)
		if(insts[i].lod >= 0 && insts[i].lod < (int)insts.size())
			isLod[insts[i].lod] = true;
	bool streamer = false;
	for(size_t i = 0; i < insts.size(); i++)
		if(!isLod[i] && (insts[i].interior & 0xFF) != 0)
			streamer = true;
	gLastUsesStreamer = streamer;
	std::string body;
	char line[512];
	int lods = 0;
	for(size_t i = 0; i < insts.size(); i++){
		if(isLod[i]){
			lods++;
			continue;
		}
		const Inst &in = insts[i];
		rw::V3d e = GetObjectRotationDegrees(in.q);
		if(streamer)
			snprintf(line, sizeof(line), "\tCreateDynamicObject(%d, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, -1, %d);\n",
				in.id, in.x, in.y, in.z, e.x, e.y, e.z, in.interior & 0xFF);
		else
			snprintf(line, sizeof(line), "\tCreateObject(%d, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f);\n",
				in.id, in.x, in.y, in.z, e.x, e.y, e.z);
		body += line;
	}
	if(lods > 0)
		note(dropped, "LOD instance(s) (SA-MP objects need no LOD)", lods);
	pawn = streamer ? "#include <a_samp>\n#include <streamer>\n\n" : "#include <a_samp>\n\n";
	pawn += "stock IplMap()\n{\n" + body + "}\n";
	return !insts.empty();
}

int
ImportForeignMap(const char *path, int *numSkipped)
{
	std::string src, pawn;
	if(!readWholeFile(path, src)){
		log("ImportForeignMap: can't read %s\n", path);
		return -1;
	}
	std::map<std::string, int> dropped;
	size_t n = strlen(path);
	bool mta = n > 4 && rw::strncmp_ci(path + n - 4, ".map", 4) == 0;
	bool ok = mta ? convertMtaMap(src, pawn, dropped) : convertIplText(src, pawn, dropped);
	if(!ok){
		log("ImportForeignMap: nothing to read in %s\n", path);
		return -1;
	}
	int imported = ImportPawnSource(pawn, path, numSkipped);
	reportDropped(dropped);
	return imported;
}
