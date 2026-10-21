#pragma once

// The updater's core, without ImGui or editor state, so the command-line
// modes (--update-check, --update-install, --update-rollback) and the tests
// run the same code as the banner: pick a release, verify its download,
// unpack it, install it by its manifest and roll it back.

#include <string>
#include <vector>
#include "versionutil.h"

// ---- SHA-256 ----

std::string Sha256Hex(const void *data, size_t size);
bool Sha256File(const std::string &path, std::string *hex);

// ---- Releases (GitHub's /releases JSON) ----

struct UpdateAsset {
	std::string name;
	std::string url;
	long long size = 0;
};

struct UpdateRelease {
	std::string tag;		// v1.0.0.57-production
	std::string version;	// 1.0.0.57
	std::string body;		// What's new (the CHANGELOG section)
	std::string pageUrl;	// the release's web page
	HaumeaVersion parsed;
	std::vector<UpdateAsset> assets;
	const UpdateAsset *Find(const char *name) const;
};

// The newest release on one of channels (beta follows "beta" and
// "production", production only "production") that is newer than current and
// not skipVersion ("" for none), from GitHub's release list. allowEqual also
// takes one of the same version (an unofficial CI build replaced by the
// official build of that commit); allowOlder takes the newest one even if
// it's older (going back on request). False with no error when there is none;
// false with *error set when the JSON is bad.
bool UpdatePickRelease(const std::string &json, const std::vector<std::string> &channels,
	const HaumeaVersion &current, const char *skipVersion, bool allowOlder, bool allowEqual,
	UpdateRelease *out, std::string *error);

// "<hex>  <name>" lines (sha256sum's format) -> the hex for name, lowercase.
bool UpdateSumFor(const std::string &sums, const char *name, std::string *hex);

// This platform's bundle asset name, or nil where there is none to install
// (then the banner only links to the release).
const char *UpdateBundleName(void);

// ---- Unpacking ----

// A .tar.gz into dir (which must not exist yet): regular files and folders
// only, relative paths without "..", at most maxBytes unpacked. Anything
// else (links, devices, absolute paths, a broken archive) is an error and
// dir is removed again.
bool UpdateUnpackTarGz(const std::string &archive, const std::string &dir, size_t maxBytes, std::string *error);

// ---- Manifests ----

// A bundle's MANIFEST: "<sha256>  <relative path>" per shipped file. Only
// these paths are ever written, moved or removed in the install folder.
struct ManifestEntry {
	std::string path;
	std::string sha256;
};
bool UpdateReadManifest(const std::string &file, std::vector<ManifestEntry> *out, std::string *error);

// ---- Installing ----

struct UpdateInstallResult {
	std::vector<std::string> keptLocal;	// edited files kept as <file>.local
	int replaced = 0, added = 0, removed = 0;
};

// Install the unpacked bundle in staged (with its MANIFEST) into installDir.
// Every file the old manifest (installDir/.haumea-manifest) or the new one
// lists is moved into installDir/.haumea-previous first, with a journal, so
// UpdateRollback can put back exactly what was there. Nothing outside the two
// manifests is touched.
bool UpdateInstall(const std::string &staged, const std::string &installDir, UpdateInstallResult *result,
	std::string *error);
// Undo the last UpdateInstall in installDir from its journal.
bool UpdateRollback(const std::string &installDir, std::string *error);
// True when installDir holds a previous version to roll back to.
bool UpdateCanRollback(const std::string &installDir);

// ---- Files ----

bool UpdateFileExists(const std::string &path);
bool UpdateIsDirectory(const std::string &path);
bool UpdateMakeDirs(const std::string &path);
bool UpdateReadFile(const std::string &path, std::string *out);
bool UpdateWriteFile(const std::string &path, const std::string &data);
// Move src to dst, replacing dst; works on a running executable on Windows
// and Linux (both allow renaming it).
bool UpdateMoveFile(const std::string &src, const std::string &dst);
bool UpdateRemoveTree(const std::string &path);
// Whether a file can be created in dir.
bool UpdateDirWritable(const std::string &dir);
