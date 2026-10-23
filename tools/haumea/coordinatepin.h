// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <vector>
namespace rw { struct V3d; }
struct ImVec2;

void CoordinatePinPanel();
void CoordinatePinOverlay(bool enabled);
bool CoordinatePinHandleViewport();
bool CoordinatePinOwnsMouse();
bool CoordinatePinPosition(rw::V3d *position, rw::V3d *end = nullptr);
// handle: 0 pin head (move), 1 base (measure), 2 endpoint (move).
bool CoordinatePinScreen(ImVec2 *point, int handle = 0, int index = -1);
int CoordinatePinCount();
int CoordinatePinSnapModel();
bool CoordinatePinAnchorSnapped();
// The same guides used by the alignment panel are targets for N / object snap.
void CoordinatePinSnapTargets(int area, const rw::V3d &centre, std::vector<rw::V3d> &targets);
