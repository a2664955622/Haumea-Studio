// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <vector>

// SA-MP / open.mp NPC recordings (npcmodes/recordings/*.rec).
//
// A file is an 8 byte header (uint32 version, int32 type) followed by frames:
// a uint32 timestamp in milliseconds since playback started, then the legacy
// sync block of the recording's type (open.mp Playback/record_manager.hpp).
// Include after haumea.h.

enum RecType {
	REC_NONE = 0,
	REC_DRIVER = 1,	// LegacyVehicleSyncData, 63 bytes
	REC_ONFOOT = 2	// LegacyOnFootSyncData, 68 bytes
};

// One frame, as stored. Quaternions are SA-MP sync quaternions, which use the
// same (conjugate) convention as IPL rotations and ObjectInst::m_rotation.
struct RecFrame
{
	uint32 time;
	uint16 lr, ud, keys;
	rw::V3d pos;
	rw::Quat rot;
	rw::V3d vel;	// GTA units per 1/50 s
	uint8 health, armour;
	uint8 weaponKey;	// weapon in the low 6 bits, additional key in the top 2
	// driver
	uint16 vehicleId;
	float vehicleHealth;
	uint8 siren, gear;
	uint16 trailerId;
	uint32 extra;	// Hydra thruster angle, or the train speed's float bits
	// on foot
	uint8 specialAction;
	rw::V3d surfOffset;
	uint16 surfId;
	uint16 animId, animFlags;
};

struct RecFile
{
	char path[1024];
	uint32 version;
	int type;	// RecType
	std::vector<RecFrame> frames;
	int trailingBytes;	// a truncated last frame, ignored like open.mp does
	float distance;	// length of the route in GTA units
};

// The recording's state at a moment of playback.
struct RecSample
{
	int type;	// RecType
	int frame;	// index of the frame the server has sent last
	rw::V3d pos;
	rw::Quat rot;
	rw::V3d vel;
	float health, armour;
	int weapon;
	int keys, lr, ud;
	bool onFoot;
	// driver: recordings name the vehicle ID, not a model; the model is
	// whatever vehicle the NPC is put in (vehicleModel is left -1).
	int vehicleId;
	int vehicleModel;
	float vehicleHealth;
	bool siren;
	int gear;
	float trainSpeed;
	// on foot
	int specialAction;
	int animId, animFlags;
};

bool RecLoad(const char *path, RecFile *rec);
float RecDuration(const RecFile &rec);	// seconds, up to the last frame's timestamp
bool RecSampleAt(const RecFile &rec, float t, RecSample *out);	// t in seconds; false without frames
const char *RecTypeName(int type);
// The type of <name>.rec in the recordings folder uiRecPlayer uses (only the
// header is read), or REC_NONE when it is missing or not a recording.
int RecTypeOfName(const char *name);

// Viewport player. Each played instance keeps its own clock; the instance's
// own transform is put back when its playback stops. Playback never marks
// the instance dirty and never goes through undo.
void RecPlayerUpdate(void);	// once per frame, before the render list is built
bool RecPlayerIsActive(ObjectInst *inst);
void RecPlayerStop(ObjectInst *inst);
void RecPlayerStopAll(void);	// before saving or exporting, so edits are not saved mid-route

// "Recording" section for Object Info: picks a .rec file from the recordings
// folder into recName (the name without .rec, as NPC_StartPlayback takes it)
// and plays it on inst. Returns true when recName changed.
bool uiRecPlayer(ObjectInst *inst, char *recName, size_t size);
// Any path inside a server (the open map, say): the default recordings folder
// is then that server's npcmodes/recordings, until the user picks another.
void RecSetServerHint(const char *path);
