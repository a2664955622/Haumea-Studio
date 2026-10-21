#pragma once

// Dynamic areas in the 3D view (areas.cpp): drawing, handles and polygon points.

// Whether p is inside the area (CreateDynamic*'s own test, with its z limits).
bool IsPointInDynamicArea(const ObjectInst *area, const rw::V3d &p);
// How many instances are limited to the area with this variable name.
int CountItemsInArea(const char *areaName);

// The area's outline, and the handles of the selected one.
void DrawDynamicAreaOverlay(const ObjectInst *area, bool selected);
// Called first by the viewport's click handling: drags a handle of the
// selected area, or adds polygon points while that is on. True when it took
// the mouse.
bool DynamicAreaHandleViewport(void);
// Click to add corners to a polygon area (Shift+click keeps adding; Esc or a
// right click stops).
void StartAreaPointPicking(ObjectInst *area);
bool IsAreaPointPicking(const ObjectInst *area);
void StopAreaPointPicking(void);
