// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <utility>
#include <vector>

bool ParseModelIdQuery(const char *text, std::vector<std::pair<int, int>> *ranges);
// Maximum relative difference; horizontal axes may be exchanged. Proportions
// discard overall scale. Infinity means dimensions are unavailable.
float ModelDimensionDifference(std::array<float, 3> reference, std::array<float, 3> candidate, bool proportions);
