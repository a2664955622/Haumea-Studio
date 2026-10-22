// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>
#include <string>

// Help > About > Licenses: LICENSE and THIRD_PARTY_NOTICES.md, which every
// release ships next to the editor. A build run from the source tree finds
// them a few folders up (bin/<platform>/Release/haumea).

static const char *licenseFiles[] = { "LICENSE", "THIRD_PARTY_NOTICES.md", "fonts/material/LICENSE-Liberation.txt" };
static const int NUM_LICENSE_FILES = sizeof(licenseFiles)/sizeof(licenseFiles[0]);

static bool
readLicenseFile(const char *name, std::string &text)
{
	char dir[1024];
	if(!GetEditorRootDirectory(dir, sizeof(dir)))
		return false;
	std::string base = dir;
	for(int up = 0; up < 5; up++){
		char path[1200];
		snprintf(path, sizeof(path), "%s/%s", base.c_str(), name);
		FILE *f = fopen(path, "rb");
		if(f){
			char buf[8192];
			size_t n;
			text.clear();
			while((n = fread(buf, 1, sizeof(buf), f)) > 0)
				text.append(buf, n);
			fclose(f);
			return true;
		}
		size_t slash = base.find_last_of("/\\");
		if(slash == std::string::npos)
			break;
		base.resize(slash);
	}
	return false;
}

void
DrawLicensesWindow(bool *open)
{
	static std::string texts[NUM_LICENSE_FILES];
	static bool loaded, found[NUM_LICENSE_FILES];
	if(!loaded){
		for(int i = 0; i < NUM_LICENSE_FILES; i++)
			found[i] = readLicenseFile(licenseFiles[i], texts[i]);
		loaded = true;
	}
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 42.0f, ImGui::GetFontSize() * 30.0f),
		ImGuiCond_FirstUseEver);
	if(!ImGui::Begin(_("Licenses###licenses"), open)){
		ImGui::End();
		return;
	}
	ImGui::TextWrapped(_("Haumea Studio is free software under the GNU General Public License, version 2 "
		"or any later version. It includes components under their own licenses."));
	if(ImGui::BeginTabBar("##license_files")){
		for(int i = 0; i < NUM_LICENSE_FILES; i++){
			if(!ImGui::BeginTabItem(licenseFiles[i]))
				continue;
			ImGui::BeginChild("##license_text", ImVec2(0, 0), ImGuiChildFlags_Borders);
			if(found[i])
				ImGui::TextUnformatted(texts[i].c_str(), texts[i].c_str() + texts[i].size());
			else
				ImGui::TextWrapped(_("%s isn't next to the editor. It ships with every release, and is "
					"in the source at https://github.com/itsneufox/haumea."), licenseFiles[i]);
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}
