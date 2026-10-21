// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "radarart.h"
#include "icons.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// Radar art: the objects seen from above, as gang zones. Each object's
// collision (triangles, boxes, spheres; its bounding box without any) is
// rasterised into a grid of square cells, keeping the highest surface in
// each cell and the object it belongs to. The cells get their colour, and
// runs of one colour are merged greedily into rectangles, one gang zone each.

enum {
	MAX_RADAR_CELLS = 4000000,	// 2000 x 2000
	MAX_RADAR_ZONES = 20000,
};

void
RadarArtDefaults(RadarArtParams *params)
{
	memset(params, 0, sizeof(*params));
	params->source = RADAR_SOURCE_ALL;
	params->cellSize = 4.0f;
	params->colourMode = RADAR_COLOUR_TEXTURE;
	params->colour = 0x8C8C8C00;
	params->numColours = 6;
	params->alpha = 200;
	params->outline = false;
	params->outlineColour = 0x20202000;
	params->heightShading = true;
	params->heightLevels = 3;
	params->zoneMode = SAMP_ZONE_GLOBAL;
	strcpy(params->outGroup, "radar art");
	params->replace = true;
}

// ---- sources ----------------------------------------------------------------

static bool
isSourceObject(ObjectInst *inst)
{
	return inst && !inst->m_isDeleted && !inst->m_isVehicle && !IsPawnWorldObject(inst) &&
		GetSampElementKind(inst) == SAMP_ELEMENT_OBJECT && GetObjectDef(inst->m_objectId);
}

static void
collectSources(const RadarArtParams &params, std::vector<ObjectInst*> &out)
{
	CPtrList &list = params.source == RADAR_SOURCE_SELECTION ? selection : instances;
	for(CPtrNode *p = list.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!isSourceObject(inst))
			continue;
		if(params.source == RADAR_SOURCE_GROUP && strcmp(GetObjectPawnProperties(inst)->group, params.group) != 0)
			continue;
		out.push_back(inst);
	}
	// Creation order, so the same scene gives the same art.
	std::sort(out.begin(), out.end(), [](const ObjectInst *a, const ObjectInst *b){ return a->m_id < b->m_id; });
}

// ---- footprints ---------------------------------------------------------------

struct Tri { rw::V3d v[3]; };

static void
addBox(std::vector<Tri> &tris, const rw::V3d &mn, const rw::V3d &mx)
{
	rw::V3d c[8];
	for(int i = 0; i < 8; i++)
		c[i] = { i & 1 ? mx.x : mn.x, i & 2 ? mx.y : mn.y, i & 4 ? mx.z : mn.z };
	static const int faces[6][4] = {
		{ 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 3, 7, 5 },
	};
	for(int f = 0; f < 6; f++){
		tris.push_back({ { c[faces[f][0]], c[faces[f][1]], c[faces[f][2]] } });
		tris.push_back({ { c[faces[f][0]], c[faces[f][2]], c[faces[f][3]] } });
	}
}

// A model's footprint in its own frame: its collision, else its collision
// bounding box, else its mesh's box.
static const std::vector<Tri> &
modelFootprint(int model)
{
	static std::unordered_map<int, std::vector<Tri>> cache;
	std::unordered_map<int, std::vector<Tri>>::iterator it = cache.find(model);
	if(it != cache.end())
		return it->second;
	std::vector<Tri> &tris = cache[model];
	ObjectDef *obj = GetObjectDef(model);
	CColModel *col = obj ? obj->m_colModel : nil;
	if(col){
		for(int i = 0; i < col->numTriangles; i++){
			const CColTriangle *t = &col->triangles[i];
			Tri tri;
			int idx[3] = { t->a, t->b, t->c };
			for(int k = 0; k < 3; k++)
				tri.v[k] = col->flags & 0x80 ? col->compVertices[idx[k]].Uncompress() : col->vertices[idx[k]];
			tris.push_back(tri);
		}
		for(int i = 0; i < col->numBoxes; i++)
			addBox(tris, col->boxes[i].box.min, col->boxes[i].box.max);
		for(int i = 0; i < col->numSpheres; i++){
			// The top of the sphere as an octagon.
			const CSphere &s = col->spheres[i].sph;
			rw::V3d top = { s.center.x, s.center.y, s.center.z + s.radius };
			for(int k = 0; k < 8; k++){
				float a0 = k * (float)M_PI / 4.0f, a1 = (k + 1) * (float)M_PI / 4.0f;
				rw::V3d p0 = { s.center.x + cosf(a0) * s.radius, s.center.y + sinf(a0) * s.radius, s.center.z };
				rw::V3d p1 = { s.center.x + cosf(a1) * s.radius, s.center.y + sinf(a1) * s.radius, s.center.z };
				tris.push_back({ { top, p0, p1 } });
			}
		}
		if(tris.empty())
			addBox(tris, col->boundingBox.min, col->boundingBox.max);
		return tris;
	}
	rw::Atomic *atomic = obj ? obj->m_atomics[0] : nil;
	rw::Geometry *geo = atomic ? atomic->geometry : nil;
	if(geo && geo->morphTargets && geo->morphTargets[0].vertices && geo->numVertices > 0){
		rw::V3d mn = geo->morphTargets[0].vertices[0], mx = mn;
		for(int i = 1; i < geo->numVertices; i++){
			const rw::V3d &v = geo->morphTargets[0].vertices[i];
			mn = { std::min(mn.x, v.x), std::min(mn.y, v.y), std::min(mn.z, v.z) };
			mx = { std::max(mx.x, v.x), std::max(mx.y, v.y), std::max(mx.z, v.z) };
		}
		addBox(tris, mn, mx);
	}
	return tris;
}

// World axes of a stored (conjugated) rotation, as the map tools use them.
static rw::Matrix
basisOf(const rw::Quat &stored)
{
	rw::Matrix m;
	m.rotate(rw::conj(stored), rw::COMBINEREPLACE);
	return m;
}

static rw::V3d
toWorld(const rw::Matrix &m, const rw::V3d &pos, const rw::V3d &v)
{
	return add(pos, add(add(scale(m.right, v.x), scale(m.up, v.y)), scale(m.at, v.z)));
}

// ---- texture colours ------------------------------------------------------------

static uint32
rgb565(uint16 c)
{
	uint32 r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
	return ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8) | (b * 255 / 31);
}

static void
addRGB(double sum[4], uint32 rgb, double weight)
{
	sum[0] += ((rgb >> 16) & 0xFF) * weight;
	sum[1] += ((rgb >> 8) & 0xFF) * weight;
	sum[2] += (rgb & 0xFF) * weight;
	sum[3] += weight;
}

// The colours of DXT blocks: 1, 3 or 5 (alpha blocks first for 3 and 5).
static void
sumDxt(const uint8 *data, int size, int dxt, double sum[4])
{
	int blockSize = dxt == 1 ? 8 : 16;
	for(int at = 0; at + blockSize <= size; at += blockSize){
		const uint8 *b = data + at + (dxt == 1 ? 0 : 8);
		uint16 c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
		uint32 p[4] = { rgb565(c0), rgb565(c1), 0, 0 };
		bool transparent = dxt == 1 && c0 <= c1;
		for(int k = 0; k < 3; k++){
			int a = (p[0] >> (k * 8)) & 0xFF, z = (p[1] >> (k * 8)) & 0xFF;
			p[2] |= (uint32)(transparent ? (a + z) / 2 : (2 * a + z) / 3) << (k * 8);
			p[3] |= (uint32)(transparent ? 0 : (a + 2 * z) / 3) << (k * 8);
		}
		uint32 bits = b[4] | b[5] << 8 | b[6] << 16 | (uint32)b[7] << 24;
		for(int k = 0; k < 16; k++){
			int index = (bits >> (k * 2)) & 3;
			if(!(transparent && index == 3))
				addRGB(sum, p[index], 1.0);
		}
	}
}

// The average colour of a texture, read from its smallest level of at least
// 4x4 pixels; false when its format isn't readable here.
static bool
rasterColour(rw::Raster *raster, uint32 *out)
{
	if(raster == nil || raster->width <= 0 || raster->height <= 0)
		return false;
	int dxt = 0;
	bool bgra = false;
#ifdef RW_GL3
	{
		using namespace rw::gl3;
		Gl3Raster *natras = GETGL3RASTEREXT(raster);
		if(natras->isCompressed){
			switch(natras->internalFormat){
			case 0x83F0: case 0x83F1: dxt = 1; break;	// GL_COMPRESSED_RGB(A)_S3TC_DXT1_EXT
			case 0x83F2: dxt = 3; break;
			case 0x83F3: dxt = 5; break;
			default: return false;
			}
		}
	}
#elif defined(RW_D3D9)
	{
		rw::d3d::D3dRaster *natras = GETD3DRASTEREXT(raster);
		bgra = true;
		if(natras->customFormat){
			switch(natras->format){
			case 0x31545844: dxt = 1; break;	// DXT1
			case 0x32545844: case 0x33545844: dxt = 3; break;
			case 0x34545844: case 0x35545844: dxt = 5; break;
			default: return false;
			}
		}
	}
#else
	return false;
#endif
	int fmt = raster->format & 0xF00;
	if(dxt == 0 && fmt != rw::Raster::C8888 && fmt != rw::Raster::C888)
		return false;
	if(raster->format & (rw::Raster::PAL4 | rw::Raster::PAL8))
		return false;
	int levels = std::max(1, raster->getNumLevels());
	int level = 0;
	while(level + 1 < levels && (raster->width >> (level + 1)) >= 4 && (raster->height >> (level + 1)) >= 4)
		level++;
	int w = std::max(1, raster->width >> level), h = std::max(1, raster->height >> level);
	uint8 *pixels = raster->lock(level, rw::Raster::LOCKREAD);
	if(pixels == nil)
		return false;
	double sum[4] = { 0, 0, 0, 0 };
	if(dxt){
		int size = ((w + 3) / 4) * ((h + 3) / 4) * (dxt == 1 ? 8 : 16);
		sumDxt(pixels, size, dxt, sum);
	}else{
		int bpp = fmt == rw::Raster::C8888 ? 4 : 3;
		int stride = raster->stride > 0 ? raster->stride : w * bpp;
		for(int y = 0; y < h; y++)
			for(int x = 0; x < w; x++){
				const uint8 *px = pixels + y * stride + x * bpp;
				double alpha = bpp == 4 ? px[3] / 255.0 : 1.0;
				uint32 r = bgra ? px[2] : px[0], b = bgra ? px[0] : px[2];
				addRGB(sum, r << 16 | px[1] << 8 | b, alpha);
			}
	}
	raster->unlock(level);
	if(sum[3] <= 0.0)
		return false;
	*out = (uint32)(sum[0] / sum[3]) << 16 | (uint32)(sum[1] / sum[3]) << 8 | (uint32)(sum[2] / sum[3]);
	return true;
}

static uint32
textureColour(rw::Texture *texture, uint32 fallback)
{
	static std::unordered_map<rw::Texture*, uint32> cache;
	if(texture == nil)
		return fallback;
	std::unordered_map<rw::Texture*, uint32>::iterator it = cache.find(texture);
	if(it != cache.end())
		return it->second;
	uint32 c;
	if(!rasterColour(texture->raster, &c))
		c = fallback;
	cache[texture] = c;
	return c;
}

// A model's colour from above: its materials' textures (times their
// colour), weighted by the area of the upward-facing triangles that use them.
static uint32
modelColour(int model, uint32 fallback)
{
	static std::unordered_map<int, uint32> cache;
	std::unordered_map<int, uint32>::iterator it = cache.find(model);
	if(it != cache.end())
		return it->second;
	ObjectDef *obj = GetObjectDef(model);
	if(obj && !obj->IsLoaded() && !obj->m_cantLoad)
		obj->Load();
	uint32 colour = fallback;
	rw::Atomic *atomic = obj ? obj->m_atomics[0] : nil;
	rw::Geometry *geo = atomic ? atomic->geometry : nil;
	if(geo && geo->matList.numMaterials > 0){
		std::vector<double> weight(geo->matList.numMaterials, 0.0);
		bool haveTris = geo->triangles && geo->morphTargets && geo->morphTargets[0].vertices;
		for(int i = 0; haveTris && i < geo->numTriangles; i++){
			const rw::Triangle &t = geo->triangles[i];
			if(t.matId >= geo->matList.numMaterials)
				continue;
			const rw::V3d *v = geo->morphTargets[0].vertices;
			rw::V3d n = cross(sub(v[t.v[1]], v[t.v[0]]), sub(v[t.v[2]], v[t.v[0]]));
			if(n.z > 0.0f)
				weight[t.matId] += n.z;	// twice the area seen from above
		}
		double total = 0.0;
		for(size_t m = 0; m < weight.size(); m++)
			total += weight[m];
		if(total <= 0.0)
			std::fill(weight.begin(), weight.end(), 1.0);
		double sum[4] = { 0, 0, 0, 0 };
		for(int m = 0; m < geo->matList.numMaterials; m++){
			rw::Material *mat = geo->matList.materials[m];
			if(mat == nil || weight[m] <= 0.0)
				continue;
			uint32 c = textureColour(mat->texture, fallback);
			// Times the material colour, as the game draws it.
			uint32 r = ((c >> 16) & 0xFF) * mat->color.red / 255;
			uint32 g = ((c >> 8) & 0xFF) * mat->color.green / 255;
			uint32 b = (c & 0xFF) * mat->color.blue / 255;
			addRGB(sum, r << 16 | g << 8 | b, weight[m]);
		}
		if(sum[3] > 0.0)
			colour = (uint32)(sum[0] / sum[3]) << 16 | (uint32)(sum[1] / sum[3]) << 8 | (uint32)(sum[2] / sum[3]);
	}
	cache[model] = colour;
	return colour;
}

// Distinct, radar-friendly colours for groups.
static uint32
groupColour(const char *group, uint32 fallback)
{
	static const uint32 palette[] = {
		0x4E79A7, 0xF28E2B, 0x59A14F, 0xE15759, 0x76B7B2, 0xEDC948,
		0xB07AA1, 0xFF9DA7, 0x9C755F, 0xBAB0AC, 0x2F6B3A, 0x8CD17D,
	};
	if(group == nil || group[0] == '\0')
		return fallback;
	uint32 hash = 2166136261u;
	for(const char *p = group; *p; p++)
		hash = (hash ^ (uint8)*p) * 16777619u;
	return palette[hash % (sizeof(palette)/sizeof(palette[0]))];
}

// k-means over the colours, weighted by how many cells have each; the
// first centres are the most common colours.
static void
quantise(std::vector<uint32> &cellColour, const std::vector<bool> &filled, int k)
{
	std::map<uint32, int> counts;
	for(size_t i = 0; i < cellColour.size(); i++)
		if(filled[i])
			counts[cellColour[i]]++;
	if((int)counts.size() <= k)
		return;
	std::vector<std::pair<int, uint32>> byCount;
	for(std::map<uint32, int>::iterator it = counts.begin(); it != counts.end(); ++it)
		byCount.push_back({ it->second, it->first });
	std::sort(byCount.begin(), byCount.end(), [](const std::pair<int, uint32> &a, const std::pair<int, uint32> &b){
		return a.first != b.first ? a.first > b.first : a.second < b.second;
	});
	auto ch = [](uint32 c, int i){ return (float)((c >> (16 - i * 8)) & 0xFF); };
	std::vector<float> centres;
	for(int i = 0; i < k; i++)
		for(int j = 0; j < 3; j++)
			centres.push_back(ch(byCount[i].second, j));
	std::map<uint32, int> nearest;
	for(int iteration = 0; iteration < 10; iteration++){
		std::vector<double> sums(k * 4, 0.0);
		for(size_t i = 0; i < byCount.size(); i++){
			uint32 c = byCount[i].second;
			int best = 0;
			float bestD = 1e30f;
			for(int j = 0; j < k; j++){
				float dr = ch(c, 0) - centres[j*3], dg = ch(c, 1) - centres[j*3+1], db = ch(c, 2) - centres[j*3+2];
				float d = dr*dr + dg*dg + db*db;
				if(d < bestD){
					bestD = d;
					best = j;
				}
			}
			nearest[c] = best;
			for(int j = 0; j < 3; j++)
				sums[best*4 + j] += ch(c, j) * byCount[i].first;
			sums[best*4 + 3] += byCount[i].first;
		}
		for(int j = 0; j < k; j++)
			if(sums[j*4 + 3] > 0.0)
				for(int c = 0; c < 3; c++)
					centres[j*3 + c] = (float)(sums[j*4 + c] / sums[j*4 + 3]);
	}
	std::vector<uint32> packed(k);
	for(int j = 0; j < k; j++)
		packed[j] = (uint32)(centres[j*3] + 0.5f) << 16 | (uint32)(centres[j*3+1] + 0.5f) << 8 |
			(uint32)(centres[j*3+2] + 0.5f);
	for(size_t i = 0; i < cellColour.size(); i++)
		if(filled[i])
			cellColour[i] = packed[nearest[cellColour[i]]];
}

// ---- building -------------------------------------------------------------------

bool
RadarArtBuild(const RadarArtParams &params, RadarArtResult *result, std::string *error)
{
	*result = RadarArtResult();
	result->cellSize = params.cellSize;
	if(!(params.cellSize >= 0.5f && params.cellSize <= 1000.0f)){
		if(error) *error = "the cell size must be 0.5 to 1000 m";
		return false;
	}
	std::vector<ObjectInst*> sources;
	collectSources(params, sources);
	if(sources.empty()){
		if(error) *error = params.source == RADAR_SOURCE_SELECTION ? "select some objects first" :
			params.source == RADAR_SOURCE_GROUP ? "that group has no objects" : "the map has no objects";
		return false;
	}

	// Each source's footprint in the world, and the bounds of them all.
	std::vector<std::vector<Tri>> world(sources.size());
	rw::V3d mn = { 1e30f, 1e30f, 1e30f }, mx = { -1e30f, -1e30f, -1e30f };
	for(size_t s = 0; s < sources.size(); s++){
		const std::vector<Tri> &local = modelFootprint(sources[s]->m_objectId);
		if(local.empty()){
			result->skipped++;
			continue;
		}
		rw::Matrix m = basisOf(sources[s]->m_rotation);
		world[s].resize(local.size());
		for(size_t t = 0; t < local.size(); t++)
			for(int k = 0; k < 3; k++){
				rw::V3d v = toWorld(m, sources[s]->m_translation, local[t].v[k]);
				world[s][t].v[k] = v;
				mn = { std::min(mn.x, v.x), std::min(mn.y, v.y), std::min(mn.z, v.z) };
				mx = { std::max(mx.x, v.x), std::max(mx.y, v.y), std::max(mx.z, v.z) };
			}
	}
	if(mn.x > mx.x){
		if(error) *error = "none of the objects has a footprint";
		return false;
	}
	float cs = params.cellSize;
	// Cells line up with multiples of the cell size, so art made in parts meets.
	float x0 = floorf(mn.x / cs) * cs, y0 = floorf(mn.y / cs) * cs;
	long long cols = (long long)ceilf((mx.x - x0) / cs) + 1, rows = (long long)ceilf((mx.y - y0) / cs) + 1;
	if(cols * rows > MAX_RADAR_CELLS){
		float need = sqrtf((mx.x - mn.x) * (mx.y - mn.y) / MAX_RADAR_CELLS);
		if(error){
			char buf[128];
			snprintf(buf, sizeof(buf), "too many cells for this area; use cells of at least %.1f m", ceilf(need * 10.0f) / 10.0f);
			*error = buf;
		}
		return false;
	}
	result->cols = (int)cols;
	result->rows = (int)rows;

	size_t numCells = (size_t)(cols * rows);
	std::vector<float> top(numCells, -1e30f);
	std::vector<int> owner(numCells, -1);
	for(size_t s = 0; s < sources.size(); s++){
		if(world[s].empty())
			continue;
		result->objects++;
		for(const Tri &t : world[s]){
			const rw::V3d &a = t.v[0], &b = t.v[1], &c = t.v[2];
			float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
			if(fabsf(area) < 1e-6f)
				continue;	// a wall seen edge on
			int cx0 = std::max(0, (int)floorf((std::min(a.x, std::min(b.x, c.x)) - x0) / cs));
			int cx1 = std::min((int)cols - 1, (int)floorf((std::max(a.x, std::max(b.x, c.x)) - x0) / cs));
			int cy0 = std::max(0, (int)floorf((std::min(a.y, std::min(b.y, c.y)) - y0) / cs));
			int cy1 = std::min((int)rows - 1, (int)floorf((std::max(a.y, std::max(b.y, c.y)) - y0) / cs));
			for(int cy = cy0; cy <= cy1; cy++)
				for(int cx = cx0; cx <= cx1; cx++){
					// The cell's centre, by barycentric weights.
					float px = x0 + (cx + 0.5f) * cs, py = y0 + (cy + 0.5f) * cs;
					float w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) / area;
					float w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) / area;
					float w2 = 1.0f - w0 - w1;
					if(w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
						continue;
					float z = w0 * a.z + w1 * b.z + w2 * c.z;
					size_t i = (size_t)cy * cols + cx;
					if(z > top[i]){
						top[i] = z;
						owner[i] = (int)s;
					}
				}
		}
	}

	// Colours.
	std::vector<bool> filled(numCells);
	std::vector<uint32> colour(numCells, 0);
	float zmin = 1e30f, zmax = -1e30f;
	std::vector<uint32> sourceColour(sources.size(), params.colour >> 8);
	for(size_t s = 0; s < sources.size(); s++){
		if(params.colourMode == RADAR_COLOUR_GROUP)
			sourceColour[s] = groupColour(GetObjectPawnProperties(sources[s])->group, params.colour >> 8);
		else if(params.colourMode == RADAR_COLOUR_TEXTURE && !world[s].empty())
			sourceColour[s] = modelColour(sources[s]->m_objectId, params.colour >> 8);
	}
	for(size_t i = 0; i < numCells; i++){
		filled[i] = owner[i] >= 0;
		if(!filled[i])
			continue;
		result->cells++;
		colour[i] = sourceColour[owner[i]];
		zmin = std::min(zmin, top[i]);
		zmax = std::max(zmax, top[i]);
	}
	if(result->cells == 0){
		if(error) *error = "the footprints are smaller than one cell; use smaller cells";
		return false;
	}
	if(params.colourMode == RADAR_COLOUR_TEXTURE)
		quantise(colour, filled, clamp(params.numColours, 1, 64));
	int levels = clamp(params.heightLevels, 2, 16);
	if(params.heightShading && zmax - zmin > 0.5f)
		for(size_t i = 0; i < numCells; i++){
			if(!filled[i])
				continue;
			int level = std::min(levels - 1, (int)((top[i] - zmin) / (zmax - zmin) * levels));
			float f = 0.55f + 0.45f * level / (levels - 1);
			uint32 c = colour[i];
			colour[i] = (uint32)(((c >> 16) & 0xFF) * f) << 16 | (uint32)(((c >> 8) & 0xFF) * f) << 8 |
				(uint32)((c & 0xFF) * f);
		}
	if(params.outline){
		std::vector<uint32> outlined = colour;
		for(long long cy = 0; cy < rows; cy++)
			for(long long cx = 0; cx < cols; cx++){
				size_t i = (size_t)(cy * cols + cx);
				if(!filled[i])
					continue;
				bool edge = cx == 0 || cy == 0 || cx == cols - 1 || cy == rows - 1 ||
					!filled[i - 1] || !filled[i + 1] || !filled[i - cols] || !filled[i + cols];
				if(edge)
					outlined[i] = params.outlineColour >> 8;
			}
		colour.swap(outlined);
	}

	// Greedy rectangles: the longest run of one colour along a row, then as
	// many rows up as repeat it exactly.
	uint32 alpha = (uint32)clamp(params.alpha, 0, 255);
	std::vector<bool> used(numCells);
	for(long long cy = 0; cy < rows; cy++)
		for(long long cx = 0; cx < cols; cx++){
			size_t i = (size_t)(cy * cols + cx);
			if(!filled[i] || used[i])
				continue;
			uint32 c = colour[i];
			long long w = 1;
			while(cx + w < cols && filled[i + w] && !used[i + w] && colour[i + w] == c)
				w++;
			long long h = 1;
			for(; cy + h < rows; h++){
				size_t row = (size_t)((cy + h) * cols + cx);
				bool same = true;
				for(long long k = 0; k < w && same; k++)
					same = filled[row + k] && !used[row + k] && colour[row + k] == c;
				if(!same)
					break;
			}
			float z = -1e30f;
			for(long long y = 0; y < h; y++)
				for(long long x = 0; x < w; x++){
					size_t j = (size_t)((cy + y) * cols + cx + x);
					used[j] = true;
					z = std::max(z, top[j]);
				}
			RadarArtZone zone = { x0 + cx * cs, y0 + cy * cs, x0 + (cx + w) * cs, y0 + (cy + h) * cs, z, c << 8 | alpha };
			result->zones.push_back(zone);
		}
	return true;
}

bool
RadarArtFitCellSize(const RadarArtParams &params, int maxZones, float *cellSize, std::string *error)
{
	if(maxZones < 1){
		if(error) *error = "fit to at least one zone";
		return false;
	}
	// Fewer, bigger cells make fewer zones (nearly always), so search the
	// cell size on a log scale and keep the smallest that fits.
	RadarArtParams p = params;
	RadarArtResult r;
	std::string why;
	float lo = 0.5f, hi = 1000.0f, best = -1.0f;
	for(int i = 0; i < 24 && hi - lo > 0.05f; i++){
		float mid = sqrtf(lo * hi);
		p.cellSize = mid;
		bool ok = RadarArtBuild(p, &r, &why);
		if(ok && (int)r.zones.size() <= maxZones){
			best = mid;
			hi = mid;
		}else if(!ok && why.find("too many cells") == std::string::npos && why.find("smaller than one cell") == std::string::npos){
			if(error) *error = why;
			return false;
		}else if(!ok && why.find("smaller than one cell") != std::string::npos)
			hi = mid;	// too coarse to show anything
		else
			lo = mid;
	}
	if(best < 0.0f){
		if(error) *error = "no cell size gets it down to that many zones";
		return false;
	}
	// Rounded up to 0.1 m; the grid then lines up differently, so check it
	// still fits and nudge it up if not.
	float cs = ceilf(best * 10.0f) / 10.0f;
	for(int i = 0; i < 50; i++, cs += 0.1f){
		p.cellSize = cs;
		if(RadarArtBuild(p, &r, &why) && (int)r.zones.size() <= maxZones){
			*cellSize = cs;
			return true;
		}
	}
	*cellSize = best;
	return true;
}

// ---- applying -------------------------------------------------------------------

int
RadarArtApply(const RadarArtParams &params, std::string *error)
{
	if(params.outGroup[0] == '\0'){
		if(error) *error = "name the group the zones go in";
		return -1;
	}
	RadarArtResult result;
	if(!RadarArtBuild(params, &result, error))
		return -1;
	if((int)result.zones.size() > MAX_RADAR_ZONES){
		if(error) *error = std::to_string(result.zones.size()) + " zones is more than " +
			std::to_string((int)MAX_RADAR_ZONES) + "; use bigger cells";
		return -1;
	}
	int proxy = GetSampElementProxyModel(SAMP_ELEMENT_GANGZONE);
	if(GetObjectDef(proxy) == nil){
		if(error) *error = "Haumea has no model to draw gang zones with";
		return -1;
	}
	ClearSelection();
	UndoBeginGroup();
	if(params.replace){
		std::vector<ObjectInst*> old;
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(inst && !inst->m_isDeleted && GetSampElementKind(inst) == SAMP_ELEMENT_GANGZONE &&
			   strcmp(GetObjectPawnProperties(inst)->group, params.outGroup) == 0)
				old.push_back(inst);
		}
		for(size_t i = 0; i < old.size(); i++)
			old[i]->Delete();
		if(!old.empty())
			UndoRecordDelete(old.data(), (int)old.size());
	}
	int oldSpawnId = GetSpawnObjectId(), oldSpawnKind = GetSpawnElementKind();
	SetSpawnObjectId(proxy);
	SetSpawnElementKind(SAMP_ELEMENT_GANGZONE);
	std::vector<ObjectInst*> made;
	for(size_t i = 0; i < result.zones.size(); i++){
		const RadarArtZone &z = result.zones[i];
		rw::V3d pos = { (z.minX + z.maxX) * 0.5f, (z.minY + z.maxY) * 0.5f, z.z };
		rw::Quat rot = MakeObjectRotationDegrees({ 0.0f, 0.0f, 0.0f });
		ObjectInst *spawned[4] = {};
		int n = SpawnPlaceObjectNoUndo(pos, &rot, spawned, 4);
		if(n == 0)
			continue;
		ObjectInst *inst = spawned[n - 1];
		ObjectPawnProperties p = *GetObjectPawnProperties(inst);
		p.zoneSizeX = z.maxX - z.minX;
		p.zoneSizeY = z.maxY - z.minY;
		p.zoneColor = z.colour;
		p.zoneMode = params.zoneMode;
		snprintf(p.group, sizeof(p.group), "%s", params.outGroup);
		SetObjectPawnProperties(inst, p, false);
		made.insert(made.end(), spawned, spawned + n);
	}
	SetSpawnObjectId(oldSpawnId);
	SetSpawnElementKind(oldSpawnKind);
	if(!made.empty())
		UndoRecordPaste(made.data(), (int)made.size());
	UndoEndGroup();
	ClearSelection();
	if(made.empty()){
		if(error) *error = "could not make the zones";
		return -1;
	}
	return (int)made.size();
}

// ---- streamed grid ----------------------------------------------------------------

void
GetStreamedZoneGrid(const std::vector<float> &bounds, StreamedZoneGrid *grid)
{
	*grid = StreamedZoneGrid();
	size_t n = bounds.size() / 4;
	if(n == 0){
		grid->cols = grid->rows = 1;
		return;
	}
	const float cs = STREAMED_ZONE_CELL;
	float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
	for(size_t i = 0; i < n; i++){
		float cx = (bounds[i*4] + bounds[i*4+2]) * 0.5f, cy = (bounds[i*4+1] + bounds[i*4+3]) * 0.5f;
		mnx = std::min(mnx, cx); mxx = std::max(mxx, cx);
		mny = std::min(mny, cy); mxy = std::max(mxy, cy);
	}
	grid->x0 = (floorf(mnx / cs) - 1.0f) * cs;
	grid->y0 = (floorf(mny / cs) - 1.0f) * cs;
	grid->cols = (int)(floorf(mxx / cs) - floorf(mnx / cs)) + 3;
	grid->rows = (int)(floorf(mxy / cs) - floorf(mny / cs)) + 3;
	std::vector<int> count(grid->cols * grid->rows, 0);
	for(size_t i = 0; i < n; i++){
		float cx = (bounds[i*4] + bounds[i*4+2]) * 0.5f, cy = (bounds[i*4+1] + bounds[i*4+3]) * 0.5f;
		int col = clamp((int)floorf((cx - grid->x0) / cs), 0, grid->cols - 1);
		int row = clamp((int)floorf((cy - grid->y0) / cs), 0, grid->rows - 1);
		count[row * grid->cols + col]++;
	}
	for(int row = 0; row < grid->rows; row++)
		for(int col = 0; col < grid->cols; col++){
			int sum = 0;
			for(int dy = -1; dy <= 1; dy++)
				for(int dx = -1; dx <= 1; dx++){
					int r = row + dy, c = col + dx;
					if(r >= 0 && c >= 0 && r < grid->rows && c < grid->cols)
						sum += count[r * grid->cols + c];
				}
			grid->capacity = std::max(grid->capacity, sum);
		}
}

// ---- the panel --------------------------------------------------------------------

static RadarArtParams gParams;
static bool gParamsReady;
static RadarArtResult gResult;
static std::string gError;
static bool gHaveResult;
static bool gPreview = true;
static int gFitTo = 1000;
static double gChangedAt = -1.0;
static RadarArtParams gBuiltParams;
static uint32 gBuiltSeq;
static size_t gBuiltSelection;

static size_t
selectionKey(void)
{
	size_t key = 0;
	for(CPtrNode *p = selection.first; p; p = p->next)
		key = key * 31 + (size_t)((ObjectInst*)p->item)->m_id + 1;
	return key;
}

static void
rebuildIfNeeded(void)
{
	bool changed = !gHaveResult || memcmp(&gParams, &gBuiltParams, sizeof(gParams)) != 0 ||
		gBuiltSeq != GetLatestChangeSeq() ||
		(gParams.source == RADAR_SOURCE_SELECTION && gBuiltSelection != selectionKey());
	if(!changed){
		gChangedAt = -1.0;
		return;
	}
	// Wait for the sliders to settle: building can take a moment.
	double now = ImGui::GetTime();
	if(gChangedAt < 0.0)
		gChangedAt = now;
	if(gHaveResult && now - gChangedAt < 0.3)
		return;
	gError.clear();
	if(!RadarArtBuild(gParams, &gResult, &gError))
		gResult = RadarArtResult();
	gHaveResult = true;
	gBuiltParams = gParams;
	gBuiltSeq = GetLatestChangeSeq();
	gBuiltSelection = selectionKey();
	gChangedAt = -1.0;
}

static bool
colourEdit(const char *label, uint32 *rgb00)
{
	float c[3] = { ((*rgb00 >> 24) & 0xFF) / 255.0f, ((*rgb00 >> 16) & 0xFF) / 255.0f, ((*rgb00 >> 8) & 0xFF) / 255.0f };
	if(!ImGui::ColorEdit3(label, c, ImGuiColorEditFlags_NoInputs))
		return false;
	*rgb00 = (uint32)(c[0] * 255.0f + 0.5f) << 24 | (uint32)(c[1] * 255.0f + 0.5f) << 16 |
		(uint32)(c[2] * 255.0f + 0.5f) << 8;
	return true;
}

static void
drawPreview(void)
{
	size_t n = std::min(gResult.zones.size(), (size_t)6000);
	for(size_t i = 0; i < n; i++){
		const RadarArtZone &z = gResult.zones[i];
		rw::RGBA c = { (uint8)(z.colour >> 24), (uint8)(z.colour >> 16), (uint8)(z.colour >> 8), 255 };
		float h = z.z + 0.5f;
		RenderLine({ z.minX, z.minY, h }, { z.maxX, z.minY, h }, c, c);
		RenderLine({ z.maxX, z.minY, h }, { z.maxX, z.maxY, h }, c, c);
		RenderLine({ z.maxX, z.maxY, h }, { z.minX, z.maxY, h }, c, c);
		RenderLine({ z.minX, z.maxY, h }, { z.minX, z.minY, h }, c, c);
	}
}

void
RadarArtPanel(void)
{
	if(!gParamsReady){
		RadarArtDefaults(&gParams);
		gParamsReady = true;
	}
	if(!ImGui::CollapsingHeader(ICON_FA_SQUARE " Radar art from objects")){
		gHaveResult = false;	// built again when opened
		return;
	}
	ImGui::PushID("radar_art");
	RadarArtParams &p = gParams;

	static const char *sources[] = { "All objects", "Selection", "A group" };
	ImGui::Combo("Objects", &p.source, sources, IM_ARRAYSIZE(sources));
	ImGui::SetItemTooltip("The objects drawn: every scripted object, the selection, or one group.\n"
		"Vehicles, SA-MP elements and unchanged world objects are left out.");
	if(p.source == RADAR_SOURCE_GROUP){
		std::vector<std::string> groups;
		for(CPtrNode *n = instances.first; n; n = n->next){
			ObjectInst *inst = (ObjectInst*)n->item;
			if(!isSourceObject(inst))
				continue;
			const char *g = GetObjectPawnProperties(inst)->group;
			if(g[0] && std::find(groups.begin(), groups.end(), g) == groups.end())
				groups.push_back(g);
		}
		std::sort(groups.begin(), groups.end());
		if(ImGui::BeginCombo("Group", p.group[0] ? p.group : "(pick one)")){
			for(size_t i = 0; i < groups.size(); i++)
				if(ImGui::Selectable(groups[i].c_str(), groups[i] == p.group))
					snprintf(p.group, sizeof(p.group), "%s", groups[i].c_str());
			if(groups.empty())
				ImGui::TextDisabled("No object is in a group.");
			ImGui::EndCombo();
		}
	}

	ImGui::DragFloat("Cell size", &p.cellSize, 0.1f, 0.5f, 1000.0f, "%.1f m");
	ImGui::SetItemTooltip("The art's pixel size. Smaller cells follow the shapes more\n"
		"closely and take more gang zones.");
	p.cellSize = clamp(p.cellSize, 0.5f, 1000.0f);
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
	ImGui::InputInt("##fit_to", &gFitTo, 0);
	gFitTo = clamp(gFitTo, 1, (int)MAX_RADAR_ZONES);
	ImGui::SameLine();
	if(ImGui::Button("Fit to this many zones")){
		float cs;
		std::string why;
		if(RadarArtFitCellSize(p, gFitTo, &cs, &why))
			p.cellSize = cs;
		else
			ToastFor(TOAST_SELECTION, 4.0f, "Radar art: %s", why.c_str());
	}
	ImGui::SetItemTooltip("Picks the smallest cell size whose art needs at most this many zones.");

	ImGui::SeparatorText("Colours");
	static const char *colourModes[] = { "One colour", "By group", "From textures" };
	ImGui::Combo("Colour", &p.colourMode, colourModes, IM_ARRAYSIZE(colourModes));
	ImGui::SetItemTooltip("From textures: each model's colour seen from above, from its\n"
		"textures, reduced to a few colours so neighbouring cells merge.");
	if(p.colourMode == RADAR_COLOUR_TEXTURE){
		ImGui::SliderInt("Colours", &p.numColours, 1, 16);
		ImGui::SetItemTooltip("Fewer colours make fewer zones.");
	}
	colourEdit(p.colourMode == RADAR_COLOUR_ONE ? "Zone colour" : "Fallback colour", &p.colour);
	if(p.colourMode != RADAR_COLOUR_ONE)
		ImGui::SetItemTooltip(p.colourMode == RADAR_COLOUR_GROUP ? "For objects without a group." :
			"For models whose textures can't be read.");
	ImGui::SliderInt("Opacity", &p.alpha, 0, 255);
	ImGui::Checkbox("Outline", &p.outline);
	ImGui::SetItemTooltip("Draws the footprints' edge cells in their own colour.");
	if(p.outline){
		ImGui::SameLine();
		colourEdit("##outline_colour", &p.outlineColour);
	}
	ImGui::Checkbox("Height shading", &p.heightShading);
	ImGui::SetItemTooltip("Higher surfaces are lighter, in a few steps.");
	if(p.heightShading)
		ImGui::SliderInt("Steps", &p.heightLevels, 2, 8);

	ImGui::SeparatorText("Zones");
	static const char *modes[] = { "Global zones", "Per-player zones", "Streamed per-player zones" };
	ImGui::Combo("Shown as", &p.zoneMode, modes, IM_ARRAYSIZE(modes));
	ImGui::SetItemTooltip("Global: GangZoneCreate, 1024 on the whole server.\n"
		"Per-player: CreatePlayerGangZone for every player, 1024 each.\n"
		"Streamed: per-player zones made around each player as they move,\n"
		"in %.0f m cells (streamer areas), so a map can have many more.", STREAMED_ZONE_CELL);
	if(p.zoneMode != SAMP_ZONE_GLOBAL){
		UiBadge("open.mp only", "CreatePlayerGangZone is an open.mp native; SA-MP 0.3.7 has no per-player zones.");
		if(p.zoneMode == SAMP_ZONE_STREAMED){
			ImGui::SameLine();
			UiBadge("streamer", "The map includes the streamer for its areas.");
		}
	}
	ImGui::InputText("Group##out_group", p.outGroup, sizeof(p.outGroup));
	ImGui::SetItemTooltip("The group the zones go in.");
	ImGui::Checkbox("Replace this group's zones", &p.replace);

	rebuildIfNeeded();
	ImGui::Separator();
	if(!gError.empty())
		ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "%s", gError.c_str());
	else if(gHaveResult){
		int n = (int)gResult.zones.size();
		bool over = false;
		if(p.zoneMode == SAMP_ZONE_STREAMED){
			std::vector<float> bounds;
			for(const RadarArtZone &z : gResult.zones){
				bounds.push_back(z.minX); bounds.push_back(z.minY);
				bounds.push_back(z.maxX); bounds.push_back(z.maxY);
			}
			StreamedZoneGrid grid;
			GetStreamedZoneGrid(bounds, &grid);
			over = grid.capacity > 1024;
			ImGui::Text("%d zones, at most %d around a player", n, grid.capacity);
		}else{
			over = n > 1024;
			ImGui::Text("%d zones", n);
		}
		if(over){
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "(over 1024)");
			ImGui::SetItemTooltip(p.zoneMode == SAMP_ZONE_GLOBAL ?
				"The server makes at most 1024 gang zones. Use bigger cells, fewer colours,\n"
				"or streamed zones." : "A player sees at most 1024 gang zones. Use bigger cells or fewer colours.");
		}
		ImGui::TextDisabled("%d objects, %d x %d cells%s", gResult.objects, gResult.cols, gResult.rows,
			gResult.skipped ? " (some without a footprint)" : "");
	}else
		ImGui::TextDisabled("Working...");
	ImGui::Checkbox("Preview##radar_preview", &gPreview);
	ImGui::SameLine();
	ImGui::BeginDisabled(!gError.empty() || !gHaveResult || gResult.zones.empty());
	if(ImGui::Button("Apply##radar_apply")){
		std::string why;
		int made = RadarArtApply(p, &why);
		if(made < 0)
			ToastFor(TOAST_SELECTION, 4.0f, "Radar art: %s", why.c_str());
		else
			Toast(TOAST_SELECTION, "Radar art: %d gang zone(s) in \"%s\"", made, p.outGroup);
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Makes the zones, as one undo step.");
	if(gPreview && gHaveResult && gError.empty())
		drawPreview();
	ImGui::PopID();
}
