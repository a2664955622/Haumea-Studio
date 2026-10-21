// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "playerview.h"

#include <algorithm>
#include <vector>

bool gPlayerView;
int gPlayerViewWorld;

namespace {

// open.mp SDK values.hpp and the streamer's defaults.
const int CLIENT_OBJECTS = 1000;	// OBJECT_POOL_SIZE_037: what a client creates
const float STREAMER_OBJECT_SD = 300.0f;
const float STREAM_DISTANCE = 200.0f;	// players, vehicles and actors
const int MAX_STREAMED_VEHICLES = 700;
const int MAX_STREAMED_ACTORS = 50;
const float LABEL_DRAW_DISTANCE = 20.0f;	// what the editor writes when it is 0

enum { PV_UNTOUCHED, PV_HIDDEN, PV_SHOWN };

PlayerViewStats stats;

struct Candidate
{
	ObjectInst *inst;
	float dist;
};

bool
byDistance(const Candidate &a, const Candidate &b)
{
	return a.dist < b.dist;
}

// Streams the nearest `limit` of the candidates in range; the rest are hidden.
int
streamNearest(std::vector<Candidate> &list, int limit)
{
	std::sort(list.begin(), list.end(), byDistance);
	for(size_t i = 0; i < list.size(); i++)
		if((int)i >= limit)
			list[i].inst->m_playerView = PV_HIDDEN;
	return min((int)list.size(), limit);
}

}	// namespace

void
PlayerViewUpdate(void)
{
	// Off: one pass to clear what the last frame set, then nothing.
	static bool wasOn;
	if(!gPlayerView && !wasOn)
		return;
	wasOn = gPlayerView;
	stats = PlayerViewStats();
	bool streamer = GetPawnMapFormat() == PAWN_CODE_STREAMER;
	stats.streamer = streamer;
	std::vector<Candidate> objects, vehicles, actors;
	std::vector<ObjectInst*> nativeObjects;

	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		inst->m_playerView = PV_UNTOUCHED;
		if(!gPlayerView || inst->m_isDeleted || !PawnObjectNeedsExport(inst))
			continue;
		const ObjectPawnProperties *props = GetObjectPawnProperties(inst);
		int kind = GetSampElementKind(inst);
		bool nativeObject = !streamer && kind == SAMP_ELEMENT_OBJECT && !inst->m_isVehicle;

		// Virtual world and interior. Native objects have neither; native
		// elements have a world but no interior, except vehicles.
		if(!nativeObject){
			bool world = props->virtualWorld < 0 || props->virtualWorld == gPlayerViewWorld;
			bool usesInterior = streamer || inst->m_isVehicle;
			bool interior = !usesInterior || inst->m_area <= 0 || inst->m_area == currentArea;
			if(!world || !interior){
				inst->m_playerView = PV_HIDDEN;
				stats.otherWorld++;
				continue;
			}
		}

		float dist = TheCamera.distanceTo(inst->m_translation);
		inst->m_playerView = PV_SHOWN;
		if(inst->m_isVehicle){
			if(dist > STREAM_DISTANCE)
				inst->m_playerView = PV_HIDDEN;
			else
				vehicles.push_back({ inst, dist });
		}else if(kind == SAMP_ELEMENT_ACTOR){
			if(dist > STREAM_DISTANCE)
				inst->m_playerView = PV_HIDDEN;
			else
				actors.push_back({ inst, dist });
		}else if(kind == SAMP_ELEMENT_LABEL){
			float draw = props->drawDistance > 0.0f ? props->drawDistance : LABEL_DRAW_DISTANCE;
			if(dist > draw)
				inst->m_playerView = PV_HIDDEN;
		}else if(kind == SAMP_ELEMENT_OBJECT){
			if(nativeObject)
				nativeObjects.push_back(inst);
			else{
				float sd = props->streamDistance > 0.0f ? props->streamDistance : STREAMER_OBJECT_SD;
				if(dist > sd)
					inst->m_playerView = PV_HIDDEN;
				else
					objects.push_back({ inst, dist });
			}
		}
	}
	if(!gPlayerView)
		return;

	// Native objects past the pool are never created; the client has the rest.
	if(!streamer){
		std::sort(nativeObjects.begin(), nativeObjects.end(),
			[](const ObjectInst *a, const ObjectInst *b){ return a->m_id < b->m_id; });
		for(size_t i = 0; i < nativeObjects.size(); i++){
			if((int)i >= CLIENT_OBJECTS)
				nativeObjects[i]->m_playerView = PV_HIDDEN;
			else
				objects.push_back({ nativeObjects[i], TheCamera.distanceTo(nativeObjects[i]->m_translation) });
		}
		stats.objectsInRange = (int)objects.size();
		stats.objectsStreamed = (int)objects.size();
	}else{
		stats.objectsInRange = (int)objects.size();
		stats.objectsStreamed = streamNearest(objects, CLIENT_OBJECTS);
	}
	stats.vehiclesStreamed = streamNearest(vehicles, MAX_STREAMED_VEHICLES);
	stats.actorsStreamed = streamNearest(actors, MAX_STREAMED_ACTORS);

	// Created objects show within their draw distance, the model's when 0.
	for(const Candidate &c : objects){
		if(c.inst->m_playerView != PV_SHOWN)
			continue;
		const ObjectPawnProperties *props = GetObjectPawnProperties(c.inst);
		ObjectDef *obj = GetObjectDef(c.inst->m_objectId);
		float draw = props->drawDistance > 0.0f ? props->drawDistance : obj ? obj->GetLargestDrawDist() : 300.0f;
		if(c.dist > draw)
			c.inst->m_playerView = PV_HIDDEN;
		else
			stats.objectsVisible++;
	}
}

bool
PlayerViewHides(const ObjectInst *inst)
{
	return gPlayerView && inst->m_playerView == PV_HIDDEN && !inst->m_selected;
}

bool
PlayerViewShows(const ObjectInst *inst)
{
	return gPlayerView && inst->m_playerView == PV_SHOWN;
}

const PlayerViewStats&
GetPlayerViewStats(void)
{
	return stats;
}
