#include "updatecore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <algorithm>
#include <map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include "src/lodepng/lodepng.h"

// Integrity, not authenticity: a download is checked against SHA256SUMS
// from the same release, which catches a broken or truncated download but
// not a release that was replaced as a whole. Signing the sums would close
// that; for now the release itself is trusted (HTTPS to GitHub).

// ---- SHA-256 (FIPS 180-4) ----

namespace {

struct Sha256 {
	uint32_t h[8];
	uint64_t length = 0;
	uint8_t block[64];
	size_t used = 0;

	Sha256(void){
		static const uint32_t init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
		memcpy(h, init, sizeof(h));
	}
	static uint32_t rotr(uint32_t x, int n){ return (x >> n) | (x << (32 - n)); }
	void compress(const uint8_t *p){
		static const uint32_t k[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
		uint32_t w[64];
		for(int i = 0; i < 16; i++)
			w[i] = (uint32_t)p[i*4] << 24 | (uint32_t)p[i*4+1] << 16 | (uint32_t)p[i*4+2] << 8 | p[i*4+3];
		for(int i = 16; i < 64; i++){
			uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
			uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
			w[i] = w[i-16] + s0 + w[i-7] + s1;
		}
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
		for(int i = 0; i < 64; i++){
			uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
			uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
	}
	void update(const void *data, size_t size){
		const uint8_t *p = (const uint8_t*)data;
		length += size;
		while(size > 0){
			size_t n = (std::min)(size, 64 - used);	// windows.h defines min
			memcpy(block + used, p, n);
			used += n; p += n; size -= n;
			if(used == 64){
				compress(block);
				used = 0;
			}
		}
	}
	std::string hex(void){
		uint64_t bits = length * 8;
		uint8_t pad = 0x80;
		update(&pad, 1);
		uint8_t zero = 0;
		while(used != 56)
			update(&zero, 1);
		uint8_t len[8];
		for(int i = 0; i < 8; i++)
			len[i] = (uint8_t)(bits >> (56 - 8*i));
		update(len, 8);
		static const char digits[] = "0123456789abcdef";
		std::string out;
		for(int i = 0; i < 8; i++)
			for(int s = 28; s >= 0; s -= 4)
				out += digits[(h[i] >> s) & 15];
		return out;
	}
};

}	// namespace

std::string
Sha256Hex(const void *data, size_t size)
{
	Sha256 sha;
	sha.update(data, size);
	return sha.hex();
}

bool
Sha256File(const std::string &path, std::string *hex)
{
	FILE *f = fopen(path.c_str(), "rb");
	if(f == nullptr)
		return false;
	Sha256 sha;
	char buf[65536];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		sha.update(buf, n);
	bool ok = !ferror(f);
	fclose(f);
	if(ok)
		*hex = sha.hex();
	return ok;
}

// ---- A small JSON reader, enough for GitHub's release list ----

namespace {

struct Json {
	enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
	std::string str;
	std::vector<Json> items;							// ARR
	std::vector<std::pair<std::string, Json>> fields;	// OBJ
	bool boolean = false;

	const Json *get(const char *key) const {
		for(const auto &f : fields)
			if(f.first == key)
				return &f.second;
		return nullptr;
	}
	std::string text(const char *key) const {
		const Json *v = get(key);
		return v && v->type == STR ? v->str : std::string();
	}
	bool flag(const char *key) const {
		const Json *v = get(key);
		return v && v->type == BOOL && v->boolean;
	}
};

struct JsonParser {
	const char *p, *end;
	int depth = 0;

	void ws(void){ while(p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
	bool lit(const char *word){
		size_t n = strlen(word);
		if((size_t)(end - p) < n || memcmp(p, word, n) != 0) return false;
		p += n;
		return true;
	}
	static void utf8(std::string &out, uint32_t c){
		if(c < 0x80) out += (char)c;
		else if(c < 0x800){ out += (char)(0xC0 | c >> 6); out += (char)(0x80 | (c & 63)); }
		else if(c < 0x10000){ out += (char)(0xE0 | c >> 12); out += (char)(0x80 | (c >> 6 & 63)); out += (char)(0x80 | (c & 63)); }
		else{ out += (char)(0xF0 | c >> 18); out += (char)(0x80 | (c >> 12 & 63)); out += (char)(0x80 | (c >> 6 & 63)); out += (char)(0x80 | (c & 63)); }
	}
	bool hex4(uint32_t *c){
		if(end - p < 4) return false;
		*c = 0;
		for(int i = 0; i < 4; i++){
			char h = *p++;
			*c <<= 4;
			if(h >= '0' && h <= '9') *c |= h - '0';
			else if(h >= 'a' && h <= 'f') *c |= h - 'a' + 10;
			else if(h >= 'A' && h <= 'F') *c |= h - 'A' + 10;
			else return false;
		}
		return true;
	}
	bool string(std::string &out){
		if(p >= end || *p != '"') return false;
		p++;
		while(p < end && *p != '"'){
			if(*p != '\\'){
				out += *p++;
				continue;
			}
			if(++p >= end) return false;
			char c = *p++;
			switch(c){
			case 'n': out += '\n'; break;
			case 't': out += '\t'; break;
			case 'r': break;
			case 'b': case 'f': break;
			case 'u': {
				uint32_t cp;
				if(!hex4(&cp)) return false;
				if(cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u'){
					p += 2;
					uint32_t lo;
					if(!hex4(&lo)) return false;
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				}
				utf8(out, cp);
				break;
			}
			default: out += c; break;	// \" \\ \/
			}
		}
		if(p >= end) return false;
		p++;
		return true;
	}
	bool value(Json &v){
		if(++depth > 64) return false;
		ws();
		if(p >= end) return false;
		bool ok = true;
		if(*p == '{'){
			v.type = Json::OBJ;
			p++; ws();
			if(p < end && *p == '}') p++;
			else for(;;){
				std::string key;
				ws();
				if(!string(key)) return false;
				ws();
				if(p >= end || *p++ != ':') return false;
				v.fields.emplace_back(key, Json());
				if(!value(v.fields.back().second)) return false;
				ws();
				if(p < end && *p == ','){ p++; continue; }
				if(p < end && *p == '}'){ p++; break; }
				return false;
			}
		}else if(*p == '['){
			v.type = Json::ARR;
			p++; ws();
			if(p < end && *p == ']') p++;
			else for(;;){
				v.items.emplace_back();
				if(!value(v.items.back())) return false;
				ws();
				if(p < end && *p == ','){ p++; continue; }
				if(p < end && *p == ']'){ p++; break; }
				return false;
			}
		}else if(*p == '"'){
			v.type = Json::STR;
			ok = string(v.str);
		}else if(lit("true")){ v.type = Json::BOOL; v.boolean = true; }
		else if(lit("false")){ v.type = Json::BOOL; }
		else if(lit("null")){ v.type = Json::NUL; }
		else{
			v.type = Json::NUM;
			const char *start = p;
			while(p < end && (strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) p++;
			ok = p > start;
			v.str.assign(start, p - start);	// the number's text
		}
		depth--;
		return ok;
	}
};

}	// namespace

const UpdateAsset*
UpdateRelease::Find(const char *name) const
{
	for(const UpdateAsset &a : assets)
		if(a.name == name)
			return &a;
	return nullptr;
}

bool
UpdatePickRelease(const std::string &json, const std::vector<std::string> &channels,
	const HaumeaVersion &current, const char *skipVersion, bool allowOlder, bool allowEqual,
	UpdateRelease *out, std::string *error)
{
	Json root;
	JsonParser parser = { json.data(), json.data() + json.size() };
	if(!parser.value(root) || root.type != Json::ARR){
		*error = "the release list isn't valid JSON";
		return false;
	}
	bool found = false;
	for(const Json &r : root.items){
		if(r.type != Json::OBJ || r.flag("draft"))
			continue;
		std::string tag = r.text("tag_name");
		char tagChannel[64];
		HaumeaVersion v;
		if(!HaumeaTagChannel(tag.c_str(), tagChannel, sizeof(tagChannel)) ||
		   std::find(channels.begin(), channels.end(), std::string(tagChannel)) == channels.end() ||
		   !ParseHaumeaVersion(tag.c_str(), &v))
			continue;
		int cmp = CompareHaumeaVersion(v, current);
		if((cmp < 0 && !allowOlder) || (cmp == 0 && !allowEqual && !allowOlder))
			continue;
		char version[64];
		snprintf(version, sizeof(version), "%d.%d.%d.%d", v.part[0], v.part[1], v.part[2], v.part[3]);
		if(skipVersion && strcmp(version, skipVersion) == 0)
			continue;
		if(found && CompareHaumeaVersion(v, out->parsed) <= 0)
			continue;
		UpdateRelease rel;
		rel.tag = tag;
		rel.version = version;
		rel.body = r.text("body");
		rel.pageUrl = r.text("html_url");
		rel.parsed = v;
		if(const Json *assets = r.get("assets"))
			for(const Json &a : assets->items)
				if(a.type == Json::OBJ){
					const Json *size = a.get("size");
					rel.assets.push_back({ a.text("name"), a.text("browser_download_url"),
						size && size->type == Json::NUM ? atoll(size->str.c_str()) : 0 });
				}
		*out = rel;
		found = true;
	}
	return found;
}

bool
UpdateSumFor(const std::string &sums, const char *name, std::string *hex)
{
	size_t at = 0;
	while(at < sums.size()){
		size_t eol = sums.find('\n', at);
		if(eol == std::string::npos) eol = sums.size();
		std::string line = sums.substr(at, eol - at);
		at = eol + 1;
		if(!line.empty() && line.back() == '\r') line.pop_back();
		if(line.size() < 66 || line[64] != ' ')
			continue;
		std::string file = line.substr(65);
		if(!file.empty() && (file[0] == ' ' || file[0] == '*'))	// text or binary mode
			file = file.substr(1);
		if(file != name)
			continue;
		std::string h = line.substr(0, 64);
		for(char &c : h){
			if(c >= 'A' && c <= 'F') c += 'a' - 'A';
			if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
				return false;
		}
		*hex = h;
		return true;
	}
	return false;
}

const char*
UpdateBundleName(void)
{
#if defined(_WIN32) && defined(RW_D3D9)
	return "haumea-windows-d3d9.tar.gz";
#elif defined(_WIN32)
	return "haumea-windows-gl3.tar.gz";
#elif defined(__APPLE__)
	return nullptr;	// notify only: an app bundle is replaced as a whole and signed
#elif defined(__linux__) && defined(__x86_64__)
	return "haumea-linux-amd64.tar.gz";
#else
	return nullptr;
#endif
}

// ---- Files ----

bool
UpdateFileExists(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

bool
UpdateIsDirectory(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static bool
makeDir(const std::string &path)
{
#ifdef _WIN32
	return _mkdir(path.c_str()) == 0 || UpdateIsDirectory(path);
#else
	return mkdir(path.c_str(), 0755) == 0 || UpdateIsDirectory(path);
#endif
}

bool
UpdateMakeDirs(const std::string &path)
{
	for(size_t i = 1; i <= path.size(); i++)
		if(i == path.size() || path[i] == '/' || path[i] == '\\'){
			std::string part = path.substr(0, i);
			if(part.size() == 2 && part[1] == ':')
				continue;	// a Windows drive
			if(!makeDir(part))
				return false;
		}
	return true;
}

static std::string
parentOf(const std::string &path)
{
	size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

bool
UpdateReadFile(const std::string &path, std::string *out)
{
	FILE *f = fopen(path.c_str(), "rb");
	if(f == nullptr)
		return false;
	out->clear();
	char buf[65536];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out->append(buf, n);
	bool ok = !ferror(f);
	fclose(f);
	return ok;
}

bool
UpdateWriteFile(const std::string &path, const std::string &data)
{
	FILE *f = fopen(path.c_str(), "wb");
	if(f == nullptr)
		return false;
	bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	ok = fclose(f) == 0 && ok;
	return ok;
}

static bool
copyFile(const std::string &src, const std::string &dst)
{
	FILE *in = fopen(src.c_str(), "rb");
	if(in == nullptr)
		return false;
	FILE *out = fopen(dst.c_str(), "wb");
	if(out == nullptr){
		fclose(in);
		return false;
	}
	char buf[65536];
	size_t n;
	bool ok = true;
	while(ok && (n = fread(buf, 1, sizeof(buf), in)) > 0)
		ok = fwrite(buf, 1, n, out) == n;
	ok = !ferror(in) && ok;
	fclose(in);
	ok = fclose(out) == 0 && ok;
#ifndef _WIN32
	struct stat st;
	if(ok && stat(src.c_str(), &st) == 0)
		chmod(dst.c_str(), st.st_mode & 0777);
#endif
	if(!ok)
		remove(dst.c_str());
	return ok;
}

bool
UpdateMoveFile(const std::string &src, const std::string &dst)
{
#ifdef _WIN32
	if(MoveFileExA(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return true;
#else
	if(rename(src.c_str(), dst.c_str()) == 0)
		return true;
#endif
	// Across file systems (the config folder to the install folder): copy
	// next to dst, then rename it into place, so dst is never half written.
	std::string temp = dst + ".haumea-tmp";
	if(!copyFile(src, temp))
		return false;
#ifdef _WIN32
	bool ok = MoveFileExA(temp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
	bool ok = rename(temp.c_str(), dst.c_str()) == 0;
#endif
	if(!ok){
		remove(temp.c_str());
		return false;
	}
	remove(src.c_str());
	return true;
}

static void
listDir(const std::string &dir, std::vector<std::string> *names)
{
#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "/*").c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE)
		return;
	do{
		if(strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0)
			names->push_back(fd.cFileName);
	}while(FindNextFileA(h, &fd));
	FindClose(h);
#else
	DIR *d = opendir(dir.c_str());
	if(d == nullptr)
		return;
	while(struct dirent *e = readdir(d))
		if(strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0)
			names->push_back(e->d_name);
	closedir(d);
#endif
}

// Only ever called on folders the updater made itself (staging, the
// unpacked bundle, .haumea-previous), never on the install folder.
bool
UpdateRemoveTree(const std::string &path)
{
#ifndef _WIN32
	struct stat st;
	if(lstat(path.c_str(), &st) != 0)
		return true;
	if((st.st_mode & S_IFMT) != S_IFDIR)
		return remove(path.c_str()) == 0;
#else
	if(!UpdateFileExists(path))
		return true;
	if(!UpdateIsDirectory(path))
		return remove(path.c_str()) == 0;
#endif
	std::vector<std::string> names;
	listDir(path, &names);
	bool ok = true;
	for(const std::string &n : names)
		ok = UpdateRemoveTree(path + "/" + n) && ok;
#ifdef _WIN32
	return _rmdir(path.c_str()) == 0 && ok;
#else
	return rmdir(path.c_str()) == 0 && ok;
#endif
}

bool
UpdateDirWritable(const std::string &dir)
{
	std::string probe = dir + "/.haumea-write-test";
	FILE *f = fopen(probe.c_str(), "wb");
	if(f == nullptr)
		return false;
	fclose(f);
	remove(probe.c_str());
	return true;
}

// ---- Unpacking ----

// A path from an archive or a manifest that stays inside its folder:
// relative, '/'-separated, no empty, "." or ".." parts, no drive or '\'.
static bool
safeRelativePath(const std::string &path)
{
	if(path.empty() || path.size() > 255 || path[0] == '/' || path.find('\\') != std::string::npos ||
	   path.find(':') != std::string::npos)
		return false;
	size_t at = 0;
	while(at <= path.size()){
		size_t slash = path.find('/', at);
		if(slash == std::string::npos) slash = path.size();
		std::string part = path.substr(at, slash - at);
		if(part.empty() || part == "." || part == "..")
			return false;
		at = slash + 1;
	}
	for(unsigned char c : path)
		if(c < 0x20)
			return false;
	return true;
}

static uint32_t
crc32Of(const unsigned char *data, size_t size)
{
	static uint32_t table[256];
	if(table[1] == 0)
		for(uint32_t i = 0; i < 256; i++){
			uint32_t c = i;
			for(int k = 0; k < 8; k++)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	uint32_t c = 0xFFFFFFFFu;
	for(size_t i = 0; i < size; i++)
		c = table[(c ^ data[i]) & 255] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

// One gzip member into out, at most maxBytes.
static bool
gunzip(const std::string &in, size_t maxBytes, std::string *out, std::string *error)
{
	const unsigned char *p = (const unsigned char*)in.data();
	size_t n = in.size();
	if(n < 18 || p[0] != 0x1f || p[1] != 0x8b || p[2] != 8){
		*error = "not a gzip file";
		return false;
	}
	int flags = p[3];
	size_t at = 10;
	if(flags & 4){	// FEXTRA
		if(at + 2 > n) goto broken;
		at += 2 + (p[at] | p[at+1] << 8);
	}
	if(flags & 8){	// FNAME
		while(at < n && p[at]) at++;
		at++;
	}
	if(flags & 16){	// FCOMMENT
		while(at < n && p[at]) at++;
		at++;
	}
	if(flags & 2)	// FHCRC
		at += 2;
	if(at + 8 > n) goto broken;
	{
		uint32_t crc = p[n-8] | p[n-7] << 8 | p[n-6] << 16 | (uint32_t)p[n-5] << 24;
		uint32_t isize = p[n-4] | p[n-3] << 8 | p[n-2] << 16 | (uint32_t)p[n-1] << 24;
		// lodepng has no output limit, so the size the trailer claims is
		// checked first (the download's sha256 was checked before this).
		if(isize > maxBytes){
			*error = "the archive is damaged or larger than allowed";
			return false;
		}
		unsigned char *data = nullptr;
		size_t size = 0;
		LodePNGDecompressSettings settings;
		lodepng_decompress_settings_init(&settings);
		unsigned err = lodepng_inflate(&data, &size, p + at, n - 8 - at, &settings);
		if(err != 0 || size != isize || crc32Of(data, size) != crc){
			free(data);
			goto broken;
		}
		out->assign((const char*)data, size);
		free(data);
		return true;
	}
broken:
	*error = "the archive is damaged";
	return false;
}

static bool
octal(const char *field, int len, uint64_t *out)
{
	*out = 0;
	int i = 0;
	while(i < len && field[i] == ' ') i++;
	bool any = false;
	for(; i < len && field[i] >= '0' && field[i] <= '7'; i++){
		*out = *out * 8 + (field[i] - '0');
		any = true;
	}
	for(; i < len; i++)
		if(field[i] != ' ' && field[i] != '\0')
			return false;
	return any;
}

bool
UpdateUnpackTarGz(const std::string &archive, const std::string &dir, size_t maxBytes, std::string *error)
{
	std::string gz, tar;
	if(!UpdateReadFile(archive, &gz)){
		*error = "can't read " + archive;
		return false;
	}
	if(!gunzip(gz, maxBytes, &tar, error))
		return false;
	gz.clear();
	if(UpdateFileExists(dir) || !UpdateMakeDirs(dir)){
		*error = "can't make " + dir;
		return false;
	}
	size_t at = 0, total = 0;
	int entries = 0;
	bool ended = false;
	while(at + 512 <= tar.size()){
		const char *h = tar.data() + at;
		static const char zeros[512] = {};
		if(memcmp(h, zeros, 512) == 0){
			ended = true;
			break;
		}
		uint64_t sum, size;
		if(!octal(h + 148, 8, &sum) || !octal(h + 124, 12, &size)){
			*error = "the archive has a bad header";
			goto fail;
		}
		{
			uint64_t check = 0;
			for(int i = 0; i < 512; i++)
				check += (i >= 148 && i < 156) ? ' ' : (unsigned char)h[i];
			if(check != sum){
				*error = "the archive has a bad header checksum";
				goto fail;
			}
		}
		{
			std::string name(h, strnlen(h, 100));
			if(memcmp(h + 257, "ustar", 5) == 0 && h[345]){
				std::string prefix(h + 345, strnlen(h + 345, 155));
				name = prefix + "/" + name;
			}
			char type = h[156];
			bool isDir = type == '5';
			if(isDir && !name.empty() && name.back() == '/')
				name.pop_back();
			// Only plain files and folders: links, devices, FIFOs and
			// extended headers are refused, not skipped.
			if(!(type == '0' || type == '\0' || isDir)){
				*error = "the archive holds a link or special file (" + name + ")";
				goto fail;
			}
			if(!safeRelativePath(name)){
				*error = "the archive holds an unsafe path (" + name + ")";
				goto fail;
			}
			if(++entries > 20000 || (total += size) > maxBytes){
				*error = "the archive is larger than allowed";
				goto fail;
			}
			at += 512;
			if(at + size > tar.size()){
				*error = "the archive is cut short";
				goto fail;
			}
			std::string target = dir + "/" + name;
			if(isDir){
				if(!UpdateMakeDirs(target)){
					*error = "can't make " + target;
					goto fail;
				}
			}else{
				if(!UpdateMakeDirs(parentOf(target)) || UpdateIsDirectory(target) ||
				   !UpdateWriteFile(target, tar.substr(at, size))){
					*error = "can't write " + target;
					goto fail;
				}
#ifndef _WIN32
				uint64_t mode;
				if(octal(h + 100, 8, &mode))
					chmod(target.c_str(), (mode & 0111) ? 0755 : 0644);
#endif
			}
			at += (size + 511) / 512 * 512;
		}
	}
	if(!ended){
		*error = "the archive is cut short";
		goto fail;
	}
	return true;
fail:
	UpdateRemoveTree(dir);
	return false;
}

// ---- Manifests ----

bool
UpdateReadManifest(const std::string &file, std::vector<ManifestEntry> *out, std::string *error)
{
	std::string text;
	if(!UpdateReadFile(file, &text)){
		*error = "can't read " + file;
		return false;
	}
	out->clear();
	size_t at = 0;
	while(at < text.size()){
		size_t eol = text.find('\n', at);
		if(eol == std::string::npos) eol = text.size();
		std::string line = text.substr(at, eol - at);
		at = eol + 1;
		if(!line.empty() && line.back() == '\r') line.pop_back();
		if(line.empty())
			continue;
		if(line.size() < 67 || line[64] != ' ' || line[65] != ' '){
			*error = "bad manifest line: " + line;
			return false;
		}
		ManifestEntry e = { line.substr(66), line.substr(0, 64) };
		// The updater's own files are never manifest entries.
		if(!safeRelativePath(e.path) || e.path.compare(0, 16, ".haumea-previous") == 0 ||
		   e.path == ".haumea-manifest"){
			*error = "unsafe manifest path: " + e.path;
			return false;
		}
		out->push_back(e);
	}
	if(out->empty()){
		*error = "the manifest is empty";
		return false;
	}
	return true;
}

// ---- Installing ----

// installDir/.haumea-previous/journal lists, in order, what the install did:
//   moved <path>   the file that was there is in .haumea-previous/files/<path>
//   added <path>   nothing was there; the new file was added
//   local <path>   the file was edited locally and kept as <path>.local
// Rollback undoes them in reverse.

static const char *kPrevious = ".haumea-previous";

static bool
journalLine(FILE *journal, const char *what, const std::string &path)
{
	return fprintf(journal, "%s %s\n", what, path.c_str()) > 0 && fflush(journal) == 0;
}

// Undo a journal's lines, last first.
static bool
undoJournal(const std::string &installDir, const std::vector<std::pair<std::string, std::string>> &lines,
	std::string *error)
{
	std::string prev = installDir + "/" + kPrevious;
	bool ok = true;
	for(size_t i = lines.size(); i-- > 0;){
		const std::string &what = lines[i].first, &path = lines[i].second;
		std::string target = installDir + "/" + path;
		if(what == "moved")
			ok = UpdateMoveFile(prev + "/files/" + path, target) && ok;
		else if(what == "added")
			ok = (!UpdateFileExists(target) || remove(target.c_str()) == 0) && ok;
		else if(what == "local")
			ok = UpdateMoveFile(target + ".local", target) && ok;
		if(!ok && error->empty())
			*error = "couldn't restore " + path;
	}
	return ok;
}

static bool
readJournal(const std::string &installDir, std::vector<std::pair<std::string, std::string>> *lines)
{
	std::string text;
	if(!UpdateReadFile(installDir + "/" + kPrevious + "/journal", &text))
		return false;
	size_t at = 0;
	while(at < text.size()){
		size_t eol = text.find('\n', at);
		if(eol == std::string::npos) eol = text.size();
		std::string line = text.substr(at, eol - at);
		at = eol + 1;
		size_t space = line.find(' ');
		if(space == std::string::npos)
			continue;
		std::string path = line.substr(space + 1);
		if(!safeRelativePath(path))
			return false;
		lines->push_back({ line.substr(0, space), path });
	}
	return true;
}

bool
UpdateInstall(const std::string &staged, const std::string &installDir, UpdateInstallResult *result,
	std::string *error)
{
	std::vector<ManifestEntry> fresh, old;
	if(!UpdateReadManifest(staged + "/MANIFEST", &fresh, error))
		return false;
	// Every new file is checked against the manifest before anything moves.
	for(const ManifestEntry &e : fresh){
		std::string hex;
		if(!Sha256File(staged + "/" + e.path, &hex) || hex != e.sha256){
			*error = "the update's " + e.path + " doesn't match its manifest";
			return false;
		}
	}
	std::string oldError;
	bool haveOld = UpdateFileExists(installDir + "/.haumea-manifest") &&
		UpdateReadManifest(installDir + "/.haumea-manifest", &old, &oldError);
	std::map<std::string, std::string> oldHash;
	for(const ManifestEntry &e : old)
		oldHash[e.path] = e.sha256;
	std::map<std::string, bool> inFresh;
	for(const ManifestEntry &e : fresh)
		inFresh[e.path] = true;

	// One previous version is kept: the folder is the updater's own.
	std::string prev = installDir + "/" + kPrevious;
	UpdateRemoveTree(prev);
	if(!UpdateMakeDirs(prev + "/files")){
		*error = "can't make " + prev;
		return false;
	}
	FILE *journal = fopen((prev + "/journal").c_str(), "wb");
	if(journal == nullptr){
		*error = "can't write the install journal";
		return false;
	}
	std::vector<std::pair<std::string, std::string>> done;
	auto note = [&](const char *what, const std::string &path){
		done.push_back({ what, path });
		return journalLine(journal, what, path);
	};
	// Edited locally: the old manifest lists it, and it no longer matches.
	auto edited = [&](const std::string &path){
		auto it = oldHash.find(path);
		std::string hex;
		return it != oldHash.end() && Sha256File(installDir + "/" + path, &hex) && hex != it->second;
	};
	*result = UpdateInstallResult();
	bool ok = true;

	// Shipped by the old version but not the new one: moved aside unless
	// the user changed it, then it is theirs and stays.
	for(const ManifestEntry &e : old){
		std::string target = installDir + "/" + e.path;
		if(inFresh.count(e.path) || !UpdateFileExists(target) || edited(e.path))
			continue;
		std::string keep = prev + "/files/" + e.path;
		ok = UpdateMakeDirs(parentOf(keep)) && UpdateMoveFile(target, keep) && note("moved", e.path);
		if(!ok){ *error = "couldn't move " + e.path; break; }
		result->removed++;
	}
	for(size_t i = 0; ok && i < fresh.size(); i++){
		const ManifestEntry &e = fresh[i];
		std::string target = installDir + "/" + e.path;
		if(UpdateIsDirectory(target)){
			ok = false;
			*error = e.path + " is a folder in the install folder";
			break;
		}
		if(UpdateFileExists(target)){
			if(edited(e.path)){
				// An older .local (from an earlier update) goes aside first,
				// so the user's earlier copy isn't overwritten.
				std::string local = e.path + ".local";
				if(UpdateFileExists(installDir + "/" + local)){
					std::string keep = prev + "/files/" + local;
					ok = UpdateMakeDirs(parentOf(keep)) && UpdateMoveFile(installDir + "/" + local, keep) &&
						note("moved", local);
				}
				ok = ok && UpdateMoveFile(target, target + ".local") && note("local", e.path);
				result->keptLocal.push_back(e.path);
			}else{
				std::string keep = prev + "/files/" + e.path;
				ok = UpdateMakeDirs(parentOf(keep)) && UpdateMoveFile(target, keep) && note("moved", e.path);
				result->replaced++;
			}
		}else{
			ok = UpdateMakeDirs(parentOf(target)) && note("added", e.path);
			result->added++;
		}
		ok = ok && UpdateMoveFile(staged + "/" + e.path, target);
		if(!ok && error->empty())
			*error = "couldn't install " + e.path;
	}
	// The new manifest last: until it's in place the old one describes the folder.
	if(ok){
		std::string target = installDir + "/.haumea-manifest";
		if(haveOld || UpdateFileExists(target)){
			ok = UpdateMoveFile(target, prev + "/files/.haumea-manifest") && note("moved", ".haumea-manifest");
		}else
			ok = note("added", ".haumea-manifest");
		ok = ok && UpdateMoveFile(staged + "/MANIFEST", target);
		if(!ok && error->empty())
			*error = "couldn't write the manifest";
	}
	fclose(journal);
	if(!ok){
		std::string undoError;
		undoJournal(installDir, done, &undoError);
		UpdateRemoveTree(prev);
		if(!undoError.empty())
			*error += "; " + undoError;
		return false;
	}
	return true;
}

bool
UpdateCanRollback(const std::string &installDir)
{
	return UpdateFileExists(installDir + "/" + kPrevious + "/journal");
}

bool
UpdateRollback(const std::string &installDir, std::string *error)
{
	std::vector<std::pair<std::string, std::string>> lines;
	if(!readJournal(installDir, &lines)){
		*error = "there is no previous version to go back to";
		return false;
	}
	if(!undoJournal(installDir, lines, error))
		return false;
	UpdateRemoveTree(installDir + "/" + kPrevious);
	return true;
}
