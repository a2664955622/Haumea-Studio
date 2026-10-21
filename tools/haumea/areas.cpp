#include "haumea.h"
#include "areas.h"
#include "shortcuts.h"

#include <math.h>
#include <string.h>

// Dynamic areas (streamer only): drawn as their shape around the element's
// position; the selected one has handles to size it, and a polygon takes
// corners clicked in the world.

namespace {

const rw::RGBA areaColour = { 60, 200, 220, 230 };
const rw::RGBA selectedColour = { 255, 220, 90, 255 };
const rw::RGBA handleColour = { 255, 255, 255, 255 };

void ring(const rw::V3d &c, float r, float z, rw::RGBA col)
{
	const int segments = 48;
	for(int k = 0; k < segments; k++){
		float a0 = (float)k / segments * 2.0f * (float)M_PI;
		float a1 = (float)(k + 1) / segments * 2.0f * (float)M_PI;
		RenderLine({ c.x + r * cosf(a0), c.y + r * sinf(a0), z }, { c.x + r * cosf(a1), c.y + r * sinf(a1), z }, col, col);
	}
}

void box(float minX, float minY, float minZ, float maxX, float maxY, float maxZ, rw::RGBA col)
{
	float xs[2] = { minX, maxX }, ys[2] = { minY, maxY }, zs[2] = { minZ, maxZ };
	for(int z = 0; z < 2; z++){
		RenderLine({ minX, minY, zs[z] }, { maxX, minY, zs[z] }, col, col);
		RenderLine({ maxX, minY, zs[z] }, { maxX, maxY, zs[z] }, col, col);
		RenderLine({ maxX, maxY, zs[z] }, { minX, maxY, zs[z] }, col, col);
		RenderLine({ minX, maxY, zs[z] }, { minX, minY, zs[z] }, col, col);
	}
	if(minZ != maxZ)
		for(int x = 0; x < 2; x++)
			for(int y = 0; y < 2; y++)
				RenderLine({ xs[x], ys[y], minZ }, { xs[x], ys[y], maxZ }, col, col);
}

// The handles of an area: where they are and what dragging them changes.
enum HandleKind { HANDLE_RADIUS, HANDLE_SIZE_X, HANDLE_SIZE_Y, HANDLE_HEIGHT, HANDLE_POINT };
struct Handle { HandleKind kind; rw::V3d pos; int point; };

void handlesOf(const ObjectInst *area, std::vector<Handle> &out)
{
	out.clear();
	const ObjectPawnProperties *p = GetObjectPawnProperties(area);
	const rw::V3d &c = area->m_translation;
	switch(p->areaShape){
	case SAMP_AREA_CIRCLE:
	case SAMP_AREA_SPHERE:
		out.push_back({ HANDLE_RADIUS, { c.x + p->areaRadius, c.y, c.z }, 0 });
		break;
	case SAMP_AREA_CYLINDER:
		out.push_back({ HANDLE_RADIUS, { c.x + p->areaRadius, c.y, c.z }, 0 });
		out.push_back({ HANDLE_HEIGHT, { c.x, c.y, c.z + p->areaHeight }, 0 });
		break;
	case SAMP_AREA_RECTANGLE:
		out.push_back({ HANDLE_SIZE_X, { c.x + p->areaSize.x * 0.5f, c.y, c.z }, 0 });
		out.push_back({ HANDLE_SIZE_Y, { c.x, c.y + p->areaSize.y * 0.5f, c.z }, 0 });
		break;
	case SAMP_AREA_CUBOID:
	case SAMP_AREA_CUBE:
		out.push_back({ HANDLE_SIZE_X, { c.x + p->areaSize.x * 0.5f, c.y, c.z }, 0 });
		out.push_back({ HANDLE_SIZE_Y, { c.x, c.y + p->areaSize.y * 0.5f, c.z }, 0 });
		out.push_back({ HANDLE_HEIGHT, { c.x, c.y, c.z + p->areaSize.z }, 0 });
		break;
	case SAMP_AREA_POLYGON:
		for(int i = 0; i < p->areaPointCount; i++)
			out.push_back({ HANDLE_POINT, { c.x + p->areaPoints[i][0], c.y + p->areaPoints[i][1], c.z }, i });
		if(p->areaHeight > 0.0f)
			out.push_back({ HANDLE_HEIGHT, { c.x, c.y, c.z + p->areaHeight }, 0 });
		break;
	}
}

bool toScreen(const rw::V3d &p, ImVec2 *out)
{
	rw::V3d screen;
	float w, h;
	if(!Sprite::CalcScreenCoors(p, &screen, &w, &h, false))
		return false;
	*out = ImVec2(screen.x, screen.y);
	return true;
}

// Where the mouse ray meets the horizontal plane at z, or the vertical line
// through a point (for heights).
bool mouseOnPlane(float z, rw::V3d *out)
{
	rw::V3d dir = normalize(TheCamera.m_mouseDir);
	if(fabsf(dir.z) < 0.0001f)
		return false;
	float t = (z - TheCamera.m_position.z) / dir.z;
	if(t <= 0.0f || t > 20000.0f)
		return false;
	*out = add(TheCamera.m_position, scale(dir, t));
	return true;
}

float mouseHeightAt(const rw::V3d &base)
{
	// The point on the vertical through base nearest the mouse ray.
	rw::V3d d = normalize(TheCamera.m_mouseDir), o = TheCamera.m_position;
	rw::V3d w = sub(o, base);
	float b = d.z, dd = dot(d, w);
	float denom = 1.0f - b * b;
	if(fabsf(denom) < 0.0001f)
		return base.z;
	float s = (w.z - b * dd) / denom;	// along the vertical (0, 0, 1)
	return base.z + s;
}

ObjectInst *gDragArea, *gPickArea;
int gDragHandle = -1;

ObjectInst *selectedArea(void)
{
	ObjectInst *only = nil;
	for(CPtrNode *p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted)
			continue;
		if(only)
			return nil;	// handles only with one area selected
		only = inst;
	}
	return only && GetSampElementKind(only) == SAMP_ELEMENT_AREA ? only : nil;
}

}	// namespace

bool
IsPointInDynamicArea(const ObjectInst *area, const rw::V3d &q)
{
	const ObjectPawnProperties *p = GetObjectPawnProperties(area);
	const rw::V3d &c = area->m_translation;
	float dx = q.x - c.x, dy = q.y - c.y;
	switch(p->areaShape){
	case SAMP_AREA_CIRCLE:
		return dx * dx + dy * dy <= p->areaRadius * p->areaRadius;
	case SAMP_AREA_CYLINDER:
		return dx * dx + dy * dy <= p->areaRadius * p->areaRadius && q.z >= c.z && q.z <= c.z + p->areaHeight;
	case SAMP_AREA_SPHERE:
		return dx * dx + dy * dy + (q.z - c.z) * (q.z - c.z) <= p->areaRadius * p->areaRadius;
	case SAMP_AREA_RECTANGLE:
		return fabsf(dx) <= p->areaSize.x * 0.5f && fabsf(dy) <= p->areaSize.y * 0.5f;
	case SAMP_AREA_CUBOID:
	case SAMP_AREA_CUBE:
		return fabsf(dx) <= p->areaSize.x * 0.5f && fabsf(dy) <= p->areaSize.y * 0.5f &&
			q.z >= c.z && q.z <= c.z + p->areaSize.z;
	case SAMP_AREA_POLYGON: {
		if(p->areaPointCount < 3)
			return false;
		if(p->areaHeight > 0.0f && (q.z < c.z || q.z > c.z + p->areaHeight))
			return false;
		bool inside = false;
		for(int i = 0, j = p->areaPointCount - 1; i < p->areaPointCount; j = i++){
			float xi = p->areaPoints[i][0], yi = p->areaPoints[i][1];
			float xj = p->areaPoints[j][0], yj = p->areaPoints[j][1];
			if((yi > dy) != (yj > dy) && dx < (xj - xi) * (dy - yi) / (yj - yi) + xi)
				inside = !inside;
		}
		return inside;
	}
	}
	return false;
}

int
CountItemsInArea(const char *areaName)
{
	if(areaName == nil || areaName[0] == '\0')
		return 0;
	int n = 0;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted && !inst->m_isVehicle && strcmp(GetObjectPawnProperties(inst)->areaName, areaName) == 0)
			n++;
	}
	return n;
}

void
DrawDynamicAreaOverlay(const ObjectInst *area, bool selected)
{
	const ObjectPawnProperties *p = GetObjectPawnProperties(area);
	const rw::V3d &c = area->m_translation;
	rw::RGBA col = selected ? selectedColour : areaColour;
	float r = p->areaRadius, hx = p->areaSize.x * 0.5f, hy = p->areaSize.y * 0.5f;
	switch(p->areaShape){
	case SAMP_AREA_CIRCLE:
		ring(c, r, c.z, col);
		break;
	case SAMP_AREA_CYLINDER:
		ring(c, r, c.z, col);
		ring(c, r, c.z + p->areaHeight, col);
		for(int k = 0; k < 4; k++){
			float a = k * (float)M_PI * 0.5f;
			RenderLine({ c.x + r * cosf(a), c.y + r * sinf(a), c.z }, { c.x + r * cosf(a), c.y + r * sinf(a), c.z + p->areaHeight }, col, col);
		}
		break;
	case SAMP_AREA_SPHERE: {
		ring(c, r, c.z, col);
		for(int k = 1; k < 4; k++){
			float z = r * k / 4.0f, rr = sqrtf(max(0.0f, r * r - z * z));
			ring(c, rr, c.z + z, col);
			ring(c, rr, c.z - z, col);
		}
		break;
	}
	case SAMP_AREA_RECTANGLE:
		box(c.x - hx, c.y - hy, c.z, c.x + hx, c.y + hy, c.z, col);
		break;
	case SAMP_AREA_CUBOID:
	case SAMP_AREA_CUBE:
		box(c.x - hx, c.y - hy, c.z, c.x + hx, c.y + hy, c.z + p->areaSize.z, col);
		break;
	case SAMP_AREA_POLYGON: {
		float top = p->areaHeight > 0.0f ? c.z + p->areaHeight : c.z;
		for(int i = 0; i < p->areaPointCount; i++){
			int j = (i + 1) % p->areaPointCount;
			rw::V3d a = { c.x + p->areaPoints[i][0], c.y + p->areaPoints[i][1], c.z };
			rw::V3d b = { c.x + p->areaPoints[j][0], c.y + p->areaPoints[j][1], c.z };
			if(p->areaPointCount > 1)
				RenderLine(a, b, col, col);
			if(top != c.z){
				RenderLine({ a.x, a.y, top }, { b.x, b.y, top }, col, col);
				RenderLine(a, { a.x, a.y, top }, col, col);
			}
		}
		break;
	}
	}
	if(!selected)
		return;
	std::vector<Handle> handles;
	handlesOf(area, handles);
	for(const Handle &h : handles){
		ImVec2 s;
		if(toScreen(h.pos, &s)){
			ImDrawList *dl = ImGui::GetBackgroundDrawList();
			dl->AddCircleFilled(s, 6.0f, IM_COL32(handleColour.red, handleColour.green, handleColour.blue, 255));
			dl->AddCircle(s, 6.0f, IM_COL32(0, 0, 0, 255), 0, 1.5f);
		}
	}
}

void
StartAreaPointPicking(ObjectInst *area)
{
	gPickArea = area;
}

bool
IsAreaPointPicking(const ObjectInst *area)
{
	return gPickArea && gPickArea == area;
}

void
StopAreaPointPicking(void)
{
	gPickArea = nil;
}

bool
DynamicAreaHandleViewport(void)
{
	// Adding polygon corners: click adds one at the clicked surface.
	if(gPickArea){
		if(gPickArea->m_isDeleted || GetSampElementKind(gPickArea) != SAMP_ELEMENT_AREA){
			gPickArea = nil;
			return false;
		}
		if(CPad::IsMButtonClicked(1)){
			Ray ray;
			ray.start = TheCamera.m_position;
			ray.dir = normalize(TheCamera.m_mouseDir);
			rw::V3d hit;
			if(GetVisibleInstUnderRay(ray, &hit) == nil && !mouseOnPlane(gPickArea->m_translation.z, &hit))
				return true;
			ObjectPawnProperties properties = *GetObjectPawnProperties(gPickArea);
			if(properties.areaPointCount < PAWN_AREA_POINTS){
				properties.areaShape = SAMP_AREA_POLYGON;
				properties.areaPoints[properties.areaPointCount][0] = hit.x - gPickArea->m_translation.x;
				properties.areaPoints[properties.areaPointCount][1] = hit.y - gPickArea->m_translation.y;
				properties.areaPointCount++;
				SetObjectPawnProperties(gPickArea, properties);
			}else
				Toast(TOAST_SELECTION, "An area has at most 32 corners");
			// A click without Shift adds one; Shift+click keeps adding.
			if(!CPad::IsShiftDown())
				gPickArea = nil;
			return true;
		}
		if(CPad::IsMButtonClicked(3) || CPad::IsMButtonClicked(2) || ShortcutPressed(SC_CANCEL)){
			gPickArea = nil;
			return true;
		}
		return true;
	}

	// Dragging a handle of the one selected area.
	ObjectInst *area = selectedArea();
	if(gDragArea && (area != gDragArea || !CPad::IsMButtonDown(1))){
		gDragArea = nil;
		gDragHandle = -1;
		UndoEndGroup();
		return true;
	}
	if(area == nil)
		return false;
	std::vector<Handle> handles;
	handlesOf(area, handles);
	if(gDragArea == nil){
		if(!CPad::IsMButtonJustDown(1))
			return false;
		ImVec2 mouse = ImGui::GetIO().MousePos;
		for(size_t i = 0; i < handles.size(); i++){
			ImVec2 s;
			if(toScreen(handles[i].pos, &s) && fabsf(s.x - mouse.x) <= 8.0f && fabsf(s.y - mouse.y) <= 8.0f){
				gDragArea = area;
				gDragHandle = (int)i;
				UndoBeginGroup();
				return true;
			}
		}
		return false;
	}
	if(gDragHandle < 0 || gDragHandle >= (int)handles.size())
		return true;
	const Handle &h = handles[gDragHandle];
	ObjectPawnProperties properties = *GetObjectPawnProperties(area);
	const rw::V3d &c = area->m_translation;
	rw::V3d at;
	switch(h.kind){
	case HANDLE_RADIUS:
		if(mouseOnPlane(c.z, &at))
			properties.areaRadius = max(0.5f, sqrtf((at.x - c.x) * (at.x - c.x) + (at.y - c.y) * (at.y - c.y)));
		break;
	case HANDLE_SIZE_X:
		if(mouseOnPlane(c.z, &at))
			properties.areaSize.x = max(0.5f, fabsf(at.x - c.x) * 2.0f);
		break;
	case HANDLE_SIZE_Y:
		if(mouseOnPlane(c.z, &at))
			properties.areaSize.y = max(0.5f, fabsf(at.y - c.y) * 2.0f);
		break;
	case HANDLE_HEIGHT: {
		float top = mouseHeightAt(c);
		float height = max(0.5f, top - c.z);
		if(properties.areaShape == SAMP_AREA_CUBOID || properties.areaShape == SAMP_AREA_CUBE)
			properties.areaSize.z = height;
		else
			properties.areaHeight = height;
		break;
	}
	case HANDLE_POINT:
		if(mouseOnPlane(c.z, &at)){
			properties.areaPoints[h.point][0] = at.x - c.x;
			properties.areaPoints[h.point][1] = at.y - c.y;
		}
		break;
	}
	SetObjectPawnProperties(area, properties);
	return true;
}
