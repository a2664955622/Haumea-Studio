// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "tstudio_textures.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

// Texture Studio maps (scriptfiles/tstudio/SavedMaps/*.db) are SQLite files.
// They are read here without SQLite itself: only reading is needed, and the
// tables are small. Each map is turned into the Pawn calls Texture Studio's
// own export writes and handed to the Pawn importer.

namespace {

// ---- Read-only SQLite ----

struct SqlValue
{
	enum { NUL, INT, REAL, TEXT } type;
	int64 i;
	double f;
	std::string s;	// text and blobs, as bytes

	double number(double fallback = 0.0) const {
		return type == INT ? (double)i : type == REAL ? f : fallback;
	}
	int64 integer(int64 fallback = 0) const {
		return type == INT ? i : type == REAL ? (int64)f : fallback;
	}
};

struct SqlTable
{
	std::vector<std::string> columns;
	std::vector<std::vector<SqlValue>> rows;

	int column(const char *name) const {
		for(size_t c = 0; c < columns.size(); c++)
			if(rw::strcmp_ci(columns[c].c_str(), name) == 0)
				return (int)c;
		return -1;
	}
	// A row's value, or a null one for columns added after the row was written.
	const SqlValue &get(const std::vector<SqlValue> &row, const char *name) const {
		static const SqlValue none = { SqlValue::NUL, 0, 0.0, std::string() };
		int c = column(name);
		return c >= 0 && c < (int)row.size() ? row[c] : none;
	}
};

class SqliteFile
{
public:
	bool open(const char *path, std::string &error);
	bool table(const char *name, SqlTable &out);

private:
	std::vector<uint8> data;
	uint32 pageSize = 0, usable = 0;
	std::map<std::string, std::pair<uint32, std::string>> tables;	// name -> rootpage, sql

	const uint8 *page(uint32 number) const {
		if(number == 0 || (uint64)number * pageSize > data.size())
			return nil;
		return &data[(size_t)(number - 1) * pageSize];
	}
	static uint64 varint(const uint8 *p, const uint8 *end, int *length);
	bool payload(const uint8 *cell, const uint8 *end, std::string &out);
	void walk(uint32 pageNumber, int depth, std::vector<std::string> &records);
	static void decode(const std::string &record, std::vector<SqlValue> &out);
};

static uint32
be16(const uint8 *p)
{
	return (uint32)p[0] << 8 | p[1];
}

static uint32
be32(const uint8 *p)
{
	return (uint32)p[0] << 24 | (uint32)p[1] << 16 | (uint32)p[2] << 8 | p[3];
}

uint64
SqliteFile::varint(const uint8 *p, const uint8 *end, int *length)
{
	uint64 v = 0;
	for(int i = 0; i < 9 && p + i < end; i++){
		if(i == 8){
			v = (v << 8) | p[i];
			*length = 9;
			return v;
		}
		v = (v << 7) | (p[i] & 0x7F);
		if((p[i] & 0x80) == 0){
			*length = i + 1;
			return v;
		}
	}
	*length = 1;
	return v;
}

bool
SqliteFile::open(const char *path, std::string &error)
{
	FILE *f = fopen(path, "rb");
	if(f == nil){
		error = "could not open the file";
		return false;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	data.resize(size > 0 ? (size_t)size : 0);
	bool ok = size > 100 && fread(data.data(), 1, data.size(), f) == data.size();
	fclose(f);
	if(!ok || memcmp(data.data(), "SQLite format 3", 16) != 0){
		error = "not an SQLite database";
		return false;
	}
	pageSize = be16(&data[16]);
	if(pageSize == 1)
		pageSize = 65536;
	usable = pageSize - data[20];
	if(pageSize < 512 || usable < 480){
		error = "unsupported page size";
		return false;
	}
	// sqlite_master: type, name, tbl_name, rootpage, sql
	std::vector<std::string> records;
	walk(1, 0, records);
	for(size_t r = 0; r < records.size(); r++){
		std::vector<SqlValue> row;
		decode(records[r], row);
		if(row.size() >= 5 && row[0].s == "table")
			tables[row[1].s] = std::make_pair((uint32)row[3].integer(), row[4].s);
	}
	if(tables.empty()){
		error = "no tables found";
		return false;
	}
	return true;
}

// A table-leaf cell's record, following overflow pages.
bool
SqliteFile::payload(const uint8 *cell, const uint8 *end, std::string &out)
{
	int n;
	uint64 size = varint(cell, end, &n);
	cell += n;
	varint(cell, end, &n);	// rowid
	cell += n;
	uint32 maxLocal = usable - 35;
	uint32 local = (uint32)size;
	if(size > maxLocal){
		uint32 minLocal = (usable - 12) * 32 / 255 - 23;
		uint32 k = minLocal + (uint32)((size - minLocal) % (usable - 4));
		local = k <= maxLocal ? k : minLocal;
	}
	if(cell + local > end)
		return false;
	out.assign((const char*)cell, local);
	if(local == size)
		return true;
	if(cell + local + 4 > end)
		return false;
	uint32 next = be32(cell + local);
	while(next != 0 && out.size() < size){
		const uint8 *overflow = page(next);
		if(overflow == nil)
			return false;
		uint32 chunk = std::min<uint64>(usable - 4, size - out.size());
		out.append((const char*)overflow + 4, chunk);
		next = be32(overflow);
	}
	return out.size() == size;
}

void
SqliteFile::walk(uint32 pageNumber, int depth, std::vector<std::string> &records)
{
	const uint8 *base = page(pageNumber);
	if(base == nil || depth > 32)
		return;
	const uint8 *end = base + pageSize;
	const uint8 *header = pageNumber == 1 ? base + 100 : base;
	uint8 kind = header[0];
	uint32 cells = be16(header + 3);
	if(kind == 0x0D){	// table leaf
		for(uint32 c = 0; c < cells; c++){
			const uint8 *cell = base + be16(header + 8 + c * 2);
			std::string record;
			if(cell < end && payload(cell, end, record))
				records.push_back(record);
		}
	}else if(kind == 0x05){	// table interior
		for(uint32 c = 0; c < cells; c++)
			walk(be32(base + be16(header + 12 + c * 2)), depth + 1, records);
		walk(be32(header + 8), depth + 1, records);
	}
}

void
SqliteFile::decode(const std::string &record, std::vector<SqlValue> &out)
{
	const uint8 *p = (const uint8*)record.data();
	const uint8 *end = p + record.size();
	int n;
	uint64 headerSize = varint(p, end, &n);
	const uint8 *type = p + n;
	const uint8 *body = p + headerSize;
	while(type < p + headerSize && body <= end){
		uint64 serial = varint(type, end, &n);
		type += n;
		SqlValue v = { SqlValue::NUL, 0, 0.0, std::string() };
		static const int intSizes[] = { 0, 1, 2, 3, 4, 6, 8 };
		if(serial >= 1 && serial <= 6){
			int size = intSizes[serial];
			if(body + size > end)
				break;
			int64 value = (body[0] & 0x80) ? -1 : 0;	// sign extend
			for(int b = 0; b < size; b++)
				value = (value << 8) | body[b];
			v.type = SqlValue::INT;
			v.i = value;
			body += size;
		}else if(serial == 7){
			if(body + 8 > end)
				break;
			uint64 bits = 0;
			for(int b = 0; b < 8; b++)
				bits = (bits << 8) | body[b];
			memcpy(&v.f, &bits, 8);
			v.type = SqlValue::REAL;
			body += 8;
		}else if(serial == 8 || serial == 9){
			v.type = SqlValue::INT;
			v.i = serial == 9;
		}else if(serial >= 12){
			uint64 size = (serial - 12) / 2;
			if(body + size > end)
				break;
			v.type = SqlValue::TEXT;
			v.s.assign((const char*)body, (size_t)size);
			body += size;
		}
		out.push_back(v);
	}
}

// Column names from a CREATE TABLE statement, in order.
static void
columnsFromSql(const std::string &sql, std::vector<std::string> &columns)
{
	size_t open = sql.find('('), close = sql.rfind(')');
	if(open == std::string::npos || close == std::string::npos || close <= open)
		return;
	std::string body = sql.substr(open + 1, close - open - 1);
	int depth = 0;
	bool quote = false;
	size_t start = 0;
	for(size_t i = 0; i <= body.size(); i++){
		char c = i < body.size() ? body[i] : ',';
		if(c == '\'') quote = !quote;
		if(quote) continue;
		if(c == '(') depth++;
		else if(c == ')') depth--;
		else if(c == ',' && depth == 0){
			std::string def = body.substr(start, i - start);
			size_t a = def.find_first_not_of(" \t\r\n");
			if(a != std::string::npos){
				char q = def[a];
				size_t b;
				if(q == '`' || q == '"' || q == '['){
					char close = q == '[' ? ']' : q;
					b = def.find(close, a + 1);
					columns.push_back(def.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1));
				}else{
					b = def.find_first_of(" \t\r\n", a);
					columns.push_back(def.substr(a, b == std::string::npos ? std::string::npos : b - a));
				}
			}
			start = i + 1;
		}
	}
}

bool
SqliteFile::table(const char *name, SqlTable &out)
{
	std::map<std::string, std::pair<uint32, std::string>>::iterator it = tables.begin();
	for(; it != tables.end(); ++it)
		if(rw::strcmp_ci(it->first.c_str(), name) == 0)
			break;
	if(it == tables.end())
		return false;
	columnsFromSql(it->second.second, out.columns);
	std::vector<std::string> records;
	walk(it->second.first, 0, records);
	for(size_t r = 0; r < records.size(); r++){
		out.rows.push_back(std::vector<SqlValue>());
		decode(records[r], out.rows.back());
	}
	return true;
}

// ---- Texture Studio ----

// SQLitei's DB::TYPE_ARRAY: a 4 byte length (7 bits per byte, including the
// header itself), then one byte per small value (10xxxxxx, 11xxxxxx when
// negative) or 01xxxxxx groups of 6 bits ending with a byte that has 0x80.
static std::vector<int32>
decompressArray(const std::string &bytes, size_t count)
{
	std::vector<int32> out(count, 0);
	const uint8 *p = (const uint8*)bytes.data();
	if(bytes.size() < 4)
		return out;
	size_t total = ((size_t)(p[0] & 0x7F) << 21) | ((size_t)(p[1] & 0x7F) << 14) |
		((size_t)(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
	total = std::min(total, bytes.size());
	size_t o = 0;
	for(size_t i = 4; i < total && o < count; i++){
		if(p[i] & 0x80){
			out[o++] = (p[i] & 0x40) ? -(int32)(p[i] & 0x3F) : (int32)(p[i] & 0x3F);
		}else{
			uint32 v = p[i] & 0x3F;
			while(i + 1 < total){
				v = (v << 6) | (p[++i] & 0x3F);
				if(p[i] & 0x80)
					break;
			}
			out[o++] = (int32)v;
		}
	}
	return out;
}

static const char *tstudioFonts[] = {
	"Arial", "courier", "Webdings", "Wingdings", "GTAWEAPON3", "Calibri",
	"Engravers MT", "Quartz MS", "Segoe Keycaps", "Fixedsys",
};

static void
appendQuoted(std::string &out, const std::string &text)
{
	// Texture Studio exports its text between quotes as stored, so escapes
	// typed into it (\n) keep working; only quotes need escaping.
	out += '"';
	for(size_t i = 0; i < text.size(); i++){
		if(text[i] == '"') out += "\\\"";
		else if(text[i] == '\n') out += "\\n";
		else if(text[i] != '\r') out += text[i];
	}
	out += '"';
}

static void
appendf(std::string &out, const char *fmt, ...)
{
	char buffer[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	out += buffer;
}

}	// namespace

bool
BuildTextureStudioPawn(const char *path, std::string &source, std::string &error)
{
	SqliteFile db;
	if(!db.open(path, error))
		return false;
	SqlTable objects, removed, vehicles, settings;
	if(!db.table("Objects", objects)){
		error = "no Objects table; is this a Texture Studio map?";
		return false;
	}
	db.table("RemovedBuildings", removed);
	db.table("Vehicles", vehicles);
	db.table("Settings", settings);

	int world = -1, interior = -1;
	if(!settings.rows.empty()){
		world = (int)settings.get(settings.rows[0], "VirtualWorld").integer(-1);
		interior = (int)settings.get(settings.rows[0], "Interior").integer(-1);
	}

	source = "// Converted by Haumea from the Texture Studio map ";
	source += path;
	source += "\n#include <a_samp>\n#include <streamer>\n\n";

	for(size_t r = 0; r < removed.rows.size(); r++){
		const std::vector<SqlValue> &row = removed.rows[r];
		appendf(source, "RemoveBuildingForPlayer(playerid, %d, %.6f, %.6f, %.6f, %.6f);\n",
			(int)removed.get(row, "ModelID").integer(),
			removed.get(row, "xPos").number(), removed.get(row, "yPos").number(),
			removed.get(row, "zPos").number(), removed.get(row, "Range").number(0.25));
	}

	// Objects in their Texture Studio order.
	std::vector<size_t> order(objects.rows.size());
	for(size_t r = 0; r < order.size(); r++)
		order[r] = r;
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b){
		return objects.get(objects.rows[a], "IndexID").integer((int64)a) <
			objects.get(objects.rows[b], "IndexID").integer((int64)b);
	});
	const int numTextures = (int)(sizeof(tstudioTextures)/sizeof(tstudioTextures[0]));
	for(size_t k = 0; k < order.size(); k++){
		const std::vector<SqlValue> &row = objects.rows[order[k]];
		float drawDistance = (float)objects.get(row, "DrawDistance").number(300.0);
		source += "tmpobjid = CreateDynamicObject(";
		appendf(source, "%d, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %d, %d, -1, STREAMER_OBJECT_SD, ",
			(int)objects.get(row, "ModelID").integer(),
			objects.get(row, "xPos").number(), objects.get(row, "yPos").number(),
			objects.get(row, "zPos").number(), objects.get(row, "rxRot").number(),
			objects.get(row, "ryRot").number(), objects.get(row, "rzRot").number(),
			world, interior);
		if(drawDistance > 0.0f && fabsf(drawDistance - 300.0f) > 0.01f)
			appendf(source, "%.2f);", drawDistance);
		else
			source += "STREAMER_OBJECT_DD);";
		int group = (int)objects.get(row, "GroupID").integer();
		std::string note = objects.get(row, "Note").s;
		if(group > 0 || !note.empty()){
			source += " //";
			if(group > 0)
				appendf(source, " @group(\"Group %d\")", group);
			if(!note.empty()){
				source += " @note(";
				appendQuoted(source, note);
				source += ")";
			}
		}
		source += "\n";

		std::vector<int32> textures = decompressArray(objects.get(row, "TextureIndex").s, MAX_PAWN_MATERIALS);
		std::vector<int32> colors = decompressArray(objects.get(row, "ColorIndex").s, MAX_PAWN_MATERIALS);
		for(int slot = 0; slot < MAX_PAWN_MATERIALS; slot++){
			int t = textures[slot];
			if(t > 0 && t < numTextures)
				appendf(source, "SetDynamicObjectMaterial(tmpobjid, %d, %d, \"%s\", \"%s\", 0x%08X);\n",
					slot, tstudioTextures[t].model, tstudioTextures[t].txd,
					tstudioTextures[t].texture, (uint32)colors[slot]);
			else if(colors[slot] != 0)
				appendf(source, "SetDynamicObjectMaterial(tmpobjid, %d, -1, \"none\", \"none\", 0x%08X);\n",
					slot, (uint32)colors[slot]);
		}
		if(objects.get(row, "usetext").integer() != 0){
			int face = (int)objects.get(row, "FontFace").integer();
			int size = (int)objects.get(row, "FontSize").integer();
			source += "SetDynamicObjectMaterialText(tmpobjid, 0, ";
			appendQuoted(source, objects.get(row, "ObjectText").s);
			appendf(source, ", %d, \"%s\", %d, %d, 0x%08X, 0x%08X, %d);\n",
				// FontSizes[] is OBJECT_MATERIAL_SIZE_32x32 (10) .. 512x512 (140)
				size >= 0 && size < 14 ? (size + 1) * 10 : 90,
				face >= 0 && face < (int)(sizeof(tstudioFonts)/sizeof(tstudioFonts[0])) ?
					tstudioFonts[face] : "Arial",
				(int)objects.get(row, "TextFontSize").integer(24),
				(int)objects.get(row, "FontBold").integer(),
				(uint32)objects.get(row, "FontColor").integer(),
				(uint32)objects.get(row, "BackColor").integer(),
				(int)objects.get(row, "Alignment").integer());
		}
	}

	for(size_t r = 0; r < vehicles.rows.size(); r++){
		const std::vector<SqlValue> &row = vehicles.rows[r];
		int model = (int)vehicles.get(row, "CarModel").integer();
		if(model < 400 || model > 611)
			continue;	// Texture Studio leaves empty vehicle slots as model 0
		appendf(source, "AddStaticVehicleEx(%d, %.6f, %.6f, %.6f, %.6f, %d, %d, 100);\n",
			model,
			vehicles.get(row, "CarSpawnX").number(), vehicles.get(row, "CarSpawnY").number(),
			vehicles.get(row, "CarSpawnZ").number(), vehicles.get(row, "CarSpawnFA").number(),
			(int)vehicles.get(row, "CarColor1").integer(-1),
			(int)vehicles.get(row, "CarColor2").integer(-1));
	}
	return true;
}

int
ImportTextureStudioMap(const char *path, int *numSkipped)
{
	std::string source, error;
	if(!BuildTextureStudioPawn(path, source, error)){
		log("ImportTextureStudioMap: %s: %s\n", path, error.c_str());
		if(numSkipped)
			*numSkipped = 0;
		return -1;
	}
	return ImportPawnSource(source, path, numSkipped);
}
