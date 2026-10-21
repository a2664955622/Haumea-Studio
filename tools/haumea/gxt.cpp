#include "haumea.h"
#include "gxt.h"
#include "workspace.h"

#include <string>
#include <unordered_map>

// SA's format: a version (4) and character size (8), then TABL, the tables
// by name and offset. MAIN's TKEY lists (string offset, key hash) pairs and
// its TDAT holds the strings. The hash is CRC-32 of the upper-case key
// without the final inversion.

namespace {

std::unordered_map<uint32, std::string> gStrings;
bool gRead;

uint32
keyHash(const char *key)
{
	static uint32 table[256];
	if(table[1] == 0)
		for(uint32 i = 0; i < 256; i++){
			uint32 c = i;
			for(int k = 0; k < 8; k++)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	uint32 crc = 0xFFFFFFFFu;
	for(const char *p = key; *p; p++)
		crc = table[(crc ^ (uint8)toupper((unsigned char)*p)) & 0xFF] ^ (crc >> 8);
	return crc;
}

uint32
read32(const std::vector<uint8> &d, size_t at)
{
	return at + 4 <= d.size() ? d[at] | d[at+1] << 8 | d[at+2] << 16 | (uint32)d[at+3] << 24 : 0;
}

void
readGxt(void)
{
	gRead = true;
	FILE *f = fopen(GamePath("text/american.gxt").c_str(), "rb");
	if(f == nil)
		return;
	std::vector<uint8> d;
	uint8 buf[65536];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		d.insert(d.end(), buf, buf + n);
	fclose(f);
	if(d.size() < 20 || memcmp(&d[4], "TABL", 4) != 0 || (d[2] | d[3] << 8) != 8)
		return;	// not SA's 8-bit format
	// MAIN is the first table; it has no name before its TKEY.
	size_t keys = read32(d, 20);
	if(keys + 8 > d.size() || memcmp(&d[keys], "TKEY", 4) != 0)
		return;
	uint32 keysSize = read32(d, keys + 4);
	size_t data = keys + 8 + keysSize;
	if(data + 8 > d.size() || memcmp(&d[data], "TDAT", 4) != 0)
		return;
	data += 8;
	for(uint32 i = 0; i + 8 <= keysSize; i += 8){
		uint32 offset = read32(d, keys + 8 + i), hash = read32(d, keys + 12 + i);
		std::string text;
		for(size_t at = data + offset; at < d.size() && d[at]; at++)
			text += (char)d[at];
		gStrings[hash] = text;
	}
}

}	// namespace

const char*
GxtLookup(const char *key)
{
	if(!gRead)
		readGxt();
	auto it = gStrings.find(keyHash(key));
	return it != gStrings.end() ? it->second.c_str() : "";
}
