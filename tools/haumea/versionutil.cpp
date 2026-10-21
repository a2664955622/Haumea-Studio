// SPDX-License-Identifier: GPL-2.0-or-later
#include "versionutil.h"
#include "version.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// The build's number and commit; a build without the generated header (an
// IDE that skipped the pre-build step) is build 0 of an unknown commit.
#if defined(__has_include)
#if __has_include("haumea_buildinfo.h")
#include "haumea_buildinfo.h"
#endif
#endif
#ifndef HAUMEA_BUILD
#define HAUMEA_BUILD 0
#endif
#ifndef HAUMEA_COMMIT
#define HAUMEA_COMMIT "unknown"
#endif

bool
ParseHaumeaVersion(const char *text, HaumeaVersion *out)
{
	HaumeaVersion v = { { 0, 0, 0, 0 } };
	if(text == nullptr)
		return false;
	const char *p = text;
	if(*p == 'v' || *p == 'V')
		p++;
	int n = 0;
	for(;;){
		if(!isdigit((unsigned char)*p) || n == 4)
			return false;
		long value = 0;
		while(isdigit((unsigned char)*p)){
			value = value * 10 + (*p++ - '0');
			if(value > 1000000000)
				return false;
		}
		v.part[n++] = (int)value;
		if(*p == '.'){
			p++;
			continue;
		}
		if(*p == '\0' || *p == '-')
			break;
		return false;
	}
	if(out)
		*out = v;
	return true;
}

int
CompareHaumeaVersion(const HaumeaVersion &a, const HaumeaVersion &b)
{
	for(int i = 0; i < 4; i++)
		if(a.part[i] != b.part[i])
			return a.part[i] < b.part[i] ? -1 : 1;
	return 0;
}

bool
HaumeaTagChannel(const char *tag, char *out, int size)
{
	const char *dash = tag ? strchr(tag, '-') : nullptr;
	if(dash == nullptr || dash[1] == '\0' || size <= 0)
		return false;
	const char *begin = dash + 1;
	size_t len = strlen(begin);
	// An old tag's trailing commit: the last '-' followed by 7-40 hex digits.
	const char *last = strrchr(begin, '-');
	if(last){
		size_t hex = strlen(last + 1);
		bool isHex = hex >= 7 && hex <= 40;
		for(size_t i = 0; isHex && i < hex; i++)
			isHex = isxdigit((unsigned char)last[1 + i]) != 0;
		if(isHex)
			len = (size_t)(last - begin);
	}
	if(len == 0 || len >= (size_t)size)
		return false;
	memcpy(out, begin, len);
	out[len] = '\0';
	return true;
}

const HaumeaVersion &
HaumeaCurrentVersion(void)
{
	static const HaumeaVersion v = { { HAUMEA_VERSION_MAJOR, HAUMEA_VERSION_MINOR, HAUMEA_VERSION_PATCH,
		HAUMEA_BUILD } };
	return v;
}

int
HaumeaBuildNumber(void)
{
	return HAUMEA_BUILD;
}

const char *
HaumeaCommit(void)
{
	return HAUMEA_COMMIT;
}

const char *
HaumeaFullVersion(void)
{
	static char text[48];
	if(text[0] == '\0')
		snprintf(text, sizeof(text), "%s.%d", HAUMEA_VERSION, HAUMEA_BUILD);
	return text;
}

const char *
HaumeaVersionDisplay(void)
{
	static char text[96];
	if(text[0] == '\0')
		snprintf(text, sizeof(text), "%s (%s)", HaumeaFullVersion(), HAUMEA_COMMIT);
	return text;
}
