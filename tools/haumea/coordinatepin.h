// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

void CoordinatePinPanel();
void CoordinatePinOverlay(bool enabled);
bool CoordinatePinHandleViewport();
bool CoordinatePinOwnsMouse();
bool CoordinatePinPosition(rw::V3d *position, rw::V3d *end = nullptr);
bool CoordinatePinScreen(ImVec2 *point);
