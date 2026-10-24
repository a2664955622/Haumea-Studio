// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "editorxml.h"
#include <unordered_map>
struct ObjectInst;
struct RadarArtLayer;
struct TextDraw;
struct TextDrawGuide;
struct PawnAreaRemoval;

void BeginMapXMLExport();
void CaptureMapXMLExport(const std::vector<ObjectInst *> &objects,
			 const std::unordered_map<const ObjectInst *, std::string> &handles, int format,
			 bool selectedOnly);
EditorXML::Node FinishMapXMLExport();
bool ApplyMapXML(const EditorXML::Node &document, bool sameRevision, std::vector<ObjectInst *> &created,
		 std::vector<RadarArtLayer> &radarLayers, size_t previousTextDraws,
		 const std::unordered_map<std::string, ObjectInst *> &handles, std::string *error);
int RestoreMapXMLWorkspace(const char *path);
bool CanSaveEditorDocument(const std::string &path, const char *kind, std::string *error);
EditorXML::Node BuildTextDrawXML(const std::vector<TextDraw> &textdraws, const std::vector<TextDrawGuide> &guides,
				 int encoding);
bool ReadTextDrawXML(const EditorXML::Node &document, bool sameRevision, std::vector<TextDraw> &textdraws,
		     std::vector<TextDrawGuide> &guides, std::string *error);
bool ReadMapXMLRemovals(const EditorXML::Node &document, std::vector<PawnAreaRemoval> &removals, std::string *error);
bool ValidateEditorDocument(const EditorXML::Node &document, std::string *error);
