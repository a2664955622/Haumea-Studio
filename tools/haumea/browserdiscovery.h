// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
struct ObjectInst;
void BrowserFindInstances(int model);
void BrowserFindInstanceQuery(const char *query);
void BrowserFindSimilar(int model);
bool BrowserDiscoveryActive(void);
bool BrowserDiscoveryShowsModels(void);
void BrowserDiscoveryDraw(void (*drawModels)(int *, int, int), int selectedModel);
void BrowserDiscoveryHighlight(bool enabled);
int BrowserDiscoveryCount(int pawnModel);
bool BrowserDiscoveryIsMatch(const ObjectInst *inst);
