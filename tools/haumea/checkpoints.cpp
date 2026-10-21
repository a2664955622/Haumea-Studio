#include "haumea.h"
#include "checkpoints.h"

#include <math.h>
#include <string.h>
#include <algorithm>

// Checkpoints drawn the way the SA-MP client shows them, as meshes made here
// like the game's 3D markers (C3dMarkers): a red cylinder that fades upward
// for checkpoints and ground race checkpoints, a hoop for the air types, an
// arrow to the next checkpoint and a chequered flag at the finish. The air
// types 5-8 move as they do in game. Colours, alpha and heights follow the
// game's look by eye: they are close, not measured.

static const rw::RGBA CP_RED = { 255, 0, 0, 255 };

// ---- Triangles, batched through im3d ----

#define CP_VERTS 1200
#define CP_INDICES 3600
static rw::RWDEVICE::Im3DVertex gVerts[CP_VERTS];
static uint16 gIndices[CP_INDICES];
static int gNumVerts, gNumIndices;

static void
flush(void)
{
	if(gNumVerts && gNumIndices){
		rw::im3d::Transform(gVerts, gNumVerts, nil, rw::im3d::EVERYTHING);
		rw::im3d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, gIndices, gNumIndices);
		rw::im3d::End();
	}
	gNumVerts = 0;
	gNumIndices = 0;
}

static void
reserve(int verts, int indices)
{
	if(gNumVerts + verts > CP_VERTS || gNumIndices + indices > CP_INDICES)
		flush();
}

static int
vertex(rw::V3d p, rw::RGBA c)
{
	rw::RWDEVICE::Im3DVertex &v = gVerts[gNumVerts];
	v.setX(p.x);
	v.setY(p.y);
	v.setZ(p.z);
	v.setU(0.0f);
	v.setV(0.0f);
	v.setColor(c.red, c.green, c.blue, c.alpha);
	return gNumVerts++;
}

static void
triangle(int a, int b, int c)
{
	gIndices[gNumIndices++] = (uint16)a;
	gIndices[gNumIndices++] = (uint16)b;
	gIndices[gNumIndices++] = (uint16)c;
}

static void
quad(rw::V3d a, rw::V3d b, rw::V3d c, rw::V3d d, rw::RGBA ca, rw::RGBA cb, rw::RGBA cc, rw::RGBA cd)
{
	reserve(4, 6);
	int i = vertex(a, ca), j = vertex(b, cb), k = vertex(c, cc), l = vertex(d, cd);
	triangle(i, j, k);
	triangle(i, k, l);
}

static rw::RGBA
withAlpha(rw::RGBA c, int alpha)
{
	c.alpha = (uint8)std::max(0, std::min(255, alpha));
	return c;
}

// ---- Shapes ----

static const int SEGMENTS = 32;

// An open cylinder on the ground, opaque-ish at the bottom and gone at the top.
static void
cylinder(rw::V3d base, float radius, float height, rw::RGBA colour, bool solid)
{
	rw::RGBA bottom = solid ? colour : withAlpha(colour, 140);
	rw::RGBA top = solid ? colour : withAlpha(colour, 0);
	for(int k = 0; k < SEGMENTS; k++){
		float a0 = (float)k / SEGMENTS * 2.0f * (float)M_PI;
		float a1 = (float)(k + 1) / SEGMENTS * 2.0f * (float)M_PI;
		rw::V3d p0 = { base.x + radius * cosf(a0), base.y + radius * sinf(a0), base.z };
		rw::V3d p1 = { base.x + radius * cosf(a1), base.y + radius * sinf(a1), base.z };
		quad(p0, p1, { p1.x, p1.y, p1.z + height }, { p0.x, p0.y, p0.z + height }, bottom, bottom, top, top);
	}
}

// A hoop (torus) standing upright, its hole facing along dir.
static void
hoop(rw::V3d centre, rw::V3d dir, float radius, float thickness, rw::RGBA colour)
{
	// The ring's plane: perpendicular to dir (kept horizontal-free of roll).
	rw::V3d up = { 0.0f, 0.0f, 1.0f };
	rw::V3d side = { dir.y, -dir.x, 0.0f };
	float sideLength = sqrtf(side.x * side.x + side.y * side.y);
	if(sideLength < 0.001f)
		side = { 1.0f, 0.0f, 0.0f };
	else
		side = { side.x / sideLength, side.y / sideLength, 0.0f };
	const int tube = 8;
	for(int k = 0; k < SEGMENTS; k++){
		float a0 = (float)k / SEGMENTS * 2.0f * (float)M_PI;
		float a1 = (float)(k + 1) / SEGMENTS * 2.0f * (float)M_PI;
		for(int t = 0; t < tube; t++){
			float b0 = (float)t / tube * 2.0f * (float)M_PI;
			float b1 = (float)(t + 1) / tube * 2.0f * (float)M_PI;
			auto point = [&](float a, float b){
				// Around the ring (a) in the side/up plane, and around the tube (b).
				rw::V3d ring = { side.x * cosf(a) + up.x * sinf(a), side.y * cosf(a) + up.y * sinf(a),
					side.z * cosf(a) + up.z * sinf(a) };
				float r = radius + thickness * cosf(b);
				return rw::V3d{ centre.x + ring.x * r + dir.x * thickness * sinf(b),
					centre.y + ring.y * r + dir.y * thickness * sinf(b),
					centre.z + ring.z * r + dir.z * thickness * sinf(b) };
			};
			quad(point(a0, b0), point(a1, b0), point(a1, b1), point(a0, b1), colour, colour, colour, colour);
		}
	}
}

// An arrow pointing along dir: the shaft-and-head shape twice, crossed (once
// level, once upright), so it reads from any side as the game's arrow does.
static void
arrow(rw::V3d centre, rw::V3d dir, float length, rw::RGBA colour)
{
	float flat = sqrtf(dir.x * dir.x + dir.y * dir.y);
	rw::V3d d = flat > 0.001f ? rw::V3d{ dir.x / flat, dir.y / flat, 0.0f } : rw::V3d{ 1.0f, 0.0f, 0.0f };
	rw::V3d across[2] = { { -d.y, d.x, 0.0f }, { 0.0f, 0.0f, 1.0f } };
	float half = length * 0.5f, shaft = length * 0.12f, head = length * 0.3f, headLength = length * 0.45f;
	for(const rw::V3d &s : across){
		auto at = [&](float along, float side){
			return rw::V3d{ centre.x + d.x * along + s.x * side, centre.y + d.y * along + s.y * side,
				centre.z + s.z * side };
		};
		quad(at(-half, -shaft), at(half - headLength, -shaft), at(half - headLength, shaft), at(-half, shaft),
			colour, colour, colour, colour);
		reserve(3, 3);
		triangle(vertex(at(half - headLength, -head), colour), vertex(at(half, 0.0f), colour),
			vertex(at(half - headLength, head), colour));
	}
}

// The chequered finish flag on a pole, facing the camera's side of dir.
static void
finishFlag(rw::V3d foot, float height, rw::V3d facing)
{
	rw::RGBA pole = { 200, 200, 200, 255 };
	float flat = sqrtf(facing.x * facing.x + facing.y * facing.y);
	rw::V3d s = flat > 0.001f ? rw::V3d{ -facing.y / flat, facing.x / flat, 0.0f } : rw::V3d{ 1.0f, 0.0f, 0.0f };
	float w = height * 0.06f;
	quad({ foot.x - s.x * w, foot.y - s.y * w, foot.z }, { foot.x + s.x * w, foot.y + s.y * w, foot.z },
		{ foot.x + s.x * w, foot.y + s.y * w, foot.z + height }, { foot.x - s.x * w, foot.y - s.y * w, foot.z + height },
		pole, pole, pole, pole);
	const int cols = 4, rows = 3;
	float cell = height * 0.18f;
	for(int r = 0; r < rows; r++)
		for(int c = 0; c < cols; c++){
			rw::RGBA colour = (r + c) % 2 ? rw::RGBA{ 20, 20, 20, 255 } : rw::RGBA{ 245, 245, 245, 255 };
			float z0 = foot.z + height - (r + 1) * cell, z1 = z0 + cell;
			float x0 = w + c * cell, x1 = x0 + cell;
			quad({ foot.x + s.x * x0, foot.y + s.y * x0, z0 }, { foot.x + s.x * x1, foot.y + s.y * x1, z0 },
				{ foot.x + s.x * x1, foot.y + s.y * x1, z1 }, { foot.x + s.x * x0, foot.y + s.y * x0, z1 },
				colour, colour, colour, colour);
		}
}

// ---- Checkpoints ----

// Where a race checkpoint points: at the next one, or north for the last.
static rw::V3d
pointing(const ObjectInst *inst)
{
	ObjectInst *next = GetNextRaceCheckpoint(inst);
	if(next == nil)
		return { 0.0f, 1.0f, 0.0f };
	rw::V3d d = { next->m_translation.x - inst->m_translation.x, next->m_translation.y - inst->m_translation.y,
		next->m_translation.z - inst->m_translation.z };
	float length = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
	return length > 0.01f ? rw::V3d{ d.x / length, d.y / length, d.z / length } : rw::V3d{ 0.0f, 1.0f, 0.0f };
}

void
DrawCheckpointMarker(const ObjectInst *inst, float time, const rw::RGBA *pickColour)
{
	const ObjectPawnProperties *p = GetObjectPawnProperties(inst);
	bool race = p->cpKind == SAMP_CHECKPOINT_RACE;
	int type = race ? std::max(0, std::min(8, p->cpRaceType)) : -1;
	float size = std::max(p->cpSize, 0.5f);
	rw::V3d at = inst->m_translation;
	// The id colour for picking: everything solid, nothing blended.
	bool picking = pickColour != nil;
	rw::RGBA red = picking ? *pickColour : CP_RED;

	if(type >= 3){
		// Air: a hoop facing the next checkpoint, moving as types 5-8 do.
		rw::V3d dir = pointing(inst);
		float radius = size * 0.5f, scale = 1.0f, swing = 0.0f, spin = 0.0f;
		float cycle = fmodf(time, 4.0f) / 4.0f;
		if(type == 5)	// turns a few times, then rests
			spin = cycle < 0.5f ? cycle * 2.0f * 4.0f * (float)M_PI : 0.0f;
		else if(type == 6)	// grows, shrinks, is gone for a moment
			scale = cycle < 0.8f ? 0.6f + 0.4f * sinf(cycle / 0.8f * (float)M_PI) : 0.0f;
		else if(type == 7)	// swings down and back up
			swing = -sinf(cycle * 2.0f * (float)M_PI) * radius * 0.5f;
		else if(type == 8)	// swings up and back down
			swing = sinf(cycle * 2.0f * (float)M_PI) * radius * 0.5f;
		if(spin != 0.0f){
			float c = cosf(spin), s = sinf(spin);
			dir = { dir.x * c - dir.y * s, dir.x * s + dir.y * c, dir.z };
		}
		if(scale > 0.0f)
			hoop({ at.x, at.y, at.z + swing }, dir, radius * scale, radius * 0.08f * scale,
				picking ? red : withAlpha(red, 200));
		if(type == 3 && !picking)
			arrow({ at.x, at.y, at.z + radius * 1.4f }, dir, radius * 0.9f, withAlpha(red, 220));
		else if(type == 4 && !picking)
			finishFlag({ at.x, at.y, at.z + radius * 1.1f }, radius * 0.9f, dir);
		flush();
		return;
	}

	// On the ground: the cylinder, about as tall as it is wide in game.
	float radius = size * 0.5f;
	cylinder(at, radius, std::max(1.0f, size * 0.75f), red, picking);
	if(!picking){
		float bob = sinf(time * 2.5f) * 0.15f;
		rw::V3d above = { at.x, at.y, at.z + std::max(1.0f, size * 0.75f) + 1.0f + bob };
		if(type == 0)
			arrow(above, pointing(inst), std::max(1.5f, radius), withAlpha(red, 230));
		else if(type == 1)
			finishFlag({ at.x, at.y, at.z + std::max(1.0f, size * 0.75f) }, std::max(1.5f, radius), pointing(inst));
	}
	flush();
}

void
RenderCheckpointMarkers(float time)
{
	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 0);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
	rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
	rw::SetRenderState(rw::TEXTURERASTER, 0);	// untextured
	for(CPtrNode *n = instances.first; n; n = n->next){
		ObjectInst *inst = (ObjectInst*)n->item;
		if(inst->m_isDeleted || inst->m_editorHidden || GetSampElementKind(inst) != SAMP_ELEMENT_CHECKPOINT)
			continue;
		DrawCheckpointMarker(inst, time, nil);
	}
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
}
