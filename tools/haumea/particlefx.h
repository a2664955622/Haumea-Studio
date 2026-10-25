// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <istream>
#include <string>
#include <unordered_map>
#include <vector>

// GTA SA FX project data and a bounded, renderer-independent editor simulation.
// No game assets are embedded here; the project is read from the chosen game.
// FX syntax/channel semantics: gta-reversed game_sa/Fx (FxSystemBP, FxPrimBP
// and Blueprints). This editor integrator is independent of the game engine.
namespace ParticleFX
{
struct Vec {
	float x = 0, y = 0, z = 0;
};
Vec operator+(Vec a, Vec b);
Vec operator-(Vec a, Vec b);
Vec operator*(Vec a, float b);
struct Matrix {
	Vec right{1, 0, 0}, up{0, 1, 0}, at{0, 0, 1}, pos;
	Vec vector(Vec v) const;
	Vec point(Vec v) const;
};
struct Key {
	float time, value;
};
struct Curve {
	bool looped = false;
	std::vector<Key> keys;
	float sample(float time, float fallback) const;
};
enum InfoType {
	Rate,
	Speed,
	Life,
	Direction,
	Angle,
	Position,
	Volume,
	Rotation,
	Force,
	Friction,
	Wind,
	Jitter,
	Noise,
	RotSpeed,
	Size,
	Colour,
	BrightColour,
	SpriteRect,
	Flat,
	SelfLit,
	HeatHaze,
	AnimTexture,
	Trail,
	Attract,
	Count
};
struct Info {
	bool present = false, particleTime = true;
	std::vector<Curve> curves;
	float sample(size_t channel, float systemTime, float age, float fallback) const;
};
struct Primitive {
	Matrix matrix;
	std::array<std::string, 4> textures;
	std::array<Info, Count> info;
	int srcBlend = 4, dstBlend = 5;
	bool alpha = true;
	float value(InfoType type, size_t channel, float time, float age, float fallback) const;
};
struct System {
	std::string name;
	float length = 1, loopMin = 0, loopMax = 0;
	int playMode = 2;
	std::vector<Primitive> primitives;
};
struct Project {
	std::vector<System> systems;
	std::unordered_map<std::string, size_t> names;
	bool load(std::istream &input, std::string *error);
	const System *find(const std::string &name) const;
};
struct Particle {
	Vec position, velocity;
	float age = 0, life = 1, rotation = 0, sizeRandomX = 0, sizeRandomY = 0, spinRandom = 0;
	unsigned primitive = 0, texture = 0;
};
struct Appearance {
	float width = 0, height = 0, rotation = 0, red = 255, green = 255, blue = 255, alpha = 255;
	float top = .5f, bottom = -.5f, left = -.5f, right = .5f;
	bool rectangular = false, flat = false;
};
class Instance
{
	uint32_t randomState;
	std::vector<float> remainder;
	float clock = 0;
	float random();
	void step(float dt, const Matrix &transform, Vec wind, size_t limit);

  public:
	const System *system;
	std::vector<Particle> particles;
	explicit Instance(const System *system, uint32_t seed = 1);
	void advance(float dt, const Matrix &transform, Vec wind = {}, size_t limit = 1024);
	Appearance appearance(const Particle &particle) const;
	float time() const
	{
		return clock;
	}
};
} // namespace ParticleFX
