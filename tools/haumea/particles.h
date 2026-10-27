// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
struct ObjectInst;
namespace ParticlePreview
{
extern bool enabled;
struct Stats {
	int systems = 0, live = 0, drawn = 0, missing = 0, definitions = 0;
	bool limited = false;
};
void Render(float dt);
bool HasParticleEffect(const ObjectInst *inst);
void Restart(const ObjectInst *inst);
const Stats &GetStats();
// Current emitter, live particle or drawn sprite count for one Pawn model ID.
int CountForModel(int model, int kind);
} // namespace ParticlePreview
