#pragma once
#ifndef SAMP_LOADER_H
#define SAMP_LOADER_H

namespace SampLoader {

int DiscoverAndRegisterImages(void);

void LoadDefinitions(void);

void ExtendCategories(void);


int GetSampRootCategoryIndex(void);

int GetSampUncatCategoryIndex(void);

}

#endif // SAMP_LOADER_H
