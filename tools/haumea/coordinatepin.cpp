// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "coordinatepin.h"
#include "i18n.h"
#include "shortcuts.h"
#include <cmath>

namespace {
bool visible, placed, measured, dragging, nearPin;
bool snapSurface = true;
int picking; // 1: pin, 2: endpoint
int panelFrame = -100;
rw::V3d pin, endpoint, normal = {0, 0, 1};
rw::V3d savedEnd;
bool savedMeasured;
float offset;

bool project(rw::V3d p, ImVec2 *screen)
{
	rw::V3d s;
	float w, h;
	if(!Sprite::CalcScreenCoors(p, &s, &w, &h, false)) return false;
	*screen = EditorUiPoint(s.x, s.y);
	return true;
}

void arm(int kind)
{
	SpawnExitPlaceMode();
	ExitPrefabPlaceMode();
	ExitBrushMode();
	gRectSelectActive = false;
	picking = kind;
	dragging = false;
}

bool surface(rw::V3d *point, rw::V3d *outNormal)
{
	Ray ray;
	ray.start = TheCamera.m_position;
	ray.dir = normalize(TheCamera.m_mouseDir);
	if(snapSurface){
		ObjectInst *inst = GetVisibleInstUnderRay(ray, point);
		if(inst){
			rw::V3d detailed, n;
			if(IntersectRayColModelDetailed(ray, inst, &detailed, &n)){
				*point = detailed;
				if(outNormal) *outNormal = n;
			}else if(outNormal) *outNormal = {0, 0, 1};
			return true;
		}
	}
	// Empty space uses the pin's horizontal plane (camera target for placement).
	float z = placed ? pin.z : TheCamera.m_target.z;
	if(fabsf(ray.dir.z) < 0.0001f) return false;
	float t = (z - ray.start.z) / ray.dir.z;
	if(t <= 0 || t > 5000) return false;
	*point = add(ray.start, scale(ray.dir, t));
	if(outNormal) *outNormal = {0, 0, 1};
	return true;
}

void label(ImDrawList *draw, ImVec2 p, const char *text)
{
	ImVec2 size = ImGui::CalcTextSize(text);
	p.x -= size.x * 0.5f;
	draw->AddRectFilled(ImVec2(p.x - 5, p.y - 3), ImVec2(p.x + size.x + 5, p.y + size.y + 3), IM_COL32(25, 25, 29, 230), 4);
	draw->AddText(p, IM_COL32(255, 225, 90, 255), text);
}
}

bool CoordinatePinPosition(rw::V3d *position, rw::V3d *end)
{
	if(position) *position = pin;
	if(end) *end = endpoint;
	return placed && (!end || measured);
}

bool CoordinatePinScreen(ImVec2 *point)
{
	if(!placed || !project(pin, point)) return false;
	point->y -= 20;
	return true;
}

void CoordinatePinPanel()
{
	panelFrame = ImGui::GetFrameCount();
	if(ImGui::Button(_("Place pin"))) arm(1);
	ImGui::SetItemTooltip(_("Click a surface to place the pin. Drag from the pin to measure to an object."));
	ImGui::SameLine();
	ImGui::BeginDisabled(!placed);
	if(ImGui::Button(_("Clear pin"))){ placed = measured = dragging = false; picking = 0; }
	ImGui::EndDisabled();
	ImGui::Checkbox(_("Snap to surfaces"), &snapSurface);
	if(picking) ImGui::TextWrapped("%s", picking == 1 ? _("Click to place the pin. Esc cancels.") : _("Click the measurement endpoint. Esc cancels."));
	ImGui::BeginDisabled(!selection.first);
	if(ImGui::Button(_("Pin at selected centre")) && selection.first){
		ObjectInst *inst = (ObjectInst*)selection.first->item;
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		rw::V3d centre = {0, 0, 0};
		if(obj && obj->m_colModel) centre = scale(add(obj->m_colModel->boundingBox.min, obj->m_colModel->boundingBox.max), 0.5f);
		pin = rw::add(inst->m_translation, add(add(scale(inst->m_matrix.right, centre.x), scale(inst->m_matrix.up, centre.y)), scale(inst->m_matrix.at, centre.z)));
		normal = normalize(inst->m_matrix.up);
		placed = true; measured = false; picking = 0;
	}
	ImGui::SetItemTooltip(_("Uses the object's collision-box centre. For a doorway inside a building, pick its two edges and use Midpoint."));
	ImGui::EndDisabled();
	if(!placed) return;
	ImGui::InputFloat3(_("Pin XYZ"), &pin.x, "%.3f");
	if(ImGui::Button(_("Copy coordinates"))){
		char text[160];
		snprintf(text, sizeof(text), "%.6f, %.6f, %.6f", pin.x, pin.y, pin.z);
		ImGui::SetClipboardText(text);
	}
	ImGui::SameLine();
	if(ImGui::Button(_("Pick endpoint"))) arm(2);
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
	ImGui::InputFloat(_("Surface offset"), &offset, 0, 0, "%.3f");
	ImGui::SetItemTooltip(_("Move the pin this many units along the last picked surface normal. Negative values move to the other side."));
	ImGui::SameLine();
	if(ImGui::Button(_("Apply offset"))) pin = add(pin, scale(normal, offset));
	if(!measured) return;
	ImGui::InputFloat3(_("Endpoint XYZ"), &endpoint.x, "%.3f");
	rw::V3d delta = sub(endpoint, pin);
	ImGui::Text(_("Distance: %.3f units"), length(delta));
	ImGui::Text("dX %.3f   dY %.3f   dZ %.3f", delta.x, delta.y, delta.z);
	if(ImGui::Button(_("Midpoint"))){ pin = scale(add(pin, endpoint), 0.5f); measured = false; }
	ImGui::SetItemTooltip(_("Place the pin halfway between the two picked points, for example the edges of a doorway."));
	ImGui::SameLine();
	ImGui::TextUnformatted(_("Align pin:"));
	for(int axis = 0; axis < 3; axis++){
		ImGui::SameLine();
		const char *labels[] = {"X##pin_align", "Y##pin_align", "Z##pin_align"};
		if(ImGui::SmallButton(labels[axis])){
			if(axis == 0) pin.x = endpoint.x;
			if(axis == 1) pin.y = endpoint.y;
			if(axis == 2) pin.z = endpoint.z;
		}
	}
}

bool CoordinatePinOwnsMouse()
{
	return visible && (picking || dragging || nearPin);
}

bool CoordinatePinHandleViewport()
{
	if(!visible || (!picking && !dragging && !nearPin)) return false;
	if(!dragging && ImGui::GetIO().WantCaptureMouse) return false;
	if(ShortcutPressed(SC_CANCEL) || CPad::IsMButtonClicked(3)){
		if(dragging){ endpoint = savedEnd; measured = savedMeasured; }
		picking = 0; dragging = false;
		return true;
	}
	if(picking && CPad::IsMButtonClicked(1)){
		rw::V3d p, n;
		if(surface(&p, &n)){
			if(picking == 1){ pin = p; normal = n; placed = true; measured = false; }
			else { endpoint = p; measured = true; }
			picking = 0;
		}
		return true;
	}
	if(!picking && nearPin && CPad::IsMButtonJustDown(1)){
		savedEnd = endpoint; savedMeasured = measured;
		dragging = true;
	}
	if(dragging){
		rw::V3d p;
		if(!ImGui::GetIO().WantCaptureMouse && surface(&p, nullptr)){ endpoint = p; measured = true; }
		if(!CPad::IsMButtonDown(1)) dragging = false;
	}
	return picking || dragging || nearPin;
}

void CoordinatePinOverlay(bool enabled)
{
	visible = enabled && ImGui::GetFrameCount() - panelFrame <= 1;
	nearPin = false;
	if(!visible){
		if(dragging){ endpoint = savedEnd; measured = savedMeasured; }
		picking = 0; dragging = false;
		return;
	}
	if(!placed) return;
#ifdef IMGUI_HAS_VIEWPORT
	ImDrawList *draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
#else
	ImDrawList *draw = ImGui::GetBackgroundDrawList();
#endif
	ImVec2 foot, head;
	const ImU32 yellow = IM_COL32(255, 218, 60, 255);
	if(project(pin, &foot)){
		head = ImVec2(foot.x, foot.y - 20);
		ImVec2 mouse = ImGui::GetIO().MousePos;
		float dx = mouse.x - head.x, dy = mouse.y - head.y;
		nearPin = dx * dx + dy * dy < 15 * 15;
		draw->AddLine(foot, head, yellow, 2);
		draw->AddCircleFilled(head, 8, yellow);
		draw->AddCircleFilled(head, 3, IM_COL32(50, 43, 26, 255));
		if(nearPin && !ImGui::GetIO().WantCaptureMouse) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
	if(measured){
		// A world line remains correctly clipped when an endpoint is behind the camera.
		RenderLine(pin, endpoint, {255, 218, 60, 255}, {255, 218, 60, 255});
		ImVec2 end;
		if(project(endpoint, &end)){
			draw->AddCircle(end, 5, yellow, 0, 2);
			draw->AddLine(ImVec2(end.x - 9, end.y), ImVec2(end.x + 9, end.y), yellow);
			draw->AddLine(ImVec2(end.x, end.y - 9), ImVec2(end.x, end.y + 9), yellow);
		}
		rw::V3d delta = sub(endpoint, pin);
		ImVec2 middle;
		if(project(scale(add(pin, endpoint), 0.5f), &middle)){
			char text[180];
			snprintf(text, sizeof(text), _("%.3f units | dX %.3f  dY %.3f  dZ %.3f"), length(delta), delta.x, delta.y, delta.z);
			label(draw, ImVec2(middle.x, middle.y - 24), text);
		}
	}
}
