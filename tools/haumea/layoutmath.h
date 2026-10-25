// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace LayoutMath
{
using Point = std::array<float, 3>;
inline Point add(Point a, Point b)
{
	return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
inline Point mul(Point a, float s)
{
	return {a[0] * s, a[1] * s, a[2] * s};
}
inline Point sub(Point a, Point b)
{
	return add(a, mul(b, -1));
}
inline float dot(Point a, Point b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline Point cross(Point a, Point b)
{
	return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline float length(Point a)
{
	return std::sqrt(dot(a, a));
}
inline Point unit(Point a, Point fallback = {0, 1, 0})
{
	float d = length(a);
	return d > 1e-6f ? mul(a, 1 / d) : fallback;
}
inline Point mix(Point a, Point b, float t)
{
	return add(mul(a, 1 - t), mul(b, t));
}
struct Knot {
	Point p, in = {0, 0, 0}, out = {0, 0, 0};
	bool automatic = true;
};
inline Point bezier(const Knot &a, const Knot &b, float t)
{
	float s = 1 - t;
	return add(add(mul(a.p, s * s * s), mul(add(a.p, a.out), 3 * s * s * t)),
	           add(mul(add(b.p, b.in), 3 * s * t * t), mul(b.p, t * t * t)));
}
inline Point tangent(const Knot &a, const Knot &b, float t)
{
	float s = 1 - t;
	return add(add(mul(a.out, 3 * s * s), mul(sub(add(b.p, b.in), add(a.p, a.out)), 6 * s * t)),
	           mul(b.in, -3 * t * t));
}
inline void automaticHandles(std::vector<Knot> &v, bool closed)
{
	if (v.size() < 2)
		return;
	for (size_t i = 0; i < v.size(); i++)
		if (v[i].automatic) {
			Point before = v[(i + v.size() - 1) % v.size()].p, after = v[(i + 1) % v.size()].p;
			if (!closed && i == 0)
				before = v[i].p;
			if (!closed && i + 1 == v.size())
				after = v[i].p;
			Point d = mul(sub(after, before), (!closed && (i == 0 || i + 1 == v.size())) ? 1.f / 3 : 1.f / 6);
			v[i].out = d;
			v[i].in = mul(d, -1);
		}
}
// de Casteljau subdivision preserves the curve exactly before a new point moves.
inline Knot split(Knot &a, Knot &b, float t = .5f)
{
	Point a1 = mix(a.p, add(a.p, a.out), t), b1 = mix(add(a.p, a.out), add(b.p, b.in), t),
	      c1 = mix(add(b.p, b.in), b.p, t);
	Point a2 = mix(a1, b1, t), b2 = mix(b1, c1, t), p = mix(a2, b2, t);
	a.out = sub(a1, a.p);
	b.in = sub(c1, b.p);
	a.automatic = b.automatic = false;
	Knot k;
	k.p = p;
	k.in = sub(a2, p);
	k.out = sub(b2, p);
	k.automatic = false;
	return k;
}
struct Sample {
	Point p, tangent, normal;
	float distance = 0;
};
inline std::vector<Sample> sample(const std::vector<Knot> &knots, bool curved, bool closed, float spacing,
                                  size_t limit = 2000)
{
	std::vector<Sample> dense, result;
	if (knots.size() < 2 || !std::isfinite(spacing) || spacing < .01f || limit == 0)
		return result;
	for (const Knot &k : knots)
		for (Point p : {k.p, k.in, k.out})
			for (float v : p)
				if (!std::isfinite(v) || std::abs(v) > 1000000)
					return result;
	float total = 0;
	size_t segments = closed ? knots.size() : knots.size() - 1;
	for (size_t i = 0; i < segments; i++) {
		const Knot &a = knots[i], &b = knots[(i + 1) % knots.size()];
		float extent = length(a.out) + length(sub(add(b.p, b.in), add(a.p, a.out))) + length(b.in);
		int steps =
		    curved ? std::max(16, std::min(256, (int)std::ceil(extent / std::max(.05f, spacing * .1f)))) : 1;
		for (int j = (i ? 1 : 0); j <= steps; j++) {
			float t = (float)j / steps;
			Sample s;
			s.p = curved ? bezier(a, b, t) : mix(a.p, b.p, t);
			s.tangent = unit(curved ? tangent(a, b, t) : sub(b.p, a.p));
			if (!dense.empty())
				total += length(sub(s.p, dense.back().p));
			s.distance = total;
			dense.push_back(s);
		}
	}
	if (total < .001f)
		return result;
	size_t cursor = 1;
	for (float d = 0; d <= (closed ? total - .0001f : total + .0001f) && result.size() < limit;
	     d += spacing) {
		while (cursor + 1 < dense.size() && dense[cursor].distance < d)
			cursor++;
		const Sample &a = dense[cursor - 1], &b = dense[cursor];
		float f = b.distance > a.distance
		              ? std::max(0.f, std::min(1.f, (d - a.distance) / (b.distance - a.distance)))
		              : 0;
		Sample s;
		s.p = mix(a.p, b.p, f);
		s.tangent = curved ? unit(mix(a.tangent, b.tangent, f), a.tangent) : b.tangent;
		s.distance = d;
		Point n = {0, 0, 1};
		if (!result.empty()) {
			const Sample &prev = result.back();
			n = prev.normal;
			Point axis = cross(prev.tangent, s.tangent);
			float sine = length(axis), cosine = std::max(-1.f, std::min(1.f, dot(prev.tangent, s.tangent)));
			if (sine > 1e-5f) {
				axis = mul(axis, 1 / sine);
				n = add(add(mul(n, cosine), mul(cross(axis, n), sine)),
				        mul(axis, dot(axis, n) * (1 - cosine)));
			}
		}
		n = sub(n, mul(s.tangent, dot(n, s.tangent)));
		if (length(n) < 1e-4f) {
			n = {0, 1, 0};
			n = sub(n, mul(s.tangent, dot(n, s.tangent)));
		}
		s.normal = unit(n, {1, 0, 0});
		result.push_back(s);
	}
	return result;
}
} // namespace LayoutMath
