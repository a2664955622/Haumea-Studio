// SPDX-License-Identifier: GPL-2.0-or-later
#include "mapcleanup.h"
#include "haumea.h"
#include "i18n.h"
#include "layoutpaths.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

namespace
{
struct Group {
	int keep;
	std::vector<int> copies;
};
std::vector<Group> groups;
float tolerance = .01f;
bool onlySelected = false, strict = true, scanned = false;
uint32 scanRevision;
int protectedCount;
bool sameSettings(const ObjectInst *a, const ObjectInst *b)
{
	ObjectPawnProperties pa = *GetObjectPawnProperties(a), pb = *GetObjectPawnProperties(b);
	// Keep distinct named variables: other gamemode code can refer to both.
	if (pa.name[0] && pb.name[0] && strcmp(pa.name, pb.name) != 0)
		return false;
	memset(pa.name, 0, sizeof(pa.name));
	memset(pb.name, 0, sizeof(pb.name));
	memset(pa.group, 0, sizeof(pa.group));
	memset(pb.group, 0, sizeof(pb.group));
	memset(pa.comment, 0, sizeof(pa.comment));
	memset(pb.comment, 0, sizeof(pb.comment));
	if (memcmp(&pa, &pb, sizeof(pa)) != 0)
		return false;
	for (int i = 0; i < MAX_PAWN_MATERIALS; i++) {
		const ObjectMaterialOverride *ma = GetObjectMaterialOverride(a, i),
		                             *mb = GetObjectMaterialOverride(b, i);
		bool aa = ma && ma->active, bb = mb && mb->active;
		if (aa != bb || (aa && memcmp(ma, mb, sizeof(*ma)) != 0))
			return false;
	}
	return true;
}
bool identical(const ObjectInst *a, const ObjectInst *b)
{
	if (a->m_objectId != b->m_objectId || a->m_area != b->m_area ||
	    GetObjectPawnProperties(a)->virtualWorld != GetObjectPawnProperties(b)->virtualWorld)
		return false;
	rw::V3d d = sub(a->m_translation, b->m_translation);
	float similarity = std::abs(a->m_rotation.x * b->m_rotation.x + a->m_rotation.y * b->m_rotation.y +
	                            a->m_rotation.z * b->m_rotation.z + a->m_rotation.w * b->m_rotation.w);
	return dot(d, d) <= tolerance * tolerance && similarity > 0.999999f && (!strict || sameSettings(a, b));
}
void scan()
{
	groups.clear();
	protectedCount = 0;
	std::set<int> referenced;
	std::vector<int> linkedLayouts;
	LayoutPathsReferencedObjects(linkedLayouts);
	referenced.insert(linkedLayouts.begin(), linkedLayouts.end());
	for (CPtrNode *it = instances.first; it; it = it->next) {
		auto *o = (ObjectInst *)it->item;
		if (o->m_isDeleted)
			continue;
		auto *p = GetObjectPawnProperties(o);
		if (p->attachParent >= 0)
			referenced.insert(p->attachParent);
		if (p->npcVehicle >= 0)
			referenced.insert(p->npcVehicle);
		if (o->m_lod)
			referenced.insert(o->m_lod->m_id);
	}
	std::vector<ObjectInst *> objects;
	for (CPtrNode *it = instances.first; it; it = it->next) {
		auto *o = (ObjectInst *)it->item;
		if (o->m_isDeleted || o->m_isVehicle || GetSampElementKind(o) != SAMP_ELEMENT_OBJECT ||
		    !IsPawnObjectEditable(o) || IsPawnWorldObject(o) || (onlySelected && !o->m_selected))
			continue;
		if (referenced.count(o->m_id) || GetObjectPawnProperties(o)->attachParent != PAWN_ATTACH_NONE) {
			protectedCount++;
			continue;
		}
		objects.push_back(o);
	}
	std::sort(objects.begin(), objects.end(), [](ObjectInst *a, ObjectInst *b) {
		bool an = GetObjectPawnProperties(a)->name[0], bn = GetObjectPawnProperties(b)->name[0];
		return an != bn ? an > bn : a->m_id < b->m_id;
	});
	using Key = std::tuple<int, int, int, long long, long long, long long>;
	std::map<Key, std::vector<ObjectInst *>> cells;
	std::map<int, size_t> groupIndex;
	for (ObjectInst *o : objects) {
		rw::V3d p = o->m_translation;
		if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
			continue;
		long long x = (long long)std::floor((double)p.x / tolerance),
		          y = (long long)std::floor((double)p.y / tolerance),
		          z = (long long)std::floor((double)p.z / tolerance);
		int world = GetObjectPawnProperties(o)->virtualWorld;
		ObjectInst *keep = nullptr;
		for (int dx = -1; dx <= 1 && !keep; dx++)
			for (int dy = -1; dy <= 1 && !keep; dy++)
				for (int dz = -1; dz <= 1 && !keep; dz++) {
					auto at = cells.find(Key{o->m_objectId, o->m_area, world, x + dx, y + dy, z + dz});
					if (at != cells.end())
						for (ObjectInst *candidate : at->second)
							if (identical(o, candidate)) {
								keep = candidate;
								break;
							}
				}
		if (keep) {
			auto found = groupIndex.find(keep->m_id);
			size_t index;
			if (found == groupIndex.end()) {
				index = groups.size();
				groupIndex[keep->m_id] = index;
				groups.push_back({keep->m_id, {}});
			} else
				index = found->second;
			groups[index].copies.push_back(o->m_id);
		} else
			cells[Key{o->m_objectId, o->m_area, world, x, y, z}].push_back(o);
	}
	scanRevision = GetLatestChangeSeq();
	scanned = true;
}
void selectCopies(const Group *group = nullptr)
{
	ClearSelection();
	for (const Group &g : groups)
		if (!group || &g == group)
			for (int id : g.copies)
				if (ObjectInst *o = GetInstanceByID(id))
					if (!o->m_isDeleted)
						o->Select();
}
} // namespace
int MapCleanupDuplicateCount()
{
	int count = 0;
	for (const auto &g : groups)
		count += (int)g.copies.size();
	return count;
}
void MapCleanupPanel()
{
	bool change = ImGui::Checkbox(L("Selection only"), &onlySelected);
	change |= ImGui::Checkbox(L("Match properties and materials"), &strict);
	ImGui::SetItemTooltip(
	    _("Keeps objects with different runtime settings, textures, or named variables separate."));
	ImGui::SetNextItemWidth(
	    std::max(ImGui::GetFontSize() * 6, ImGui::GetContentRegionAvail().x -
		                                       ImGui::CalcTextSize(_("Position tolerance")).x -
		                                       ImGui::GetStyle().ItemInnerSpacing.x));
	change |= ImGui::InputFloat(L("Position tolerance"), &tolerance, .001f, .01f, "%.3f m");
	tolerance = std::isfinite(tolerance) ? clamp(tolerance, .001f, 1.f) : .01f;
	if (change)
		scanned = false;
	if (ImGui::Button(L("Find duplicates")))
		scan();
	if (!scanned)
		return;
	int count = MapCleanupDuplicateCount();
	ImGui::Text(_("%d duplicates in %d groups"), count, (int)groups.size());
	if (protectedCount)
		ImGui::TextWrapped(_("%d attached or referenced objects excluded from cleanup."), protectedCount);
	bool stale = GetLatestChangeSeq() != scanRevision;
	if (stale)
		ImGui::TextWrapped(_("The map changed. Find duplicates again before removing objects."));
	ImGui::BeginDisabled(stale || count == 0);
	if (ImGui::Button(L("Select duplicates")))
		selectCopies();
	ImGui::SameLine();
	if (ImGui::Button(L("Remove duplicates")))
		ImGui::OpenPopup("##remove_duplicates");
	if (ImGui::BeginPopup("##remove_duplicates")) {
		ImGui::TextWrapped(_("Remove %d duplicates, keeping one object per group? Undo restores them."),
		                   count);
		if (!strict)
			ImGui::TextWrapped(
			    _("Properties and materials were ignored. Overlapping objects may be intentional."));
		if (ImGui::Button(L("Remove##confirm_duplicates"))) {
			// Refresh the comparison immediately before a mutation; never trust stale IDs.
			scan();
			selectCopies();
			DeleteSelected();
			scanned = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(L("Cancel")))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	ImGui::EndDisabled();
	if (ImGui::BeginChild("##duplicate_results", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 7),
	                      ImGuiChildFlags_Borders)) {
		for (const Group &g : groups)
			if (ObjectInst *o = GetInstanceByID(g.keep)) {
				ImGui::PushID(g.keep);
				char label[128];
				snprintf(label, sizeof(label), _("Model %d: keep #%d, %d duplicates"),
				         PawnModelId(o->m_objectId), g.keep, (int)g.copies.size());
				ImGui::BeginDisabled(stale);
				if (ImGui::Selectable(label))
					selectCopies(&g);
				ImGui::EndDisabled();
				ImGui::PopID();
			}
	}
	ImGui::EndChild();
}
