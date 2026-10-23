// SPDX-License-Identifier: GPL-2.0-or-later
#include "modelquery.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <limits>

bool ParseModelIdQuery(const char *text, std::vector<std::pair<int, int>> *ranges)
{
	if(!text) return false;
	std::vector<std::pair<int, int>> parsed;
	const char *p = text;
	auto number = [&](int &value){
		char *end;
		errno = 0;
		long n = std::strtol(p, &end, 10);
		if(end == p || errno == ERANGE || n < INT_MIN || n > INT_MAX) return false;
		p = end; value = (int)n;
		return true;
	};
	while(*p){
		while(*p == ' ' || *p == '\t' || *p == ',' || *p == ';') ++p;
		if(!*p) break;
		int first, last;
		if(!number(first)) return false;
		last = first;
		const char *afterNumber = p;
		while(*p == ' ' || *p == '\t') ++p;
		if(*p == '-'){
			++p;
			while(*p == ' ' || *p == '\t') ++p;
			if(!number(last)) return false;
		}
		else if(p != afterNumber){ parsed.push_back({first, first}); continue; }
		if(*p && *p != ' ' && *p != '\t' && *p != ',' && *p != ';') return false;
		if(first > last) std::swap(first, last);
		parsed.push_back({first, last});
	}
	if(parsed.empty()) return false;
	if(ranges) *ranges = std::move(parsed);
	return true;
}

float ModelDimensionDifference(std::array<float, 3> a, std::array<float, 3> b, bool proportions)
{
	for(float &x : a) x = std::abs(x);
	for(float &x : b) x = std::abs(x);
	if(a[0] < a[1]) std::swap(a[0], a[1]);
	if(b[0] < b[1]) std::swap(b[0], b[1]);
	float sa = *std::max_element(a.begin(), a.end()), sb = *std::max_element(b.begin(), b.end());
	if(sa <= 0 || sb <= 0 || !std::isfinite(sa) || !std::isfinite(sb))
		return std::numeric_limits<float>::infinity();
	if(proportions){ for(float &x : a) x /= sa; for(float &x : b) x /= sb; }
	float difference = 0;
	for(int i = 0; i < 3; ++i){
		float delta = std::abs(a[i] - b[i]);
		// Ignore centimetre-scale collision-box noise when comparing metres.
		if(!proportions) delta = std::max(0.0f, delta - 0.05f);
		difference = std::max(difference, delta / std::max(a[i], proportions ? 0.10f : 0.05f));
	}
	return difference;
}
