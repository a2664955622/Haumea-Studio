#include "haumea.h"
#include "editoricons.h"
#include "samp_loader.h"
#include "playerview.h"

#include <math.h>

// The art: a coloured disc with a white (or dark) glyph, rasterised here
// from simple shapes with 4x4 supersampling, so nothing comes from GTA's
// files and nothing has to be shipped.

namespace {

enum IconKind { ICON_LABEL, ICON_SOUND, ICON_MAPICON, ICON_GANGZONE, ICON_PATHPOINT, ICON_CHECKPOINT, ICON_AREA, ICON_PARTICLE, NUM_ICONS };

struct IconStyle {
	uint8 r, g, b;			// the disc
	uint8 gr, gg, gb;		// the glyph
};

const IconStyle kStyles[NUM_ICONS] = {
	{  66, 133, 244, 255, 255, 255 },	// 3D text label: blue
	{ 156,  90, 220, 255, 255, 255 },	// sound: purple
	{ 245, 145,  40, 255, 255, 255 },	// map icon: orange
	{  52, 168,  83, 255, 255, 255 },	// gang zone: green
	{ 240, 200,  40,  40,  40,  40 },	// path point: yellow, dark glyph
	{ 220,  30,  30, 255, 255, 255 },	// checkpoint: red, as in game (its marker is drawn too)
	{   0, 150, 160, 255, 255, 255 },	// dynamic area: teal
	{ 200,  80, 160, 255, 255, 255 },	// particle emitter: magenta
};

int
iconOf(int kind)
{
	switch(kind){
	case SAMP_ELEMENT_LABEL: return ICON_LABEL;
	case SAMP_ELEMENT_SOUND: return ICON_SOUND;
	case SAMP_ELEMENT_MAPICON: return ICON_MAPICON;
	case SAMP_ELEMENT_GANGZONE: return ICON_GANGZONE;
	case SAMP_ELEMENT_PATHPOINT: return ICON_PATHPOINT;
	case SAMP_ELEMENT_CHECKPOINT: return ICON_CHECKPOINT;
	case SAMP_ELEMENT_AREA: return ICON_AREA;
	}
	return -1;
}

int
iconOfInstance(const ObjectInst *inst)
{
	int model=inst->m_objectId;
	if(GetSampElementKind(inst)==SAMP_ELEMENT_OBJECT && model>=18668 && model<=18748 && SampLoader::IsSampModel(model))
		return ICON_PARTICLE;
	return iconOf(GetSampElementKind(inst));
}

// ---- Shapes, in [-1, 1] with y up ----

float
segmentDistance(float px, float py, float ax, float ay, float bx, float by)
{
	float dx = bx - ax, dy = by - ay;
	float t = ((px - ax)*dx + (py - ay)*dy) / (dx*dx + dy*dy);
	t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
	float qx = ax + t*dx - px, qy = ay + t*dy - py;
	return sqrtf(qx*qx + qy*qy);
}

bool
inTriangle(float px, float py, float ax, float ay, float bx, float by, float cx, float cy)
{
	float d1 = (px - bx)*(ay - by) - (ax - bx)*(py - by);
	float d2 = (px - cx)*(by - cy) - (bx - cx)*(py - cy);
	float d3 = (px - ax)*(cy - ay) - (cx - ax)*(py - ay);
	bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
	return !(neg && pos);
}

bool
inRect(float px, float py, float x0, float y0, float x1, float y1)
{
	return px >= x0 && px <= x1 && py >= y0 && py <= y1;
}

// An arc of a circle around (cx, cy), between angles a0 and a1 (radians).
bool
onArc(float px, float py, float cx, float cy, float r, float width, float a0, float a1)
{
	float dx = px - cx, dy = py - cy;
	float a = atan2f(dy, dx);
	return a >= a0 && a <= a1 && fabsf(sqrtf(dx*dx + dy*dy) - r) <= width*0.5f;
}

bool
glyph(int icon, float x, float y)
{
	const float pi = 3.14159265f;
	switch(icon){
	case ICON_PARTICLE: // a small burst, readable even when the preview is off
		return x*x+y*y<.10f*.10f ||
			segmentDistance(x,y,-.40f,0,.40f,0)<.045f ||
			segmentDistance(x,y,0,-.40f,0,.40f)<.045f ||
			segmentDistance(x,y,-.28f,-.28f,.28f,.28f)<.04f ||
			segmentDistance(x,y,-.28f,.28f,.28f,-.28f)<.04f;
	case ICON_LABEL:	// a "T"
		return inRect(x, y, -0.42f, 0.28f, 0.42f, 0.44f) || inRect(x, y, -0.09f, -0.48f, 0.09f, 0.44f);
	case ICON_SOUND:	// a speaker and two waves
		return inRect(x, y, -0.56f, -0.16f, -0.34f, 0.16f) ||
			inTriangle(x, y, -0.36f, -0.16f, -0.36f, 0.16f, -0.08f, 0.42f) ||
			inTriangle(x, y, -0.36f, -0.16f, -0.08f, 0.42f, -0.08f, -0.42f) ||
			onArc(x, y, -0.08f, 0.0f, 0.26f, 0.09f, -pi/4, pi/4) ||
			onArc(x, y, -0.08f, 0.0f, 0.46f, 0.09f, -pi/4, pi/4);
	case ICON_MAPICON: {	// a map pin with a hole
		float hx = x, hy = y - 0.16f;
		bool hole = hx*hx + hy*hy < 0.13f*0.13f;
		bool head = hx*hx + hy*hy < 0.36f*0.36f;
		return !hole && (head || inTriangle(x, y, -0.31f, 0.0f, 0.31f, 0.0f, 0.0f, -0.56f));
	}
	case ICON_GANGZONE:	// a square area with a diagonal
		return (inRect(x, y, -0.46f, -0.46f, 0.46f, 0.46f) && !inRect(x, y, -0.34f, -0.34f, 0.34f, 0.34f)) ||
			segmentDistance(x, y, -0.4f, -0.4f, 0.4f, 0.4f) < 0.06f;
	case ICON_PATHPOINT: {	// a node: a ring and a dot
		float r = sqrtf(x*x + y*y);
		return r < 0.17f || fabsf(r - 0.38f) < 0.06f;
	}
	case ICON_CHECKPOINT:	// a chequered flag on a pole
		if(inRect(x, y, -0.44f, -0.5f, -0.32f, 0.46f))
			return true;
		if(inRect(x, y, -0.32f, -0.02f, 0.46f, 0.46f)){
			int cx = (int)floorf((x + 0.32f) / 0.195f), cy = (int)floorf((y + 0.02f) / 0.16f);
			return (cx + cy) % 2 == 0;
		}
		return false;
	case ICON_AREA: {	// a region: a dashed ring around a dot
		float r = sqrtf(x*x + y*y);
		float dash = fmodf(atan2f(y, x) + pi*2.0f + pi/16.0f, pi/4.0f);
		return r < 0.12f || (fabsf(r - 0.42f) < 0.07f && dash < pi/8.0f);
	}
	}
	return false;
}

enum Art { ART_ICON, ART_MASK, ART_RING };

// One 64x64 texture: a kind's icon, the pick mask (a hard-edged disc), or
// the selection ring.
rw::Texture*
makeTexture(Art art, int icon)
{
	const int size = 64, ss = 4;
	rw::Image *image = rw::Image::create(size, size, 32);
	if(image == nil)
		return nil;
	image->allocate();
	const IconStyle &style = kStyles[icon < 0 ? 0 : icon];
	for(int py = 0; py < size; py++)
		for(int px = 0; px < size; px++){
			float cover = 0.0f, glyphCover = 0.0f, edge = 0.0f;
			for(int sy = 0; sy < ss; sy++)
				for(int sx = 0; sx < ss; sx++){
					float x = ((px + (sx + 0.5f)/ss) / size)*2.0f - 1.0f;
					float y = 1.0f - ((py + (sy + 0.5f)/ss) / size)*2.0f;
					float r = sqrtf(x*x + y*y);
					if(art == ART_RING){
						if(r <= 0.98f && r >= 0.80f) cover += 1.0f;
						continue;
					}
					if(r <= 0.94f){
						cover += 1.0f;
						if(r > 0.84f) edge += 1.0f;
						else if(glyph(icon, x, y)) glyphCover += 1.0f;
					}
				}
			float n = (float)(ss*ss);
			cover /= n; glyphCover /= n; edge /= n;
			uint8 *p = image->pixels + py*image->stride + px*4;
			if(art == ART_MASK){
				// Hard edged: the pick pass must keep every id colour exact.
				p[0] = p[1] = p[2] = 255;
				p[3] = cover >= 0.5f ? 255 : 0;
				continue;
			}
			if(art == ART_RING){
				p[0] = p[1] = p[2] = 255;
				p[3] = (uint8)(cover*255.0f);
				continue;
			}
			// Disc colour, a darker rim, the glyph on top.
			float base = cover > 0.0f ? 1.0f : 0.0f;
			float rr = style.r, gg = style.g, bb = style.b;
			if(cover > 0.0f){
				float rim = edge / cover;
				rr *= 1.0f - 0.45f*rim; gg *= 1.0f - 0.45f*rim; bb *= 1.0f - 0.45f*rim;
				float gl = glyphCover / cover;
				rr += (style.gr - rr)*gl; gg += (style.gg - gg)*gl; bb += (style.gb - bb)*gl;
			}
			p[0] = (uint8)rr; p[1] = (uint8)gg; p[2] = (uint8)bb;
			p[3] = (uint8)(cover*base*255.0f);
		}
	rw::Raster *raster = rw::Raster::createFromImage(image);
	image->destroy();
	if(raster == nil)
		return nil;
	rw::Texture *tex = rw::Texture::create(raster);
	tex->setFilter(rw::Texture::LINEAR);
	return tex;
}

rw::Texture *gIcons[NUM_ICONS], *gMask, *gRing;
bool gMade;

void
makeArt(void)
{
	if(gMade)
		return;
	gMade = true;
	for(int i = 0; i < NUM_ICONS; i++)
		gIcons[i] = makeTexture(ART_ICON, i);
	gMask = makeTexture(ART_MASK, -1);
	gRing = makeTexture(ART_RING, -1);
}

std::vector<ObjectInst*> gQueued;
int gProxyDraws;

// Past this many gang zones on screen (radar art makes hundreds), only
// selected zones get an icon; the rest are picked by their area.
const int MANY_GANG_ZONES = 64;

// ---- Billboards ----

#define ICON_VERTS 256
rw::RWDEVICE::Im3DVertex gVerts[ICON_VERTS];
uint16 gIndices[ICON_VERTS / 4 * 6];
int gNumVerts;

void
flush(void)
{
	if(gNumVerts == 0)
		return;
	int quads = gNumVerts / 4;
	for(int q = 0; q < quads; q++){
		uint16 *ix = gIndices + q*6;
		ix[0] = q*4; ix[1] = q*4 + 1; ix[2] = q*4 + 2;
		ix[3] = q*4; ix[4] = q*4 + 2; ix[5] = q*4 + 3;
	}
	rw::im3d::Transform(gVerts, gNumVerts, nil, rw::im3d::EVERYTHING);
	rw::im3d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, gIndices, quads*6);
	rw::im3d::End();
	gNumVerts = 0;
}

void
quad(const rw::V3d corners[4], rw::RGBA c)
{
	if(gNumVerts + 4 > ICON_VERTS)
		flush();
	static const float u[4] = { 0, 1, 1, 0 }, v[4] = { 0, 0, 1, 1 };
	for(int i = 0; i < 4; i++){
		rw::RWDEVICE::Im3DVertex &vt = gVerts[gNumVerts++];
		vt.setX(corners[i].x); vt.setY(corners[i].y); vt.setZ(corners[i].z);
		vt.setColor(c.red, c.green, c.blue, c.alpha);
		vt.setU(u[i]); vt.setV(v[i]);
	}
}

struct Billboard {
	rw::V3d centre, right, up;	// right and up are half extents
};

// About 30 pixels at any distance (scaled with the interface), but never
// smaller than 0.35 m up close nor bigger than 12 m far away.
bool
billboardAt(rw::V3d pos, Billboard *b, float scale)
{
	rw::V3d cam = TheCamera.m_position;
	rw::V3d dir = rw::normalize(rw::sub(TheCamera.m_target, cam));
	rw::V3d right = rw::normalize(rw::cross(dir, TheCamera.m_up));
	rw::V3d up = rw::normalize(rw::cross(right, dir));
	float dist = rw::dot(rw::sub(pos, cam), dir);
	if(dist <= 0.05f)
		return false;
	rw::Raster *fb = TheCamera.m_rwcam->frameBuffer;
	float screenHeight = fb && fb->height > 0 ? (float)fb->height : 720.0f;
	float worldPerPixel = 2.0f*dist*TheCamera.m_rwcam->viewWindow.y / screenHeight;
	float size = 30.0f*GetUiScale()*worldPerPixel;
	size = size < 0.35f ? 0.35f : size > 12.0f ? 12.0f : size;
	size *= scale;
	b->centre = pos;
	b->right = rw::scale(right, size*0.5f);
	b->up = rw::scale(up, size*0.5f);
	return true;
}

bool
billboardFor(const ObjectInst *inst, Billboard *b, float scale)
{
	return billboardAt(inst->m_translation, b, scale);
}

void
billboardQuad(const Billboard &b, rw::RGBA c)
{
	rw::V3d corners[4] = {
		rw::add(rw::sub(b.centre, b.right), b.up),
		rw::add(rw::add(b.centre, b.right), b.up),
		rw::sub(rw::add(b.centre, b.right), b.up),
		rw::sub(rw::sub(b.centre, b.right), b.up),
	};
	quad(corners, c);
}

bool
manyZones(void)
{
	int zones = 0;
	for(ObjectInst *inst : gQueued)
		if(GetSampElementKind(inst) == SAMP_ELEMENT_GANGZONE && ++zones > MANY_GANG_ZONES)
			return true;
	return false;
}

bool
drawsIcon(ObjectInst *inst, bool many)
{
	return !(many && GetSampElementKind(inst) == SAMP_ELEMENT_GANGZONE && !inst->m_selected &&
		inst->m_highlight == HIGHLIGHT_NONE);
}

void
setState(bool ztest, bool blend)
{
	rw::SetRenderState(rw::ZWRITEENABLE, 0);
	rw::SetRenderState(rw::ZTESTENABLE, ztest);
	rw::SetRenderState(rw::FOGENABLE, 0);
	rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
	rw::SetRenderState(rw::VERTEXALPHA, blend);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
}

}	// namespace

bool
ElementKindUsesIcon(int kind)
{
	return iconOf(kind) >= 0;
}

bool
InstUsesEditorIcon(const ObjectInst *inst)
{
	return inst && !inst->m_isVehicle && iconOfInstance(inst)>=0;
}

void
ClearElementIcons(void)
{
	gQueued.clear();
	gProxyDraws = 0;
}

void
QueueElementIcon(ObjectInst *inst)
{
	// Player view shows what a player sees: none of these, unless selected.
	if(gPlayerView && !inst->m_selected)
		return;
	gQueued.push_back(inst);
}

int
GetElementProxyModelDraws(void)
{
	return gProxyDraws;
}

void
NoteElementProxyModelDraw(void)
{
	gProxyDraws++;
}

void
RenderElementIcons(void)
{
	if(gQueued.empty())
		return;
	makeArt();
	bool many = manyZones();
	int32 ztest = rw::GetRenderState(rw::ZTESTENABLE), zwrite = rw::GetRenderState(rw::ZWRITEENABLE);
	int32 fog = rw::GetRenderState(rw::FOGENABLE), cull = rw::GetRenderState(rw::CULLMODE);
	int32 aref = rw::GetRenderState(rw::ALPHATESTREF);
	rw::SetRenderState(rw::ALPHATESTREF, 1);
	// Two passes: a faint copy that shows through walls, then the full icon
	// where nothing is in front of it.
	for(int pass = 0; pass < 2; pass++){
		setState(pass == 1, true);
		uint8 alpha = pass == 0 ? 70 : 255;
		// Selection and hover rings behind the icons.
		rw::SetRenderStatePtr(rw::TEXTURERASTER, gRing ? gRing->raster : nil);
		for(ObjectInst *inst : gQueued){
			if(!(inst->m_selected || inst->m_highlight != HIGHLIGHT_NONE) || !drawsIcon(inst, many))
				continue;
			Billboard b;
			if(!billboardFor(inst, &b, 1.35f))
				continue;
			rw::RGBA c = inst->m_selected ? rw::RGBA{ 255, 210, 40, alpha } : rw::RGBA{ 255, 255, 255, (uint8)(alpha*0.7f) };
			billboardQuad(b, c);
		}
		flush();
		for(int icon = 0; icon < NUM_ICONS; icon++){
			rw::SetRenderStatePtr(rw::TEXTURERASTER, gIcons[icon] ? gIcons[icon]->raster : nil);
			for(ObjectInst *inst : gQueued){
				if(iconOfInstance(inst) != icon || !drawsIcon(inst, many))
					continue;
				Billboard b;
				if(billboardFor(inst, &b, 1.0f))
					billboardQuad(b, rw::RGBA{ 255, 255, 255, alpha });
			}
			flush();
		}
	}
	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
	rw::SetRenderState(rw::ZTESTENABLE, ztest);
	rw::SetRenderState(rw::ZWRITEENABLE, zwrite);
	rw::SetRenderState(rw::FOGENABLE, fog);
	rw::SetRenderState(rw::CULLMODE, cull);
	rw::SetRenderState(rw::ALPHATESTREF, aref);
}

void
RenderElementIconsColourCoded(void)
{
	if(gQueued.empty())
		return;
	makeArt();
	bool many = manyZones();
	int32 ztest = rw::GetRenderState(rw::ZTESTENABLE), zwrite = rw::GetRenderState(rw::ZWRITEENABLE);
	int32 fog = rw::GetRenderState(rw::FOGENABLE), cull = rw::GetRenderState(rw::CULLMODE);
	int32 aref = rw::GetRenderState(rw::ALPHATESTREF);
	// Exact id colours: no blending, the mask's hard edge cut by the alpha test.
	rw::SetRenderState(rw::ALPHATESTREF, 128);
	setState(true, false);
	// Many radar-art zones: their area, under the icons, depth tested.
	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
	if(many)
		for(ObjectInst *inst : gQueued){
			if(GetSampElementKind(inst) != SAMP_ELEMENT_GANGZONE || drawsIcon(inst, many))
				continue;
			const ObjectPawnProperties *p = GetObjectPawnProperties(inst);
			float hx = p->zoneSizeX > 0.0f ? p->zoneSizeX*0.5f : 25.0f;
			float hy = p->zoneSizeY > 0.0f ? p->zoneSizeY*0.5f : 25.0f;
			rw::V3d c = inst->m_translation;
			rw::V3d corners[4] = { { c.x - hx, c.y - hy, c.z }, { c.x + hx, c.y - hy, c.z },
			                       { c.x + hx, c.y + hy, c.z }, { c.x - hx, c.y + hy, c.z } };
			quad(corners, rw::RGBA{ (uint8)(inst->m_id & 0xFF), (uint8)(inst->m_id >> 8 & 0xFF),
				(uint8)(inst->m_id >> 16 & 0xFF), 255 });
		}
	flush();
	// The icons on top of everything, so one seen through a wall still picks.
	setState(false, false);
	rw::SetRenderStatePtr(rw::TEXTURERASTER, gMask ? gMask->raster : nil);
	for(ObjectInst *inst : gQueued){
		if(!drawsIcon(inst, many))
			continue;
		Billboard b;
		if(billboardFor(inst, &b, 1.0f))
			billboardQuad(b, rw::RGBA{ (uint8)(inst->m_id & 0xFF), (uint8)(inst->m_id >> 8 & 0xFF),
				(uint8)(inst->m_id >> 16 & 0xFF), 255 });
	}
	flush();
	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
	rw::SetRenderState(rw::ZTESTENABLE, ztest);
	rw::SetRenderState(rw::ZWRITEENABLE, zwrite);
	rw::SetRenderState(rw::FOGENABLE, fog);
	rw::SetRenderState(rw::CULLMODE, cull);
	rw::SetRenderState(rw::ALPHATESTREF, aref);
}

float
GetElementPlacementBaseOffset(int kind, int objectId)
{
	return ElementKindUsesIcon(kind) ? 0.0f : GetPlacementBaseOffset(objectId);
}

void*
GetElementIconTexture(int kind)
{
	int icon = iconOf(kind);
	if(icon < 0)
		return nil;
	makeArt();
	return gIcons[icon];
}

void
RenderElementIconGhost(int kind, rw::V3d position)
{
	int icon = iconOf(kind);
	Billboard b;
	if(icon < 0 || !billboardAt(position, &b, 1.0f))
		return;
	makeArt();
	int32 ztest = rw::GetRenderState(rw::ZTESTENABLE), zwrite = rw::GetRenderState(rw::ZWRITEENABLE);
	int32 fog = rw::GetRenderState(rw::FOGENABLE), cull = rw::GetRenderState(rw::CULLMODE);
	setState(false, true);
	rw::SetRenderStatePtr(rw::TEXTURERASTER, gIcons[icon] ? gIcons[icon]->raster : nil);
	billboardQuad(b, rw::RGBA{ 255, 255, 255, 170 });
	flush();
	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
	rw::SetRenderState(rw::ZTESTENABLE, ztest);
	rw::SetRenderState(rw::ZWRITEENABLE, zwrite);
	rw::SetRenderState(rw::FOGENABLE, fog);
	rw::SetRenderState(rw::CULLMODE, cull);
}

void
RenderElementIconBillboard(int kind, rw::V3d position, rw::V3d viewDir, float worldSize)
{
	int icon = iconOf(kind);
	if(icon < 0)
		return;
	makeArt();
	rw::V3d zUp = { 0.0f, 0.0f, 1.0f };
	rw::V3d right = rw::normalize(rw::cross(viewDir, zUp));
	rw::V3d up = rw::normalize(rw::cross(right, viewDir));
	Billboard b = { position, rw::scale(right, worldSize*0.5f), rw::scale(up, worldSize*0.5f) };
	int32 zwrite = rw::GetRenderState(rw::ZWRITEENABLE), fog = rw::GetRenderState(rw::FOGENABLE);
	int32 cull = rw::GetRenderState(rw::CULLMODE);
	setState(true, true);
	rw::SetRenderStatePtr(rw::TEXTURERASTER, gIcons[icon] ? gIcons[icon]->raster : nil);
	billboardQuad(b, rw::RGBA{ 255, 255, 255, 255 });
	flush();
	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
	rw::SetRenderState(rw::ZWRITEENABLE, zwrite);
	rw::SetRenderState(rw::FOGENABLE, fog);
	rw::SetRenderState(rw::CULLMODE, cull);
}
