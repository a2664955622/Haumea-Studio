// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace Alignment
{
using Point = std::array<float, 3>;
constexpr float Degrees = 57.29577951308232f;
inline Point Angles(const Point &d)
{
	float horizontal = std::hypot(d[0], d[1]);
	return {std::hypot(horizontal, d[2]), std::atan2(d[1], d[0]) * Degrees, std::atan2(d[2], horizontal) * Degrees};
}
inline Point FromAngles(float distance, float heading, float pitch)
{
	distance = std::max(0.0f, distance);
	float horizontal = distance * std::cos(pitch / Degrees);
	return {horizontal * std::cos(heading / Degrees), horizontal * std::sin(heading / Degrees),
		distance * std::sin(pitch / Degrees)};
}
inline Point Constrain(Point d, bool shift, bool ctrl, float step)
{
	if(ctrl) {
		float horizontal = std::hypot(d[0], d[1]);
		step = std::max(1.0f, std::min(180.0f, step));
		float heading = std::round(std::atan2(d[1], d[0]) * Degrees / step) * step / Degrees;
		d[0] = horizontal * std::cos(heading);
		d[1] = horizontal * std::sin(heading);
		if(shift)
			d[2] = 0;
	} else if(shift) {
		int axis = 0;
		for(int i = 1; i < 3; i++)
			if(std::abs(d[i]) > std::abs(d[axis]))
				axis = i;
		for(int i = 0; i < 3; i++)
			if(i != axis)
				d[i] = 0;
	}
	return d;
}
// A guide is an infinite line; its projection can lie beyond either endpoint.
inline float Projection(const Point &point, const Point &a, const Point &b)
{
	float numerator = 0, denominator = 0;
	for(int i = 0; i < 3; i++) {
		numerator += (point[i] - a[i]) * (b[i] - a[i]);
		denominator += (b[i] - a[i]) * (b[i] - a[i]);
	}
	return denominator > 1.0e-8f ? numerator / denominator : 0;
}
// Closest box edge or centre to a guide, within the editor's snap distance.
inline float AxisCorrection(float minimum, float maximum, float target, float radius)
{
	float best = radius + 1;
	for(float anchor : {minimum, (minimum + maximum) * .5f, maximum}) {
		float d = target - anchor;
		if(std::abs(d) <= radius && std::abs(d) < std::abs(best))
			best = d;
	}
	return best;
}
} // namespace Alignment
