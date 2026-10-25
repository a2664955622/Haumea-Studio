// SPDX-License-Identifier: GPL-2.0-or-later
#include "particlefx.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace ParticleFX
{
Vec operator+(Vec a, Vec b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec operator-(Vec a, Vec b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec operator*(Vec a, float b)
{
	return {a.x * b, a.y * b, a.z * b};
}
Vec Matrix::vector(Vec v) const
{
	return right * v.x + up * v.y + at * v.z;
}
Vec Matrix::point(Vec v) const
{
	return pos + vector(v);
}
static float dot(Vec a, Vec b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
static Vec cross(Vec a, Vec b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static Vec normal(Vec v)
{
	float n = std::sqrt(dot(v, v));
	return n > 1e-6f ? v * (1 / n) : Vec{0, 0, 1};
}
static float bounded(float v, float a, float b)
{
	return std::max(a, std::min(b, v));
}
static std::string lower(std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z')
			c += 32;
	return s;
}
float Curve::sample(float t, float fallback) const
{
	if (keys.empty())
		return fallback;
	if (looped && keys.back().time > 0 && t > keys.back().time)
		t = std::fmod(t, keys.back().time);
	if (t <= keys.front().time)
		return keys.front().value;
	auto hi = std::upper_bound(keys.begin(), keys.end(), t, [](float x, const Key &k) { return x < k.time; });
	if (hi == keys.end())
		return keys.back().value;
	const Key &a = *(hi - 1), &b = *hi;
	return a.value + (b.value - a.value) * ((t - a.time) / (b.time - a.time));
}
float Info::sample(size_t c, float t, float age, float fallback) const
{
	return c < curves.size() ? curves[c].sample(particleTime ? age : t, fallback) : fallback;
}
float Primitive::value(InfoType type, size_t channel, float t, float age, float fallback) const
{
	return info[type].sample(channel, t, age, fallback);
}
static int infoType(const std::string &name)
{
	static const char *names[] = {
		"EMRATE",		"EMSPEED",	  "EMLIFE", "EMDIR",   "EMANGLE",  "EMPOS",	   "EMSIZE", "EMROTATION",
		"FORCE",		"FRICTION",	  "WIND",	"JITTER",  "NOISE",	   "ROTSPEED", "SIZE",	 "COLOUR",
		"COLOURBRIGHT", "SPRITERECT", "FLAT",	"SELFLIT", "HEATHAZE", "ANIMTEX",  "TRAIL",	 "ATTRACTPT"};
	for (int n = 0; n < Count; n++)
		if (name == std::string("FX_INFO_") + names[n] + "_DATA")
			return n;
	return -1;
}
const System *Project::find(const std::string &name) const
{
	auto it = names.find(lower(name));
	return it == names.end() ? nullptr : &systems[it->second];
}
bool Project::load(std::istream &input, std::string *error)
{
	systems.clear();
	names.clear();
	System *system = nullptr;
	Primitive *prim = nullptr;
	Info *info = nullptr;
	Curve *curve = nullptr;
	std::string line;
	float keyTime = 0;
	bool haveTime = false;
	int lengths = 0;
	size_t lines = 0;
	auto fail = [&](const char *why) {
		if (error)
			*error = std::string(why) + " at line " + std::to_string(lines);
		systems.clear();
		names.clear();
		return false;
	};
	while (std::getline(input, line)) {
		if (++lines > 1000000 || line.size() > 4096)
			return fail("FX project exceeds limits");
		size_t a = line.find_first_not_of(" \t\r"), b = line.find_last_not_of(" \t\r");
		if (a == std::string::npos || line[a] == '#')
			continue;
		line = line.substr(a, b - a + 1);
		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string field = line.substr(0, colon), value = line.substr(colon + 1);
		a = value.find_first_not_of(" \t");
		value = a == std::string::npos ? "" : value.substr(a);
		if (field == "FX_SYSTEM_DATA") {
			if (systems.size() >= 1024)
				return fail("Too many FX systems");
			systems.emplace_back();
			system = &systems.back();
			prim = nullptr;
			info = nullptr;
			curve = nullptr;
			lengths = 0;
			continue;
		}
		if (!system)
			continue;
		if (field == "FX_PRIM_EMITTER_DATA") {
			if (system->primitives.size() >= 64)
				return fail("Too many FX primitives");
			system->primitives.emplace_back();
			prim = &system->primitives.back();
			info = nullptr;
			curve = nullptr;
			continue;
		}
		if (field.compare(0, 8, "FX_INFO_") == 0) {
			int type = infoType(field);
			info = prim && type >= 0 ? &prim->info[type] : nullptr;
			if (info) {
				info->present = true;
				info->particleTime = type > Rotation;
			}
			curve = nullptr;
			continue;
		}
		if (field == "FX_INTERP_DATA") {
			curve = nullptr;
			if (info) {
				if (info->curves.size() >= 16)
					return fail("Too many FX channels");
				info->curves.emplace_back();
				curve = &info->curves.back();
			}
			haveTime = false;
			continue;
		}
		if (field == "NAME") {
			if (!prim)
				system->name = lower(value);
			continue;
		}
		if (field == "TEXTURE" || field == "TEXTURE2" || field == "TEXTURE3" || field == "TEXTURE4") {
			if (prim) {
				int n = field == "TEXTURE" ? 0 : field.back() - '1';
				if (value != "NULL")
					prim->textures[n] = lower(value);
			}
			continue;
		}
		if (field == "MATRIX" && prim) {
			std::istringstream in(value);
			float m[12];
			for (float &v : m)
				if (!(in >> v) || !std::isfinite(v) || std::fabs(v) > 1000000)
					return fail("Invalid FX matrix");
			prim->matrix = {{m[0], m[1], m[2]}, {m[3], m[4], m[5]}, {m[6], m[7], m[8]}, {m[9], m[10], m[11]}};
			continue;
		}
		// Labels such as SPEED: precede interpolation blocks; no value is attached.
		if (value.empty())
			continue;
		char *end = nullptr;
		float n = std::strtof(value.c_str(), &end);
		if (end == value.c_str())
			continue;
		if (!std::isfinite(n) || std::fabs(n) > 1000000)
			return fail("Invalid FX value");
		if (field == "LENGTH" && !prim) {
			if (lengths++ == 0)
				system->length = bounded(n, .001f, 600);
			else
				system->loopMax = bounded(n, 0, 600);
		} else if (field == "LOOPINTERVALMIN" && !prim)
			system->loopMin = bounded(n, 0, 600);
		else if (field == "PLAYMODE" && !prim)
			system->playMode = (int)bounded(n, 0, 2);
		else if (field == "SRCBLENDID" && prim)
			prim->srcBlend = (int)bounded(n, 0, 10);
		else if (field == "DSTBLENDID" && prim)
			prim->dstBlend = (int)bounded(n, 0, 10);
		else if (field == "ALPHAON" && prim)
			prim->alpha = n != 0;
		else if (field == "TIMEMODEPRT" && info)
			info->particleTime = n != 0;
		else if (field == "LOOPED" && curve)
			curve->looped = n != 0;
		else if (field == "NUM_KEYS" && (n < 0 || n > 4096))
			return fail("Invalid FX key count");
		else if (field == "TIME" && curve) {
			keyTime = n;
			haveTime = true;
		} else if (field == "VAL" && curve) {
			if (!haveTime || curve->keys.size() >= 4096)
				return fail("Invalid FX key");
			curve->keys.push_back({keyTime, n});
			haveTime = false;
		}
	}
	if (input.bad())
		return fail("Could not read FX project");
	for (size_t i = 0; i < systems.size(); i++) {
		System &s = systems[i];
		if (s.name.empty() || s.primitives.empty())
			return fail("Incomplete FX system");
		names[s.name] = i;
		for (Primitive &p : s.primitives)
			for (Info &in : p.info)
				for (Curve &c : in.curves) {
					std::stable_sort(c.keys.begin(), c.keys.end(),
									 [](const Key &a, const Key &b) { return a.time < b.time; });
					// The last value at a repeated time wins, avoiding a zero interpolation span.
					std::vector<Key> keys;
					for (Key k : c.keys) {
						if (!keys.empty() && keys.back().time == k.time)
							keys.back() = k;
						else
							keys.push_back(k);
					}
					c.keys.swap(keys);
				}
	}
	if (systems.empty())
		return fail("No FX systems");
	return true;
}
Instance::Instance(const System *s, uint32_t seed)
	: randomState(seed ? seed : 1), remainder(s->primitives.size(), 0), system(s)
{
}
float Instance::random()
{
	randomState ^= randomState << 13;
	randomState ^= randomState >> 17;
	randomState ^= randomState << 5;
	return (randomState >> 8) * (1.0f / 16777216.0f);
}
void Instance::advance(float dt, const Matrix &transform, Vec wind, size_t limit)
{
	if (!std::isfinite(dt) || dt <= 0)
		return;
	dt = std::min(dt, .1f);
	limit = std::min(limit, size_t(2048));
	if (particles.size() > limit)
		particles.resize(limit);
	int steps = (int)std::ceil(dt * 60);
	float stepTime = dt / steps;
	for (int i = 0; i < steps; i++)
		step(stepTime, transform, wind, limit);
}
void Instance::step(float dt, const Matrix &transform, Vec wind, size_t limit)
{
	float period = system->length + system->loopMin;
	float t = std::fmod(clock, std::max(period, .001f));
	float nt = t / system->length;
	for (Particle &p : particles) {
		const Primitive &prim = system->primitives[p.primitive];
		float age = p.age / p.life;
		Vec force = {prim.value(Force, 0, nt, age, 0), prim.value(Force, 1, nt, age, 0),
					 prim.value(Force, 2, nt, age, 0)};
		force = force + wind * prim.value(Wind, 0, nt, age, 0);
		if (prim.info[Attract].present) {
			Vec point = {prim.value(Attract, 0, nt, age, 0), prim.value(Attract, 1, nt, age, 0),
						 prim.value(Attract, 2, nt, age, 0)};
			force = force + normal(transform.point(point) - p.position) * prim.value(Attract, 3, nt, age, 0);
		}
		p.velocity = p.velocity + force * dt;
		float friction = bounded(prim.value(Friction, 0, nt, age, 1), 0, 2);
		p.velocity = p.velocity * std::pow(friction, dt * 50);
		p.position = p.position + p.velocity * dt;
		float cw = prim.value(RotSpeed, 0, nt, age, 0), cwMax = prim.value(RotSpeed, 1, nt, age, cw);
		float ccw = prim.value(RotSpeed, 2, nt, age, cw), ccwMax = prim.value(RotSpeed, 3, nt, age, cwMax);
		float spin =
			p.spinRandom >= 0 ? cw + (cwMax - cw) * p.spinRandom : -(ccw + (ccwMax - ccw) * -p.spinRandom);
		p.rotation += spin * dt;
		p.age += dt;
	}
	particles.erase(
		std::remove_if(particles.begin(), particles.end(), [](const Particle &p) { return p.age >= p.life; }),
		particles.end());
	// Non-looping bursts play once per placed object. Continuous effects use the
	// project's play mode and time curves, without weather changing stream range.
	bool emitting = t < system->length && (system->playMode != 0 || clock < system->length);
	if (emitting)
		for (unsigned index = 0; index < system->primitives.size(); index++) {
			const Primitive &prim = system->primitives[index];
			if (prim.info[HeatHaze].present)
				continue; // refractive pass is not a sprite
			float rate = bounded(prim.value(Rate, 0, t, 0, 10), 0, 2000);
			remainder[index] += rate * dt;
			unsigned count = (unsigned)remainder[index];
			remainder[index] -= count;
			for (unsigned n = 0; n < count && particles.size() < limit; n++) {
				auto value = [&](InfoType type, size_t c, float fallback) {
					return prim.value(type, c, t, 0, fallback);
				};
				Particle p;
				p.primitive = index;
				p.life = bounded(value(Life, 0, 1) + (random() * 2 - 1) * value(Life, 1, 0), .016f, 60);
				p.rotation =
					value(Rotation, 0, 0) + random() * (value(Rotation, 1, 0) - value(Rotation, 0, 0));
				p.sizeRandomX = random() * 2 - 1;
				p.sizeRandomY = random() * 2 - 1;
				p.spinRandom = random() * 2 - 1;
				p.texture = (unsigned)(random() * 4);
				Vec local = {value(Position, 0, 0), value(Position, 1, 0), value(Position, 2, 0)};
				float radius = value(Volume, 0, 0);
				if (std::fabs(radius) > 1e-6f) {
					Vec v = normal({random() * 2 - 1, random() * 2 - 1, random() * 2 - 1});
					local = local + v * (std::fabs(radius) * (radius < 0 ? 1 : std::cbrt(random())));
				} else
					for (int axis = 0; axis < 3; axis++) {
						float lo = value(Volume, 1 + axis * 2, 0), hi = value(Volume, 2 + axis * 2, lo);
						float *component = axis == 0 ? &local.x : axis == 1 ? &local.y : &local.z;
						*component += lo + random() * (hi - lo);
					}
				Vec direction =
					normal({value(Direction, 0, 0), value(Direction, 1, 0), value(Direction, 2, 1)});
				float amin = value(Angle, 0, 0) * .01745329252f, amax = value(Angle, 1, 0) * .01745329252f;
				float z = std::cos(amin) + (std::cos(amax) - std::cos(amin)) * random(),
					  az = random() * 6.283185307f;
				float r = std::sqrt(std::max(0.f, 1 - z * z));
				Vec right =
						normal(cross(direction, std::fabs(direction.z) > .9f ? Vec{0, 1, 0} : Vec{0, 0, 1})),
					up = cross(right, direction);
				Vec velocity = (direction * z + right * (r * std::cos(az)) + up * (r * std::sin(az))) *
							   (value(Speed, 0, 0) + (random() * 2 - 1) * value(Speed, 1, 0));
				p.position = transform.point(prim.matrix.point(local));
				p.velocity = transform.vector(prim.matrix.vector(velocity));
				particles.push_back(p);
			}
		}
	clock += dt;
}
Appearance Instance::appearance(const Particle &p) const
{
	const Primitive &prim = system->primitives[p.primitive];
	float nt = std::fmod(clock, std::max(system->length, .001f)) / system->length, age = p.age / p.life;
	auto value = [&](InfoType type, size_t c, float fallback) {
		return prim.value(type, c, nt, age, fallback);
	};
	Appearance a;
	a.rotation = p.rotation;
	a.width = bounded(value(Size, 0, .1f) + p.sizeRandomX * value(Size, 2, 0), 0, 200);
	a.height = bounded(value(Size, 1, .1f) + p.sizeRandomY * value(Size, 3, 0), 0, 200);
	InfoType colour = prim.info[BrightColour].present ? BrightColour : Colour;
	a.red = bounded(value(colour, 0, 255), 0, 255);
	a.green = bounded(value(colour, 1, 255), 0, 255);
	a.blue = bounded(value(colour, 2, 255), 0, 255);
	a.alpha = bounded(value(colour, 3, 255), 0, 255);
	a.rectangular = prim.info[SpriteRect].present;
	if (a.rectangular) {
		a.top = value(SpriteRect, 0, .5f);
		a.bottom = value(SpriteRect, 1, -.5f);
		a.left = value(SpriteRect, 2, -.5f);
		a.right = value(SpriteRect, 3, .5f);
	}
	a.flat = prim.info[Flat].present;
	return a;
}
} // namespace ParticleFX
