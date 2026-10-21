// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef SAMP_LOADER_H
#define SAMP_LOADER_H

namespace SampLoader {

int DiscoverAndRegisterImages(void);

void LoadDefinitions(void);

void ExtendCategories(void);

// One of SA-MP's own models (from SAMP.ide and its IMG).
bool IsSampModel(int id);


int GetSampRootCategoryIndex(void);

int GetSampUncatCategoryIndex(void);

}

#endif // SAMP_LOADER_H
