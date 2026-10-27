// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "editorxml.h"
#include <memory>
#include <string>
#include <vector>
struct ImVec2;
void LayoutPathsPanel();
void LayoutPathsOverlay(bool enabled);
bool LayoutPathsHandleViewport();
bool LayoutPathsGizmo();
bool LayoutPathsOwnsMouse();
void ResetLayoutPaths();
EditorXML::Node WriteLayoutPathsXML();
bool ReadLayoutPathsXML(const EditorXML::Node &, std::string *error, bool apply = true);
void RestoreLayoutPaths(const std::shared_ptr<void> &);
int LayoutPathCount();
bool LayoutPathPointScreen(ImVec2 *, int index, int handle = 0); // 0 knot, 1 outgoing, 2 midpoint
float LayoutPathPointHeight(int index);
int LayoutPathObjectCount();
int LayoutPathUpsideDownCount();
bool LayoutPathObjectsMatch();
void LayoutPathsReferencedObjects(std::vector<int> &out);
