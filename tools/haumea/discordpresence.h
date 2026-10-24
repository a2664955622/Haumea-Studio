// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdio.h>
void DiscordPresenceTick();
void DiscordPresenceShutdown();
bool DiscordPresencePreferences();
bool DiscordPresenceLoadSetting(const char *key, const char *value);
void DiscordPresenceSaveSettings(FILE *file);
