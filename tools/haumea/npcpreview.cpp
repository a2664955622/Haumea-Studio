// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "sapaths.h"
#include "anim.h"
#include "npcpreview.h"

#include <math.h>
#include <stdlib.h>
#include <vector>
#include <map>

// open.mp (Server/Components/NPCs/NPC/npc.cpp) moves an NPC towards its
// target at moveSpeed / 100 units per millisecond and calls a point reached
// once it is within the point's stop range. Path movement then heads for
// the next point; node movement picks a random link of the point it reached,
// avoiding the one it came from when there is a choice (Node/node.cpp).

namespace {

enum { MOVE_WALK = 1, MOVE_JOG = 2, MOVE_SPRINT = 3 };
const float SPEED_WALK = 0.1552086f, SPEED_JOG = 0.56444f, SPEED_SPRINT = 0.926784f;
// Node positions are on the ground; a standing ped's position is about 1m up.
const float NODE_HEIGHT = 1.0f;
const float REPLAY_PAUSE = 1.5f;

struct Preview
{
	// The NPC as it was, put back on stop.
	rw::V3d origPos;
	rw::Quat origRot;
	bool hadAnim;
	AnimPlayback anim;
	// Where the preview last put it, to notice the user moving it.
	rw::V3d appliedPos;
	rw::Quat appliedRot;

	bool byNodes;
	int moveType;
	float speed;	// units per second
	// Path movement: the points as they were when the preview started.
	std::vector<rw::V3d> points;
	std::vector<float> stopRanges;
	int pointIndex;
	bool reversed;
	// Node movement.
	SAPaths::Node *node, *lastNode;
	float nodeRadius;
	// Current leg.
	rw::V3d pos, target;
	float stopRange;
	bool finished;
	float pauseLeft;
};

std::map<int32, Preview> gPreviews;	// by ObjectInst::m_id

void
placeInstance(ObjectInst *inst, const rw::V3d &pos, const rw::Quat &rot)
{
	inst->m_translation = pos;
	inst->m_rotation = rot;
	inst->UpdateMatrix();
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj && inst->m_rwObject){
		rw::Frame *f = obj->m_type == ObjectDef::ATOMIC ?
			((rw::Atomic*)inst->m_rwObject)->getFrame() :
			((rw::Clump*)inst->m_rwObject)->getFrame();
		f->transform(&inst->m_matrix, rw::COMBINEREPLACE);
	}
}

bool
samePlace(const ObjectInst *inst, const rw::V3d &pos, const rw::Quat &rot)
{
	return inst->m_translation.x == pos.x && inst->m_translation.y == pos.y &&
		inst->m_translation.z == pos.z && inst->m_rotation.x == rot.x &&
		inst->m_rotation.y == rot.y && inst->m_rotation.z == rot.z &&
		inst->m_rotation.w == rot.w;
}

rw::V3d
nodePosition(SAPaths::Node *node)
{
	rw::V3d pos = { 0.0f, 0.0f, 0.0f };
	SAPaths::GetNodeInfo(node, nil, nil, nil, &pos);
	pos.z += NODE_HEIGHT;
	return pos;
}

// open.mp's NPCNode::process: a random link, not back where it came from
// when there is another way.
SAPaths::Node*
nextNode(Preview &p)
{
	SAPaths::Node *links[16];
	int n = SAPaths::GetNodeLinks(p.node, links, 16);
	if(n <= 0)
		return nil;
	for(int attempt = 0; attempt < 10; attempt++){
		SAPaths::Node *pick = links[rand() % n];
		if(pick != p.lastNode || n == 1)
			return pick;
	}
	return links[0];
}

// The move type and speed open.mp would use (NPC::move).
void
setMovement(Preview &p, int moveType, float moveSpeed)
{
	if(moveType != MOVE_WALK && moveType != MOVE_JOG && moveType != MOVE_SPRINT){
		// Auto: jog at the automatic speed, or the type nearest a given speed.
		if(moveSpeed < 0.0f)
			moveType = MOVE_JOG;
		else{
			float dWalk = fabsf(moveSpeed - SPEED_WALK), dJog = fabsf(moveSpeed - SPEED_JOG),
				dSprint = fabsf(moveSpeed - SPEED_SPRINT);
			moveType = dWalk <= dJog && dWalk <= dSprint ? MOVE_WALK : dJog <= dSprint ? MOVE_JOG : MOVE_SPRINT;
		}
	}
	if(moveSpeed < 0.0f)
		moveSpeed = moveType == MOVE_SPRINT ? SPEED_SPRINT : moveType == MOVE_JOG ? SPEED_JOG : SPEED_WALK;
	p.moveType = moveType;
	p.speed = moveSpeed * 10.0f;	// moveSpeed / 100 units per ms
}

void
playMoveAnimation(ObjectInst *inst, int moveType)
{
	static const char *names[3][3] = {
		{ "WALK_player", "WALK_civi", nil },
		{ "run_player", "JOG_maleA", nil },
		{ "sprint_civi", "sprint_panic", nil },
	};
	int row = moveType == MOVE_SPRINT ? 2 : moveType == MOVE_JOG ? 1 : 0;
	for(int i = 0; names[row][i]; i++)
		if(SetInstanceAnimation(inst, "PED", names[row][i], 4.1f, true, false, false, false))
			return;
}

// Back to the start of the route.
bool
startRoute(ObjectInst *inst, Preview &p)
{
	const ObjectPawnProperties *props = GetObjectPawnProperties(inst);
	p.finished = false;
	p.pauseLeft = 0.0f;
	p.lastNode = nil;
	if(p.byNodes){
		// NPC_PlayNode puts the NPC on the point and heads for one of its links.
		p.node = SAPaths::FindNode(props->npcNode, props->npcNodePoint);
		if(p.node == nil)
			return false;
		p.pos = nodePosition(p.node);
		SAPaths::Node *next = nextNode(p);
		if(next == nil)
			return false;
		p.lastNode = p.node;
		p.node = next;
		p.target = nodePosition(next);
		p.stopRange = p.nodeRadius;
	}else{
		// NPC_MoveByPath walks from where the NPC stands to the first point.
		if(p.points.empty())
			return false;
		p.pos = p.origPos;
		p.pointIndex = p.reversed ? (int)p.points.size() - 1 : 0;
		p.target = p.points[p.pointIndex];
		p.stopRange = p.stopRanges[p.pointIndex];
	}
	playMoveAnimation(inst, p.moveType);
	return true;
}

// The next target, or false when the route has ended.
bool
advance(Preview &p)
{
	if(p.byNodes){
		SAPaths::Node *next = nextNode(p);
		if(next == nil)
			return false;
		p.lastNode = p.node;
		p.node = next;
		p.target = nodePosition(next);
		p.stopRange = p.nodeRadius;
		return true;
	}
	p.pointIndex += p.reversed ? -1 : 1;
	if(p.pointIndex < 0 || p.pointIndex >= (int)p.points.size())
		return false;
	p.target = p.points[p.pointIndex];
	p.stopRange = p.stopRanges[p.pointIndex];
	return true;
}

void
restore(ObjectInst *inst, Preview &p, bool putBack)
{
	if(putBack)
		placeInstance(inst, p.origPos, p.origRot);
	if(p.hadAnim)
		SetInstanceAnimation(inst, p.anim.lib, p.anim.name, p.anim.delta, p.anim.loop,
			p.anim.lockX, p.anim.lockY, p.anim.freeze, p.anim.timeMs);
	else
		ClearInstanceAnimation(inst);
}

}	// namespace

bool
NpcPreviewStart(ObjectInst *npc)
{
	if(npc == nil || npc->m_isDeleted || GetSampElementKind(npc) != SAMP_ELEMENT_NPC)
		return false;
	NpcPreviewStop(npc);
	const ObjectPawnProperties *props = GetObjectPawnProperties(npc);
	Preview p = {};
	p.origPos = npc->m_translation;
	p.origRot = npc->m_rotation;
	p.hadAnim = GetInstanceAnimation(npc, &p.anim);
	// As the map plays them: a recording (the .rec player shows those), else
	// the nodes, else the path.
	if(props->npcRecording[0])
		return false;
	setMovement(p, props->npcMoveType, props->npcMoveSpeed);
	p.byNodes = props->npcPlaysNode;
	if(!p.byNodes && props->pathName[0]){
		std::vector<ObjectInst*> points;
		GetNpcPathPoints(props->pathName, points);
		for(size_t i = 0; i < points.size(); i++){
			p.points.push_back(points[i]->m_translation);
			p.stopRanges.push_back(GetObjectPawnProperties(points[i])->pathStopRange);
		}
		p.reversed = props->npcPathReversed;
	}
	p.nodeRadius = props->npcNodeRadius;
	if(!startRoute(npc, p)){
		restore(npc, p, false);
		return false;
	}
	gPreviews[npc->m_id] = p;
	Preview &live = gPreviews[npc->m_id];
	live.appliedPos = npc->m_translation;
	live.appliedRot = npc->m_rotation;
	return true;
}

void
NpcPreviewStop(ObjectInst *npc)
{
	if(npc == nil)
		return;
	std::map<int32, Preview>::iterator it = gPreviews.find(npc->m_id);
	if(it == gPreviews.end())
		return;
	restore(npc, it->second, true);
	gPreviews.erase(it);
}

bool
NpcPreviewIsActive(ObjectInst *npc)
{
	return npc && gPreviews.find(npc->m_id) != gPreviews.end();
}

void
NpcPreviewStopAll(void)
{
	for(std::map<int32, Preview>::iterator it = gPreviews.begin(); it != gPreviews.end(); ++it){
		ObjectInst *inst = GetInstanceByID(it->first);
		if(inst)
			restore(inst, it->second, true);
	}
	gPreviews.clear();
}

void
NpcPreviewUpdate(float timeStep)
{
	for(std::map<int32, Preview>::iterator it = gPreviews.begin(); it != gPreviews.end();){
		ObjectInst *inst = GetInstanceByID(it->first);
		Preview &p = it->second;
		if(inst == nil || inst->m_isDeleted){
			if(inst)
				restore(inst, p, true);
			it = gPreviews.erase(it);
			continue;
		}
		if(!samePlace(inst, p.appliedPos, p.appliedRot)){
			// The user moved or rotated it: keep their edit as its own place.
			restore(inst, p, false);
			it = gPreviews.erase(it);
			continue;
		}
		if(p.finished){
			p.pauseLeft -= timeStep;
			if(p.pauseLeft <= 0.0f && !startRoute(inst, p)){
				restore(inst, p, true);
				it = gPreviews.erase(it);
				continue;
			}
		}else{
			float travel = p.speed * timeStep;
			// Several short legs can pass in one frame.
			for(int legs = 0; legs < 64 && !p.finished; legs++){
				rw::V3d toTarget = sub(p.target, p.pos);
				float distance = length(toTarget);
				float arrive = p.stopRange > 0.0f ? p.stopRange : 0.0f;
				float toArrival = distance - arrive;
				if(toArrival <= travel){
					if(toArrival > 0.0f){
						p.pos = add(p.pos, scale(toTarget, toArrival / distance));
						travel -= toArrival;
					}
					if(!advance(p)){
						p.finished = true;
						p.pauseLeft = REPLAY_PAUSE;
						ClearInstanceAnimation(inst);	// stand at the end
					}
					continue;
				}
				p.pos = add(p.pos, scale(toTarget, travel / distance));
				break;
			}
		}
		// Face the way it goes (open.mp's getAngleOfLine: 0 is north).
		rw::Quat rot = inst->m_rotation;
		rw::V3d dir = sub(p.target, p.pos);
		if(!p.finished && dir.x*dir.x + dir.y*dir.y > 0.0001f){
			float heading = atan2f(-dir.x, dir.y) * 180.0f / 3.14159265f;
			rot = MakeObjectRotationDegrees({ 0.0f, 0.0f, heading });
		}
		placeInstance(inst, p.pos, rot);
		p.appliedPos = inst->m_translation;
		p.appliedRot = inst->m_rotation;
		++it;
	}
}
