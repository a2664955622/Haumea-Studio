// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "workspace.h"
#include "samp_loader.h"
#include "object_categories.h"

#include <string.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace SampLoader {

static std::vector<std::string> sLoadedIdeFiles;
static std::vector<std::string> sLoadedImgFiles;
static std::vector<std::string> sLoadedColFiles;
static std::vector<std::string> sLoadedIplFiles;

static bool sSampObjectIds[NUMOBJECTDEFS];

static bool
sampBuildPath(char *dst, size_t size, const char *dir, const char *name)
{
	if(size == 0)
		return false;
	if(!dir || !dir[0])
		return snprintf(dst, size, "%s", name) < (int)size;
	size_t len = strlen(dir);
	const char *sep = (dir[len-1] == '/' || dir[len-1] == '\\') ? "" : "/";
	return snprintf(dst, size, "%s%s%s", dir, sep, name) < (int)size;
}

static bool
sampFileExists(const char *path)
{
	FILE *f = fopen_ci(path, "rb");
	if(f){ fclose(f); return true; }
	return false;
}

static void
sampNormPath(const char *in, char *out, size_t size)
{
	size_t i;
	for(i = 0; i < size-1 && in[i]; i++){
		char c = in[i];
		if(c == '\\') c = '/';
		if(c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
		out[i] = c;
	}
	out[i] = '\0';
}

static bool
sampAlreadySeen(const std::vector<std::string> &seen, const char *norm)
{
	for(const auto &s : seen)
		if(s == norm)
			return true;
	return false;
}

static void
sampScanDir(const char *dir, const char *ext, int maxDepth,
            std::vector<std::string> &results)
{
	if(maxDepth <= 0)
		return;

#ifdef _WIN32
	char pattern[1024];
	if(!sampBuildPath(pattern, sizeof(pattern), dir, "*"))
		return;

	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if(h == INVALID_HANDLE_VALUE)
		return;
	do {
		if(strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
			continue;
		char child[1024];
		if(!sampBuildPath(child, sizeof(child), dir, fd.cFileName))
			continue;
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				sampScanDir(child, ext, maxDepth-1, results);
		} else {
			const char *dot = strrchr(fd.cFileName, '.');
			if(dot && rw::strcmp_ci(dot+1, ext) == 0)
				results.push_back(child);
		}
	} while(FindNextFileA(h, &fd));
	FindClose(h);
#else
	DIR *d = opendir(dir);
	if(!d)
		return;
	struct dirent *de;
	while((de = readdir(d)) != nullptr){
		if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		char child[1024];
		if(!sampBuildPath(child, sizeof(child), dir, de->d_name))
			continue;
		struct stat st;
		if(stat(child, &st) != 0)
			continue;
		if(S_ISDIR(st.st_mode)){
			sampScanDir(child, ext, maxDepth-1, results);
		} else {
			const char *dot = strrchr(de->d_name, '.');
			if(dot && rw::strcmp_ci(dot+1, ext) == 0)
				results.push_back(child);
		}
	}
	closedir(d);
#endif
}

// ./<path inside the game folder>, for paths found by scanning it.
static void
sampRelativeToGame(std::vector<std::string> &paths)
{
	std::string game = GetGameDir();
	for(std::string &path : paths)
		if(!game.empty() && path.size() > game.size() && path.compare(0, game.size(), game) == 0 &&
		   (path[game.size()] == '/' || path[game.size()] == '\\'))
			path = "." + path.substr(game.size());
}

static const char *kProbeDirs[] = {
	"",
	"SAMP",
	"samp",
	"models",
	"MODELS",
	"data",
	"DATA",
	"data/maps",
	"DATA/maps",
	nullptr
};

static void
buildCandidates(std::vector<std::string> &out, const char *root, const char *filename)
{
	for(int i = 0; kProbeDirs[i] != nullptr; i++){
		char dir[1024];
		if(kProbeDirs[i][0])
			sampBuildPath(dir, sizeof(dir), root, kProbeDirs[i]);
		else
			strncpy(dir, root, sizeof(dir)-1);
		dir[sizeof(dir)-1] = '\0';
		char path[1024];
		sampBuildPath(path, sizeof(path), dir, filename);
		out.push_back(path);
	}
}

static std::string
findFirstExisting(const char *root, const char *filename)
{
	std::vector<std::string> cands;
	buildCandidates(cands, root, filename);
	char norm[1024];
	for(const auto &c : cands){
		sampNormPath(c.c_str(), norm, sizeof(norm));
		if(sampFileExists(norm))
			return std::string(norm);
	}
	return {};
}

static void
buildSampObjectIdSet(void)
{
	std::unordered_set<std::string> ideBasenames;
	for(const auto &ide : sLoadedIdeFiles){
		char norm[1024];
		sampNormPath(ide.c_str(), norm, sizeof(norm));
		const char *base = strrchr(norm, '/');
		ideBasenames.insert(base ? base+1 : norm);
		ideBasenames.insert(norm);
	}

	memset(sSampObjectIds, 0, sizeof(sSampObjectIds));
	for(int id = 0; id < NUMOBJECTDEFS; id++){
		ObjectDef *obj = GetObjectDef(id);
		if(!obj || !obj->m_file || !obj->m_file->name)
			continue;
		char normName[1024];
		sampNormPath(obj->m_file->name, normName, sizeof(normName));
		const char *baseName = strrchr(normName, '/');
		if(ideBasenames.count(baseName ? baseName+1 : normName) ||
		   ideBasenames.count(normName))
			sSampObjectIds[id] = true;
	}
}

static bool
isObjectFromSamp(int id)
{
	if(id < 0 || id >= NUMOBJECTDEFS) return false;
	return sSampObjectIds[id];
}

static const char *kImgNames[] = {
	"SAMP.img",
	"samp.img",
	nullptr
};

int
DiscoverAndRegisterImages(void)
{
	log("SampLoader: scanning for SAMP files\n");

	std::vector<std::string> seen;
	int count = 0;

	for(int ni = 0; kImgNames[ni]; ni++){
		std::vector<std::string> cands;
		buildCandidates(cands, ".", kImgNames[ni]);
		for(const auto &raw : cands){
			char norm[1024];
			sampNormPath(raw.c_str(), norm, sizeof(norm));
			if(sampAlreadySeen(seen, norm))
				continue;
			seen.push_back(norm);
			if(sampFileExists(norm)){
				log("SampLoader: registering IMG: %s\n", norm);
				AddCdImage(norm);
				sLoadedImgFiles.push_back(norm);
				count++;
			}
		}
	}

	const char *scanRoots[] = { "SAMP", "samp", nullptr };
	for(int si = 0; scanRoots[si]; si++){
		char subdir[1024];
		// Scan the game folder, but keep names relative to it like the rest.
		sampBuildPath(subdir, sizeof(subdir), GetGameDir(), scanRoots[si]);
		std::vector<std::string> found;
		sampScanDir(subdir, "img", 3, found);
		sampRelativeToGame(found);
		for(const auto &raw : found){
			char norm[1024];
			sampNormPath(raw.c_str(), norm, sizeof(norm));
			if(sampAlreadySeen(seen, norm))
				continue;
			seen.push_back(norm);
			log("SampLoader: registering IMG (scan): %s\n", norm);
			AddCdImage(norm);
			sLoadedImgFiles.push_back(norm);
			count++;
		}
	}

	log("SampLoader: registered %d SA-MP IMG archive(s)\n", count);
	return count;
}

static const char *kIdeNames[] = {
	"SAMP.ide", "samp.ide",
	nullptr
};

static const char *kColNames[] = {
	"SAMP.col", "samp.col",
	nullptr
};

static const char *kIplNames[] = {
	"SAMP.ipl", "samp.ipl",
	nullptr
};

static void
loadIdeFiles(void)
{
	std::vector<std::string> seen;

	for(int ni = 0; kIdeNames[ni]; ni++){
		std::vector<std::string> cands;
		buildCandidates(cands, ".", kIdeNames[ni]);
		for(const auto &raw : cands){
			char norm[1024];
			sampNormPath(raw.c_str(), norm, sizeof(norm));
			if(sampAlreadySeen(seen, norm))
				continue;
			seen.push_back(norm);
			if(sampFileExists(norm)){
				log("SampLoader: loading IDE: %s\n", norm);
				FileLoader::currentFile = NewGameFile((char*)norm);
				FileLoader::LoadObjectTypes(norm);
				sLoadedIdeFiles.push_back(norm);
			}
		}
	}


	const char *scanRoots[] = { "SAMP", "samp", nullptr };
	for(int si = 0; scanRoots[si]; si++){
		char subdir[1024];
		// Scan the game folder, but keep names relative to it like the rest.
		sampBuildPath(subdir, sizeof(subdir), GetGameDir(), scanRoots[si]);
		std::vector<std::string> found;
		sampScanDir(subdir, "ide", 3, found);
		sampRelativeToGame(found);
		for(const auto &raw : found){
			char norm[1024];
			sampNormPath(raw.c_str(), norm, sizeof(norm));
			if(sampAlreadySeen(seen, norm))
				continue;
			seen.push_back(norm);
			log("SampLoader: loading IDE (scan): %s\n", norm);
			FileLoader::currentFile = NewGameFile((char*)norm);
			FileLoader::LoadObjectTypes(norm);
			sLoadedIdeFiles.push_back(norm);
		}
	}
}

static void
loadColFiles(void)
{
	std::vector<std::string> seen;

	for(int ni = 0; kColNames[ni]; ni++){
		std::vector<std::string> cands;
		buildCandidates(cands, ".", kColNames[ni]);
		for(const auto &raw : cands){
			char norm[1024];
			sampNormPath(raw.c_str(), norm, sizeof(norm));
			if(sampAlreadySeen(seen, norm))
				continue;
			seen.push_back(norm);
			if(sampFileExists(norm)){
				log("SampLoader: loading COL: %s\n", norm);
				FileLoader::currentFile = NewGameFile((char*)norm);
				FileLoader::LoadCollisionFile(norm);
				sLoadedColFiles.push_back(norm);
			}
		}
	}

	const char *scanRoots[] = { "SAMP", "samp", "models", nullptr };
	for(int si = 0; scanRoots[si]; si++){
		char subdir[1024];
		// Scan the game folder, but keep names relative to it like the rest.
		sampBuildPath(subdir, sizeof(subdir), GetGameDir(), scanRoots[si]);
		std::vector<std::string> found;
		sampScanDir(subdir, "col", 2, found);
		sampRelativeToGame(found);
		for(const auto &raw : found){
			char norm[1024];
			sampNormPath(raw.c_str(), norm, sizeof(norm));
			if(sampAlreadySeen(seen, norm))
				continue;
			seen.push_back(norm);
			log("SampLoader: loading COL (scan): %s\n", norm);
			FileLoader::currentFile = NewGameFile((char*)norm);
			FileLoader::LoadCollisionFile(norm);
			sLoadedColFiles.push_back(norm);
		}
	}
}

// Collision packed in SA-MP's IMGs (SAMPCOL.img holds AllSAMPCOLs.col) is
// read with the game's, before these models exist, so none of it sticks:
// read it again now that they do.
static void
loadImgCollisions(void)
{
	for(int i = 0; GetColDef(i); i++){
		ColDef *col = GetColDef(i);
		if(col->imageIndex < 0)
			continue;
		char norm[1024];
		sampNormPath(GetCdImageLogicalName(col->imageIndex), norm, sizeof(norm));
		if(!sampAlreadySeen(sLoadedImgFiles, norm))
			continue;
		log("SampLoader: loading COL from IMG: %s\n", col->name);
		LoadCol(i);
	}
}

static void
loadIplFiles(void)
{
	std::vector<std::string> seen;

	for(int ni = 0; kIplNames[ni]; ni++){
		std::vector<std::string> cands;
		buildCandidates(cands, ".", kIplNames[ni]);
		for(const auto &raw : cands){
			char norm[1024];
			sampNormPath(raw.c_str(), norm, sizeof(norm));
			if(sampAlreadySeen(seen, norm))
				continue;
			seen.push_back(norm);
			if(sampFileExists(norm)){
				log("SampLoader: loading IPL: %s\n", norm);
				FileLoader::currentFile = NewGameFile((char*)norm);
				FileLoader::LoadScene(norm);
				sLoadedIplFiles.push_back(norm);
			}
		}
	}

	const char *scanRoots[] = { "SAMP", "samp", nullptr };
	for(int si = 0; scanRoots[si]; si++){
		char subdir[1024];
		// Scan the game folder, but keep names relative to it like the rest.
		sampBuildPath(subdir, sizeof(subdir), GetGameDir(), scanRoots[si]);
		std::vector<std::string> found;
		sampScanDir(subdir, "ipl", 2, found);
		sampRelativeToGame(found);
		for(const auto &raw : found){
			char norm[1024];
			sampNormPath(raw.c_str(), norm, sizeof(norm));
			if(sampAlreadySeen(seen, norm))
				continue;
			seen.push_back(norm);
			log("SampLoader: loading IPL (scan): %s\n", norm);
			FileLoader::currentFile = NewGameFile((char*)norm);
			FileLoader::LoadScene(norm);
			sLoadedIplFiles.push_back(norm);
		}
	}
}

void
LoadDefinitions(void)
{
	log("SampLoader: loading SA-MP definitions\n");

	loadIdeFiles();

	if(!sLoadedIdeFiles.empty())
		RefreshCdImageMappings();

	loadColFiles();
	if(!sLoadedIdeFiles.empty())
		loadImgCollisions();
	loadIplFiles();

	if(!sLoadedIdeFiles.empty())
		buildSampObjectIdSet();

	log("SampLoader: definitions done. IDE:%d COL:%d IPL:%d\n",
	    (int)sLoadedIdeFiles.size(),
	    (int)sLoadedColFiles.size(),
	    (int)sLoadedIplFiles.size());
}

void
ExtendCategories(void)
{
	if(sLoadedIdeFiles.empty())
		return;

	int uncatIdx = GetSampUncatCategoryIndex();
	if(uncatIdx < 0)
		return;

	int assigned = 0;
	for(int id = 0; id < NUMOBJECTDEFS; id++){
		if(!GetObjectDef(id))
			continue;
		if(GetObjectCategory(id) >= 0)
			continue;
		if(isObjectFromSamp(id)){
			SetObjectCategory(id, uncatIdx);
			assigned++;
		}
	}
	log("SampLoader: assigned %d SA-MP objects to 'SA-MP / Uncategorized'\n", assigned);
}

int GetSampRootCategoryIndex(void) { return SAMP_CATEGORY_ROOT; }

int GetSampUncatCategoryIndex(void) { return SAMP_CATEGORY_UNCAT; }

}
