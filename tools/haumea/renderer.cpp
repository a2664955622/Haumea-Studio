#define WITH_D3D
#include "haumea.h"
#include "editoricons.h"
#include "playerview.h"
#include "templates.h"
#include <algorithm>
#include <vector>
#include <unordered_map>

struct InstDist
{
	ObjectInst *inst;
	union { float dist, sort; };
};

static std::vector<ObjectInst*> visibleInsts;
static std::vector<InstDist> sortedInsts;
static std::vector<InstDist> lodList;

static uint16 currentScanCode;

static rw::ObjPipeline *colourCodePipe;

static ObjectInst*
findClosestRayHitInVisibleList(const std::vector<ObjectInst*> &insts, const Ray &ray,
	rw::V3d *bestHit, float *bestT)
{
	ObjectInst *bestInst = nil;

	for(ObjectInst *inst : insts){
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		if(obj == nil || obj->m_colModel == nil)
			continue;

		rw::V3d hitPos;
		if(!IntersectRayColModel(ray, inst, &hitPos))
			continue;

		float t = dot(sub(hitPos, ray.start), ray.dir);
		if(t < 0.0f || t >= *bestT)
			continue;

		*bestT = t;
		*bestHit = hitPos;
		bestInst = inst;
	}

	return bestInst;
}

static ObjectInst*
findClosestRayHitInSortedList(const std::vector<InstDist> &insts, const Ray &ray,
	rw::V3d *bestHit, float *bestT)
{
	ObjectInst *bestInst = nil;

	for(const InstDist &entry : insts){
		ObjectInst *inst = entry.inst;
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		if(obj == nil || obj->m_colModel == nil)
			continue;

		rw::V3d hitPos;
		if(!IntersectRayColModel(ray, inst, &hitPos))
			continue;

		float t = dot(sub(hitPos, ray.start), ray.dir);
		if(t < 0.0f || t >= *bestT)
			continue;

		*bestT = t;
		*bestHit = hitPos;
		bestInst = inst;
	}

	return bestInst;
}

enum Visibility
{
	VIS_INVISIBLE,
	VIS_VISIBLE,
	VIS_CULLED,
	VIS_STREAMME
};

static void
AddToRenderList(ObjectInst *inst, float dist)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj == nil) return;
	// Labels, sounds, map icons, gang zones and path points are drawn as
	// editor icons, never as their stand-in model (editoricons.cpp).
	if(InstUsesEditorIcon(inst)){
		QueueElementIcon(inst);
		return;
	}
	if(inst->m_rwObject == nil) return;

	inst->PreRender();
	if(obj->m_drawLast){
		InstDist e;
		e.inst = inst;
		e.dist = dist;
		sortedInsts.push_back(e);
	}else
		visibleInsts.push_back(inst);
}

// This is a list of potentially rendered LODs
static void
AddToLodRenderList(ObjectInst *inst, float dist)
{
	// Highlighted objects are always visible
	if(!inst->m_selected && inst->m_highlight == 0){
		InstDist e;
		e.inst = inst;
		e.dist = dist;
		lodList.push_back(e);
	}else
		AddToRenderList(inst, dist);
}

// Don't assume there are LODs, just draw if below draw distance
static int
SetupVisibilitySimple(ObjectInst *inst, float *distout)
{
	ObjectDef *obj, *hdobj;
	float camdist;
	rw::Atomic *atm, *instatm;
	rw::Clump *instclump;

	obj = GetObjectDef(inst->m_objectId);
	if(obj == nil) return VIS_INVISIBLE;

	// Highlighted objects are always visible
	if(!inst->m_selected && inst->m_highlight == 0){
		if(!gNoAreaCull && inst->m_area != currentArea && inst->m_area != 13)
			return VIS_INVISIBLE;

		hdobj = inst->m_isBigBuilding ? obj->m_relatedModel : nil;

		if(obj->m_isHidden)
			return VIS_INVISIBLE;

		camdist = TheCamera.distanceTo(inst->m_translation);
		if(camdist >= obj->GetLargestDrawDist()*TheCamera.m_LODmult)
			return VIS_INVISIBLE;

		if(!gNoTimeCull && obj->m_isTimed && !IsHourInRange(obj->m_timeOn, obj->m_timeOff))
			return VIS_INVISIBLE;
	}else
		camdist = TheCamera.distanceTo(inst->m_translation);

	if(!obj->IsLoaded())
		return VIS_STREAMME;

	if(!inst->IsOnScreen())
		return VIS_CULLED;

	instatm = nil;
	instclump = nil;
	if(inst->m_rwObject == nil)
		if(inst->CreateRwObject() == nil)
			return VIS_INVISIBLE;	// this shouldn't happen

	if(obj->m_type == ObjectDef::ATOMIC){
		atm = obj->GetAtomicForDist(camdist);
		if(atm == nil)	// this shouldn't happen but it does (in xbox III)
			return VIS_INVISIBLE;
		instatm = (rw::Atomic*)inst->m_rwObject;
		if(instatm->geometry != atm->geometry)
			inst->SetBaseGeometry(atm->geometry);
	}

	return VIS_VISIBLE;
}

static int
SetupVisibilityIII(ObjectInst *inst, float *distout)
{
	ObjectDef *obj, *hdobj;
	float camdist;
	rw::Atomic *atm, *instatm;
	rw::Clump *instclump;

	obj = GetObjectDef(inst->m_objectId);
	if(obj == nil) return VIS_INVISIBLE;

	// Highlighted objects are always visible
	if(!inst->m_selected && inst->m_highlight == 0){
		if(!gNoAreaCull && inst->m_area != currentArea && inst->m_area != 13)
			return VIS_INVISIBLE;

		hdobj = inst->m_isBigBuilding ? obj->m_relatedModel : nil;

		if(obj->m_isHidden)
			return VIS_INVISIBLE;

		camdist = TheCamera.distanceTo(inst->m_translation);
		if(camdist >= obj->GetLargestDrawDist()*TheCamera.m_LODmult)
			return VIS_INVISIBLE;
		if(camdist < obj->m_minDrawDist*TheCamera.m_LODmult)
			if(hdobj == nil || hdobj->IsLoaded())
				return VIS_INVISIBLE;

		if(!gNoTimeCull && obj->m_isTimed && !IsHourInRange(obj->m_timeOn, obj->m_timeOff))
			return VIS_INVISIBLE;
	}else
		camdist = TheCamera.distanceTo(inst->m_translation);

	if(!obj->IsLoaded())
		return VIS_STREAMME;

	if(!inst->IsOnScreen())
		return VIS_CULLED;

	instatm = nil;
	instclump = nil;
	if(inst->m_rwObject == nil)
		if(inst->CreateRwObject() == nil)
			return VIS_INVISIBLE;	// this shouldn't happen

	if(obj->m_type == ObjectDef::ATOMIC){
		atm = obj->GetAtomicForDist(camdist);
		if(atm == nil)	// this shouldn't happen but it does (in xbox III)
			return VIS_INVISIBLE;
		instatm = (rw::Atomic*)inst->m_rwObject;
		if(instatm->geometry != atm->geometry)
			inst->SetBaseGeometry(atm->geometry);
	}

	return VIS_VISIBLE;
}

static int
SetupVisibilitySA(ObjectInst *inst, float camdist)
{
	ObjectDef *obj;
	ObjectInst *lodinst;
	rw::Atomic *atm, *instatm;
	rw::Clump *instclump;

	obj = GetObjectDef(inst->m_objectId);
	if(obj == nil) return VIS_INVISIBLE;
	lodinst = inst->m_lod;

	// Highlighted objects are always visible, and the player view decides
	// itself for the map's elements.
	if(!inst->m_selected && inst->m_highlight == 0 && !PlayerViewShows(inst)){
		if(!gNoAreaCull && inst->m_area != currentArea && inst->m_area != 13)
			return VIS_INVISIBLE;

		if(obj->m_isHidden)
			return VIS_INVISIBLE;

		if(camdist >= obj->GetLargestDrawDist()*TheCamera.m_LODmult)
			return VIS_INVISIBLE;

		if(!gNoTimeCull && obj->m_isTimed && !IsHourInRange(obj->m_timeOn, obj->m_timeOff))
			return VIS_INVISIBLE;
	}else
		camdist = TheCamera.distanceTo(inst->m_translation);

	if(!obj->IsLoaded())
		return VIS_STREAMME;

	instatm = nil;
	instclump = nil;
	if(inst->m_rwObject == nil)
		if(inst->CreateRwObject() == nil)
			return VIS_INVISIBLE;	// this shouldn't happen

	if(obj->m_type == ObjectDef::ATOMIC){
		atm = obj->GetAtomicForDist(camdist);
		if(atm == nil)	// this shouldn't happen but it does (in xbox III)
			return VIS_INVISIBLE;
		instatm = (rw::Atomic*)inst->m_rwObject;
		if(instatm->geometry != atm->geometry)
			inst->SetBaseGeometry(atm->geometry);
	}

	int ret = VIS_VISIBLE;
	if(!inst->IsOnScreen())
		ret = VIS_CULLED;	// we want to render but we're culled

	if(lodinst == nil)
		return ret;

	// Only visible HD children should suppress the fallback LOD.
	if(ret == VIS_VISIBLE)
		lodinst->m_numChildrenRendered++;
	if(lodinst->m_numChildren > 1){
		AddToLodRenderList(inst, camdist);
		return VIS_INVISIBLE;
	}
	return ret;
}

static int
SetupBigBuildingVisibilitySA(ObjectInst *inst, float *distout)
{
	int v;
	ObjectInst *lodinst;
	float camdist;

	lodinst = inst->m_lod;

	camdist = TheCamera.distanceTo(inst->m_translation);
	*distout = camdist;
	if(inst->m_numChildrenRendered > 0){
		// There is something to render here...

		// tell LOD parent that we (or children) want to be rendered
		if(lodinst)
			lodinst->m_numChildrenRendered++;

		if(inst->m_numChildren > 1)
			// might still have to render this LOD,
			// if not all children are rendered
			// We actually don't test for frustum culling here...TODO?
			AddToLodRenderList(inst, camdist);
		else
			// otherwise...???
			inst->m_numChildrenRendered = 0;
		return VIS_INVISIBLE;
	}else{
		// No child wants to be rendered (yet?)
		// but maybe this LOD...

		v = SetupVisibilitySA(inst, camdist);

		if(v == VIS_VISIBLE && inst->m_numChildren > 1){
			// we're visible AND have multiple children,
			// so we might have to render
			AddToLodRenderList(inst, camdist);
			return VIS_INVISIBLE;
		}
		// But what if we're a super LOD and our child
		// just hasn't been visited yet?
		// Maybe that isn't allowed?
		// With v == 1 and m_numChildren == 1 how is this correct?
		return v;
	}
}

static int
SetupHdVisibilitySA(ObjectInst *inst, float *distout)
{
	float camdist = TheCamera.distanceTo(inst->m_translation);
	*distout = camdist;
	return SetupVisibilitySA(inst, camdist);
}

static void
ProcessLodList(void)
{
	int i;
	ObjectInst *inst;//, *superlod;

	// I don't understand SA's code of this function...
	// So this one doesn't make perfect sense either.

	int numLods = (int)lodList.size();
	for(i = 0; i < numLods; i++){
		inst = lodList[i].inst;
		if(inst == nil) continue;
		if(inst->m_numChildren > 0 && inst->m_numChildrenRendered == inst->m_numChildren){
			// all children can be drawn, so don't draw the LOD
			inst->m_numChildrenRendered = 0;
			lodList[i].inst = nil;
		}else{
			// not all children are drawn

			// We don't have the RW object yet, in case we didn't call the visbility function
			if(inst->m_rwObject == nil)
				inst->CreateRwObject();

			//superlod = inst->m_lod;
			//if(superlod){
			//}
		}
//		printf("%d %s %d %d\n", i, GetObjectDef(inst->m_objectId)->m_name,
//			inst->m_numChildren, inst->m_numChildrenRendered);
	}

	// I don't get this, do we really want to hide superlods with this?
//	for(i = 0; i < numLods; i++){
//		inst = lodList[i];
//		if(inst == nil) continue;
//		// what's this exactly?
//		if(inst->m_numChildrenRendered > 0){
//			inst->m_numChildrenRendered = 0;
//			lodList[i] = 0;
//		}
//	}

	for(i = 0; i < numLods; i++){
		inst = lodList[i].inst;
		if(inst == nil) continue;
		AddToRenderList(inst, lodList[i].dist);
	}
}

static rw::RGBA highlightColor;
static uint8 highlightAlpha = 128;

static void
BuildGhostMatrix(rw::Matrix *matrix, const rw::Quat &rotation, const rw::V3d &translation)
{
	if(isSA() && fabs(rotation.x) <= 0.05f && fabs(rotation.y) <= 0.05f){
		float w = rotation.w;
		if(w < -1.0f) w = -1.0f;
		if(w > 1.0f) w = 1.0f;
		float heading = acosf(w) * (rotation.z < 0.0f ? 2.0f : -2.0f);
		float s = sinf(heading);
		float c = cosf(heading);

		matrix->setIdentity();
		matrix->right = { c, s, 0.0f };
		matrix->up = { -s, c, 0.0f };
		matrix->at = { 0.0f, 0.0f, 1.0f };
		matrix->flags = rw::Matrix::TYPEORTHONORMAL;
	}else
		matrix->rotate(conj(rotation), rw::COMBINEREPLACE);
	matrix->translate(&translation, rw::COMBINEPOSTCONCAT);
}

static void
SetClumpRenderCB(rw::Clump *clump, rw::Atomic::RenderCB cb)
{
	if(clump == nil)
		return;
	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		atomic->setRenderCB(cb);
	}
}

void
RenderPlacementGhost(int objectId, rw::V3d position, rw::Quat rotation, rw::RGBA color)
{
	if(colourCodePipe == nil || objectId < 0)
		return;

	ObjectDef *obj = GetObjectDef(objectId);
	if(obj == nil)
		return;
	if(!obj->IsLoaded()){
		RequestObject(objectId);
		return;
	}

	static int ghostObjectId = -1;
	static rw::Atomic *ghostAtomic = nil;
	static rw::Clump *ghostClump = nil;

	if(ghostObjectId != objectId){
		if(ghostAtomic){
			ghostAtomic->getFrame()->destroy();
			ghostAtomic->destroy();
			ghostAtomic = nil;
		}
		if(ghostClump){
			ghostClump->destroy();
			ghostClump = nil;
		}
		ghostObjectId = objectId;

		if(obj->m_type == ObjectDef::ATOMIC && obj->m_atomics[0]){
			ghostAtomic = obj->m_atomics[0]->clone();
			if(ghostAtomic){
				rw::Frame *f = rw::Frame::create();
				if(f)
					ghostAtomic->setFrame(f);
				else{
					ghostAtomic->destroy();
					ghostAtomic = nil;
				}
			}
		}else if(obj->m_type == ObjectDef::CLUMP && obj->m_clump){
			ghostClump = obj->m_clump->clone();
			SetClumpRenderCB(ghostClump, myRenderCB);
		}
		if(ghostAtomic)
			ghostAtomic->setRenderCB(myRenderCB);
	}

	if(ghostAtomic == nil && ghostClump == nil)
		return;

	rw::Matrix matrix;
	BuildGhostMatrix(&matrix, rotation, position);

	int32 zwrite = GetRenderState(rw::ZWRITEENABLE);
	int32 fog = rw::GetRenderState(rw::FOGENABLE);
	int32 vertexAlpha = GetRenderState(rw::VERTEXALPHA);
	int32 aref = GetRenderState(rw::ALPHATESTREF);
	uint32 oldCode = gta::renderColourCoded;
	rw::RGBA oldColor = gta::colourCode;

	SetRenderState(rw::ZWRITEENABLE, 0);
	SetRenderState(rw::FOGENABLE, 0);
	SetRenderState(rw::VERTEXALPHA, 1);
	SetRenderState(rw::ALPHATESTREF, 0);
	gta::renderColourCoded = 1;
	gta::colourCode = color;

	if(ghostAtomic){
		ghostAtomic->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
		ghostAtomic->render();
	}else if(ghostClump){
		ghostClump->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
		ghostClump->render();
	}

	gta::colourCode = oldColor;
	gta::renderColourCoded = oldCode;
	SetRenderState(rw::ZWRITEENABLE, zwrite);
	SetRenderState(rw::FOGENABLE, fog);
	SetRenderState(rw::VERTEXALPHA, vertexAlpha);
	SetRenderState(rw::ALPHATESTREF, aref);
}

void
myRenderCB(rw::Atomic *atomic)
{
	if(gta::renderColourCoded)
		colourCodePipe->render(atomic);
	else if(highlightColor.red || highlightColor.green || highlightColor.blue){
		atomic->getPipeline()->render(atomic);
		gta::colourCode = highlightColor;
		gta::colourCode.alpha = highlightAlpha;
		int32 zwrite, fog, aref;
		zwrite = GetRenderState(rw::ZWRITEENABLE);
		fog = rw::GetRenderState(rw::FOGENABLE);
		aref = rw::GetRenderState(rw::ALPHATESTREF);
		SetRenderState(rw::ZWRITEENABLE, 0);
		SetRenderState(rw::FOGENABLE, 0);
		SetRenderState(rw::ALPHATESTREF, 10);
		colourCodePipe->render(atomic);
		SetRenderState(rw::ZWRITEENABLE, zwrite);
		SetRenderState(rw::FOGENABLE, fog);
		SetRenderState(rw::ALPHATESTREF, aref);
	}else
		atomic->getPipeline()->render(atomic);
}

static void
RenderInst(ObjectInst *inst)
{
	static rw::RGBA black = { 0, 0, 0, 255 };
	static rw::RGBA red = { 255, 0, 0, 255 };
	static rw::RGBA green = { 0, 255, 0, 255 };
	static rw::RGBA blue = { 0, 0, 255, 255 };
	static rw::RGBA highlightCols[] = { black, green, red, blue };
	ObjectDef *obj;

//	if(!inst->m_isUnimportant && !inst->m_isUnderWater && !inst->m_isTunnel && !inst->m_isTunnelTransition)
//		return;
	obj = GetObjectDef(inst->m_objectId);
	// The render list is built before the UI runs, and a model swap there
	// (a skin or pickup model change) replaces the RW object; when the new
	// model can't be built (a missing or broken DFF) there is none left.
	if(obj == nil || inst->m_rwObject == nil) return;
	if(InstUsesEditorIcon(inst))
		NoteElementProxyModelDraw();	// never: they are queued as icons instead
	obj->m_hasPreRendered = false;

	uint32 cull;
	if(obj->m_noBackfaceCulling){
		cull = GetRenderState(rw::CULLMODE);
		SetRenderState(rw::CULLMODE, rw::CULLNONE);
	}

	pDirect->setFlags(0);
	gta::colourCode.red = inst->m_id & 0xFF;
	gta::colourCode.green = inst->m_id>>8 & 0xFF;
	gta::colourCode.blue = inst->m_id>>16 & 0xFF;
	gta::colourCode.alpha = 255;

	if(inst->m_selected && inst->m_highlight < HIGHLIGHT_SELECTION)
		inst->m_highlight = HIGHLIGHT_SELECTION;
	highlightColor = highlightCols[inst->m_highlight];
	highlightAlpha = inst->m_highlight == HIGHLIGHT_SELECTION ?
		(uint8)(gSelectionHighlightOpacity * 255 / 100) : 128;

	if(obj->m_type == ObjectDef::ATOMIC)
		((rw::Atomic*)inst->m_rwObject)->render();
	else if(obj->m_type == ObjectDef::CLUMP)
		((rw::Clump*)inst->m_rwObject)->render();

	highlightColor = black;
	if(obj->m_noBackfaceCulling)
		SetRenderState(rw::CULLMODE, cull);
	pDirect->setFlags(rw::Light::LIGHTATOMICS);
}

static void
RenderTransparentInst(ObjectInst *inst)
{
	ObjectDef *obj;
	obj = GetObjectDef(inst->m_objectId);
	if(obj == nil) return;

	if(obj->m_noZwrite){
		SetRenderState(rw::ZWRITEENABLE, 0);
		SetRenderState(rw::ALPHATESTREF, 0);
	}else
		// The opaque pass uses SA's high cutout threshold (100), but sorted
		// draw-last objects also contain soft-alpha decals, grime and shadows.
		// Restore the platform's default threshold here so those pixels blend
		// instead of being discarded almost entirely.
		SetRenderState(rw::ALPHATESTREF, params.alphaRefDefault);

//	This is not handled that way by GTA, only on fading entities....
//	if(obj->m_additive)
//		SetRenderState(rw::DESTBLEND, rw::BLENDONE);

	RenderInst(inst);

	if(obj->m_noZwrite)
		SetRenderState(rw::ZWRITEENABLE, 1);
//	SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
}

static void
ProcessBuilding(ObjectInst *inst)
{
	float dist;
	if(inst->m_isBigBuilding || inst->m_scanCode == currentScanCode)
		return;
	inst->m_scanCode = currentScanCode;
	int v;
	if(gRenderOnlyHD)
		v = SetupVisibilitySimple(inst, &dist);
	else
		v = isSA() ? 
			SetupHdVisibilitySA(inst, &dist) :
			SetupVisibilityIII(inst, &dist);
	if(v == VIS_VISIBLE)
		AddToRenderList(inst, dist);
	else if(v == VIS_STREAMME)
		RequestObject(inst->m_objectId);
}

static void
ProcessBigBuilding(ObjectInst *inst)
{
	float dist;
	if(!inst->m_isBigBuilding || inst->m_scanCode == currentScanCode)
		return;
	inst->m_scanCode = currentScanCode;
	int v;
	if(gRenderOnlyLod)
		v = SetupVisibilitySimple(inst, &dist);
	else
		v = isSA() ? 
			SetupBigBuildingVisibilitySA(inst, &dist) :
			SetupVisibilityIII(inst, &dist);
	if(v == VIS_VISIBLE)
		AddToRenderList(inst, dist);
	else if(v == VIS_STREAMME)
		RequestObject(inst->m_objectId);
}

static void
ScanInstList(CPtrList *list, void (*f)(ObjectInst *inst))
{
	CPtrNode *p;
	for(p = list->first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted || inst->m_editorHidden || PlayerViewHides(inst))
			continue;
		f(inst);
	}
}

void
BuildRenderList(void)
{
	CPtrNode *p;
	ObjectInst *inst;

	currentScanCode++;
	visibleInsts.clear();
	sortedInsts.clear();
	lodList.clear();
	ClearElementIcons();

	rw::BBox frustBox = Scene.camera->frustumBoundBox;
//	printf("%f %f %f    %f %f %f\n",
//		frustBox.inf.x, frustBox.inf.y, frustBox.inf.z, 
//		frustBox.sup.x, frustBox.sup.y, frustBox.sup.z);
	// clip to world boundaries
	frustBox.inf.x = clamp(frustBox.inf.x, worldBounds.left, worldBounds.right-1.0f);
	frustBox.inf.y = clamp(frustBox.inf.y, worldBounds.bottom, worldBounds.top-1.0f);
	frustBox.sup.x = clamp(frustBox.sup.x, worldBounds.left, worldBounds.right-1.0f);
	frustBox.sup.y = clamp(frustBox.sup.y, worldBounds.bottom, worldBounds.top-1.0f);

	int x, xstart, xend;
	int y, ystart, yend;
	xstart = GetSectorIndexX(frustBox.inf.x);
	xend   = GetSectorIndexX(frustBox.sup.x);
	ystart = GetSectorIndexY(frustBox.inf.y);
	yend   = GetSectorIndexY(frustBox.sup.y);
//	log("x: %d - %d; y: %d - %d\n", xstart, xend, ystart, yend);

	if(!gRenderOnlyLod){
		for(x = xstart; x <= xend; x++)
			for(y = ystart; y <= yend; y++){
				Sector *s = GetSector(x, y);
				ScanInstList(&s->buildings, ProcessBuilding);
				ScanInstList(&s->buildings_overlap, ProcessBuilding);
			}
		ScanInstList(&outOfBoundsSector.buildings, ProcessBuilding);
	}
	if(!gRenderOnlyHD){
		for(x = xstart; x <= xend; x++)
			for(y = ystart; y <= yend; y++){
				Sector *s = GetSector(x, y);
				ScanInstList(&s->bigbuildings, ProcessBigBuilding);
				ScanInstList(&s->bigbuildings_overlap, ProcessBigBuilding);
			}
		ScanInstList(&outOfBoundsSector.bigbuildings, ProcessBigBuilding);
	}

//	ScanInstList(&instances, ProcessBuilding);
//	ScanInstList(&instances, ProcessBigBuilding);

	ProcessLodList();

	// Reset instances
	for(p = instances.first; p; p = p->next){
		inst = (ObjectInst*)p->item;
		inst->m_numChildrenRendered = 0;
		inst->m_highlight = HIGHLIGHT_NONE;
	}

//	log("%d visible instances\n", numVisibleInsts);
}

void
RenderOpaque(void)
{
	SetRenderState(rw::ALPHATESTREF, params.alphaRef);

	SetRenderState(rw::CULLMODE, gDoBackfaceCulling ? rw::CULLBACK : rw::CULLNONE);
	int i, numVisibleInsts = (int)visibleInsts.size();
	for(i = 0; i < numVisibleInsts; i++)
		RenderInst(visibleInsts[i]);
}

void
RenderTransparent(void)
{
	SetRenderState(rw::CULLMODE, gDoBackfaceCulling ? rw::CULLBACK : rw::CULLNONE);
	std::sort(sortedInsts.begin(), sortedInsts.end(), [](const InstDist &a, const InstDist &b) {
		return a.dist < b.dist;
	});
	for(int i = (int)sortedInsts.size()-1; i >= 0; i--)
		RenderTransparentInst(sortedInsts[i].inst);
	if(gta::renderColourCoded)
		RenderElementIconsColourCoded();
	else
		RenderElementIcons();
}

void
RenderEverything(void)
{
	RenderOpaque();
	RenderTransparent();
}

void
ForEachVisibleInst(void (*fn)(ObjectInst *inst, void *data), void *data)
{
	for(int i = 0; i < (int)visibleInsts.size(); i++)
		fn(visibleInsts[i], data);
	for(int i = 0; i < (int)sortedInsts.size(); i++)
		fn(sortedInsts[i].inst, data);
}

ObjectInst*
GetVisibleInstUnderRay(const Ray &ray, rw::V3d *hitPos, float *hitT)
{
	float bestT = 1.0e30f;
	rw::V3d bestHit = { 0.0f, 0.0f, 0.0f };
	ObjectInst *bestInst = findClosestRayHitInVisibleList(visibleInsts, ray, &bestHit, &bestT);

	ObjectInst *sortedHit = findClosestRayHitInSortedList(sortedInsts, ray, &bestHit, &bestT);
	if(sortedHit)
		bestInst = sortedHit;

	if(bestInst == nil)
		return nil;
	if(hitPos)
		*hitPos = bestHit;
	if(hitT)
		*hitT = bestT;
	return bestInst;
}

static void
RenderCollision(ObjectInst *inst)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj == nil) return;
	if(obj->m_colModel)
		RenderColModelWire(obj->m_colModel, &inst->m_matrix, obj->m_isBigBuilding);
	else
		printf("object %s has no LOD\n", obj->m_name);
}

void
RenderEverythingCollisions(void)
{
	int i, numVisibleInsts = (int)visibleInsts.size();
	for(i = 0; i < numVisibleInsts; i++)
		RenderCollision(visibleInsts[i]);

	int numSortedInsts = (int)sortedInsts.size();
	for(i = 0; i < numSortedInsts; i++)
		RenderCollision(sortedInsts[i].inst);
}

void
setupLeedsPipe(void)
{
	gta::leedsPipe_amb = Timecycle::currentColours.amb;
	gta::leedsPipe_emiss = Timecycle::currentColours.amb_bl;
	switch(gBuildingPipeSwitch){
	case PLATFORM_PS2:
		gta::leedsPipe_platformSwitch = 1;
		break;
	case PLATFORM_PSP:
		gta::leedsPipe_platformSwitch = 0;
		break;
	default:
		gta::leedsPipe_platformSwitch = 2;
		break;
	}
}

#ifdef RW_D3D9
static void
leedsRenderCB(rw::Atomic *atomic, rw::d3d9::InstanceDataHeader *header)
{
	setupLeedsPipe();
	gta::leedsRenderCB(atomic, header);
}

void
MakeLeedsPipe(void)
{
	gta::MakeLeedsPipe();
	((rw::d3d9::ObjPipeline*)gta::leedsPipe)->renderCB = leedsRenderCB;
}
#endif

#ifdef RW_GL3
static void
leedsRenderCB(rw::Atomic *atomic, rw::gl3::InstanceDataHeader *header)
{
	setupLeedsPipe();
	gta::leedsRenderCB(atomic, header);
}

void
MakeLeedsPipe(void)
{
	gta::MakeLeedsPipe();
	((rw::gl3::ObjPipeline*)gta::leedsPipe)->renderCB = leedsRenderCB;
}
#endif

void
RegisterPipes(void)
{
#ifdef RW_GL3
	rw::gl3::registerUniform("u_dayparam");
	rw::gl3::registerUniform("u_nightparam");
	rw::gl3::registerUniform("u_texmat");
	rw::gl3::registerUniform("u_envmat");
	rw::gl3::registerUniform("u_envXform");
	rw::gl3::registerUniform("u_shininess");
	rw::gl3::registerUniform("u_colorscale");

	rw::gl3::registerUniform("tex0");
	rw::gl3::registerUniform("tex1");
	rw::gl3::registerUniform("u_lm");

	rw::gl3::registerUniform("u_postfxCol1");
	rw::gl3::registerUniform("u_postfxCol2");
	rw::gl3::registerUniform("u_postfxParams");

	rw::gl3::registerUniform("u_amb");
	rw::gl3::registerUniform("u_emiss");
#endif
}

void
RenderInit(void)
{
	colourCodePipe = gta::makeColourCodePipeline();
	MakeCustomBuildingPipelines();
	MakeNeoWorldPipe();
	MakeLeedsPipe();
}

// Object outlines: the edges that shape a model, drawn as lines on top of
// it. They show selected and hovered objects whatever their textures, so
// invisible and see-through objects can still be found and retextured.

// Model-space segments (pairs of points) along a geometry's open borders
// and sharp creases. Vertices are welded by position first, so texture
// seams do not count as borders.
static void
BuildOutlineEdges(rw::Geometry *geo, std::vector<rw::V3d> &out)
{
	out.clear();
	if(geo->triangles == nil || geo->numMorphTargets < 1 || geo->morphTargets[0].vertices == nil)
		return;
	rw::V3d *verts = geo->morphTargets[0].vertices;

	std::vector<int> weld(geo->numVertices);
	std::unordered_map<uint64, int> byPosition;
	for(int i = 0; i < geo->numVertices; i++){
		// 2 mm grid; 21 bits per axis covers +-2 km of model space.
		uint64 qx = (uint64)(lroundf(verts[i].x * 500.0f) & 0x1FFFFF);
		uint64 qy = (uint64)(lroundf(verts[i].y * 500.0f) & 0x1FFFFF);
		uint64 qz = (uint64)(lroundf(verts[i].z * 500.0f) & 0x1FFFFF);
		auto it = byPosition.emplace(qx << 42 | qy << 21 | qz, i);
		weld[i] = it.first->second;
	}

	struct EdgeInfo {
		rw::V3d normal;
		int count;
		bool crease;	// two faces meet at a sharp angle
		bool opposite;	// back-to-back faces, as on a double-sided sheet
	};
	const float creaseCos = 0.82f;	// about 35 degrees
	std::unordered_map<uint64, EdgeInfo> edges;
	for(int t = 0; t < geo->numTriangles; t++){
		int v[3];
		for(int k = 0; k < 3; k++)
			v[k] = weld[geo->triangles[t].v[k]];
		if(v[0] == v[1] || v[1] == v[2] || v[0] == v[2])
			continue;
		rw::V3d n = cross(sub(verts[v[1]], verts[v[0]]), sub(verts[v[2]], verts[v[0]]));
		float len = length(n);
		if(len < 1e-8f)
			continue;
		n = scale(n, 1.0f/len);
		for(int k = 0; k < 3; k++){
			uint32 a = v[k], b = v[(k+1)%3];
			uint64 key = a < b ? (uint64)a << 32 | b : (uint64)b << 32 | a;
			auto it = edges.find(key);
			if(it == edges.end()){
				EdgeInfo info = { n, 1, false, false };
				edges.emplace(key, info);
				continue;
			}
			EdgeInfo &info = it->second;
			info.count++;
			float d = dot(n, info.normal);
			if(d < -0.98f)
				info.opposite = true;
			else if(d < creaseCos)
				info.crease = true;
		}
	}

	for(auto &edge : edges){
		const EdgeInfo &info = edge.second;
		// A double-sided sheet's border has exactly its front and back face.
		if(info.count == 1 || info.crease || (info.count == 2 && info.opposite)){
			out.push_back(verts[edge.first >> 32]);
			out.push_back(verts[edge.first & 0xFFFFFFFF]);
		}
	}
}

struct OutlineCacheEntry
{
	int numVertices;
	int numTriangles;
	std::vector<rw::V3d> points;
};
static std::unordered_map<rw::Geometry*, OutlineCacheEntry> outlineCache;

static const std::vector<rw::V3d>&
GetOutlineEdges(rw::Geometry *geo)
{
	auto it = outlineCache.find(geo);
	// A streamed-out model's geometry can be freed and the address reused.
	if(it != outlineCache.end() && it->second.numVertices == geo->numVertices &&
	   it->second.numTriangles == geo->numTriangles)
		return it->second.points;
	if(outlineCache.size() > 4096)
		outlineCache.clear();
	OutlineCacheEntry &entry = outlineCache[geo];
	entry.numVertices = geo->numVertices;
	entry.numTriangles = geo->numTriangles;
	BuildOutlineEdges(geo, entry.points);
	return entry.points;
}

static void
AddAtomicOutline(rw::Atomic *atomic, std::vector<rw::V3d> &out)
{
	rw::Geometry *geo = atomic->geometry;
	if(geo == nil || !(atomic->getFlags() & rw::Atomic::RENDER))
		return;
	const std::vector<rw::V3d> &edges = GetOutlineEdges(geo);
	if(edges.empty())
		return;
	size_t start = out.size();
	out.resize(start + edges.size());
	rw::V3d::transformPoints(&out[start], edges.data(), (int32)edges.size(), atomic->getFrame()->getLTM());
	// Pull the lines a little toward the camera so they win the depth test
	// against the surface they lie on.
	for(size_t i = start; i < out.size(); i++)
		out[i] = add(out[i], scale(sub(TheCamera.m_position, out[i]), 0.002f));
}

static void
AddInstOutline(ObjectInst *inst, std::vector<rw::V3d> &out)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj == nil || inst->m_rwObject == nil)
		return;
	if(obj->m_type == ObjectDef::ATOMIC)
		AddAtomicOutline((rw::Atomic*)inst->m_rwObject, out);
	else if(obj->m_type == ObjectDef::CLUMP){
		FORLIST(lnk, ((rw::Clump*)inst->m_rwObject)->atomics)
			AddAtomicOutline(rw::Atomic::fromClump(lnk), out);
	}
}

void
RenderObjectOutlines(void)
{
	enum { OUTLINE_MAP, OUTLINE_HOVER, OUTLINE_SELECTED, NUM_OUTLINES };
	static const rw::RGBA colours[NUM_OUTLINES] = {
		{ 90, 200, 255, 170 },	// map objects
		{ 255, 255, 255, 255 },	// hovered
		{ 255, 190, 40, 255 },	// selected
	};
	static std::vector<rw::V3d> points[NUM_OUTLINES];
	const size_t maxMapPoints = 400000;
	const float maxMapDistance = 300.0f;

	for(int k = 0; k < NUM_OUTLINES; k++)
		points[k].clear();
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted || inst->m_editorHidden || PlayerViewHides(inst) || inst->m_rwObject == nil ||
		   InstUsesEditorIcon(inst))
			continue;
		if(inst->m_selected && gOutlineSelection)
			AddInstOutline(inst, points[OUTLINE_SELECTED]);
		else if(inst->m_highlight == HIGHLIGHT_HOVER && gOutlineSelection)
			AddInstOutline(inst, points[OUTLINE_HOVER]);
		else if(gOutlineMapObjects && points[OUTLINE_MAP].size() < maxMapPoints &&
		        PawnObjectNeedsExport(inst) &&
		        length(sub(inst->m_translation, TheCamera.m_position)) < maxMapDistance)
			AddInstOutline(inst, points[OUTLINE_MAP]);
	}
	if(points[OUTLINE_MAP].empty() && points[OUTLINE_HOVER].empty() && points[OUTLINE_SELECTED].empty())
		return;

	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 0);
	// Faintly where something is in front, so an object behind a wall or
	// inside another can still be found.
	rw::SetRenderState(rw::ZTESTENABLE, 0);
	for(int k = OUTLINE_HOVER; k < NUM_OUTLINES; k++){
		rw::RGBA faint = colours[k];
		faint.alpha = 70;
		RenderLinesNow(points[k].data(), (int)points[k].size(), faint);
	}
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	for(int k = 0; k < NUM_OUTLINES; k++)
		RenderLinesNow(points[k].data(), (int)points[k].size(), colours[k]);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
}
