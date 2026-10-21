// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "version.h"
#include "versionutil.h"
#include "updatecore.h"
#include "updater.h"
#include "textdraws.h"
#include "testserver.h"
#include "agentbridge.h"
#include "workspace.h"
#include "icons.h"

#include <atomic>
#include <string>
#include <sys/stat.h>

#ifdef _WIN32
#include <winhttp.h>
#include <process.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// The flow: a background check reads this channel's releases; a newer one
// shows the banner. Update now (or "download automatically") fetches
// SHA256SUMS and this platform's bundle, checks the bundle's sha256, unpacks
// it into <config>/update/staged and checks every file against its MANIFEST.
// The next start (or Restart now) installs it before any window opens:
// UpdateInstall moves only manifest files, keeping the old ones for Roll back.
// Integrity, not authenticity (see updatecore.cpp).

// ---- Settings ----

static bool gUpdateCheckAuto = true;
static bool gUpdateDownloadAuto = false;
static char gUpdateChannel[32] = HAUMEA_CHANNEL;
static char gUpdateSkip[32];	// "Skip this version"

bool
UpdaterParseSetting(const char *key, const char *value)
{
	if(strcmp(key, "update_check") == 0)
		gUpdateCheckAuto = atoi(value) != 0;
	else if(strcmp(key, "update_download") == 0)
		gUpdateDownloadAuto = atoi(value) != 0;
	else if(strcmp(key, "update_channel") == 0)
		snprintf(gUpdateChannel, sizeof(gUpdateChannel), "%s", value);
	else if(strcmp(key, "update_skip") == 0)
		snprintf(gUpdateSkip, sizeof(gUpdateSkip), "%s", value);
	else
		return false;
	return true;
}

void
UpdaterWriteSettings(FILE *f)
{
	fprintf(f, "update_check %d\n", gUpdateCheckAuto ? 1 : 0);
	fprintf(f, "update_download %d\n", gUpdateDownloadAuto ? 1 : 0);
	if(gUpdateChannel[0] && strcmp(gUpdateChannel, HAUMEA_CHANNEL) != 0)
		fprintf(f, "update_channel %s\n", gUpdateChannel);
	if(gUpdateSkip[0])
		fprintf(f, "update_skip %s\n", gUpdateSkip);
}

// ---- Paths ----

static std::string
updateDir(void)
{
	return std::string(GetConfigDir()) + "/update";
}

static std::string
installDir(void)
{
	char exe[1024];
	if(!GetHaumeaExecutablePath(exe, sizeof(exe)))
		return std::string();
	std::string path = exe;
	size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// Why this install can only be told about updates, or "" when it can update.
static std::string
notifyOnlyReason(const std::string &dir)
{
	if(UpdateBundleName() == nil)
		return "Updates for this platform are downloaded from the release page.";
	if(dir.empty())
		return "Haumea can't find its own folder.";
#ifdef __linux__
	if(dir.compare(0, 5, "/usr/") == 0 || dir.compare(0, 5, "/opt/") == 0 || dir.compare(0, 5, "/nix/") == 0)
		return "Haumea was installed by a package manager: update it there.";
#endif
	if(UpdateFileExists(dir + "/.package-managed"))
		return "Haumea was installed by a package manager: update it there.";
	if(!UpdateDirWritable(dir))
		return "Haumea's folder can't be written to.";
	return std::string();
}

// The test suite points the updater at a local release server; nothing else
// may use plain http.
static const char*
testServer(void)
{
	const char *url = getenv("HAUMEA_UPDATE_URL");
	return url && url[0] ? url : nil;
}

static std::string
releaseListUrl(void)
{
	if(const char *test = testServer())
		return std::string(test) + "/releases";
	return "https://api.github.com/repos/itsneufox/haumea/releases?per_page=30";
}

static bool
urlAllowed(const std::string &url)
{
	if(url.compare(0, 8, "https://") == 0)
		return true;
	const char *test = testServer();
	return test && url.compare(0, strlen(test), test) == 0;
}

// ---- Threads ----

static void
startThread(void (*fn)(void))
{
#ifdef _WIN32
	struct Trampoline {
		static unsigned __stdcall run(void *arg){ ((void (*)(void))arg)(); return 0; }
	};
	HANDLE h = (HANDLE)_beginthreadex(nil, 0, Trampoline::run, (void*)fn, 0, nil);
	if(h) CloseHandle(h);
#else
	struct Trampoline {
		static void *run(void *arg){ ((void (*)(void))arg)(); return nil; }
	};
	pthread_t t;
	if(pthread_create(&t, nil, Trampoline::run, (void*)fn) == 0)
		pthread_detach(t);
#endif
}

// ---- Downloads ----

static std::atomic<long long> gDownloadedBytes(0);

#ifdef _WIN32
static bool
httpFetch(const std::string &url, const std::string &outPath, std::string *error)
{
	if(!urlAllowed(url)){
		*error = "refusing a non-https address";
		return false;
	}
	std::wstring wurl(url.begin(), url.end());
	URL_COMPONENTS parts = {};
	parts.dwStructSize = sizeof(parts);
	wchar_t host[256], path[2048];
	parts.lpszHostName = host; parts.dwHostNameLength = 256;
	parts.lpszUrlPath = path; parts.dwUrlPathLength = 2048;
	wchar_t extra[1024];
	parts.lpszExtraInfo = extra; parts.dwExtraInfoLength = 1024;
	if(!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)){
		*error = "bad address " + url;
		return false;
	}
	std::wstring fullPath = std::wstring(path) + extra;
	bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
	std::string ua = std::string("Haumea-Updater/") + HaumeaFullVersion();
	std::wstring wua(ua.begin(), ua.end());
	HINTERNET session = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	HINTERNET connect = session ? WinHttpConnect(session, host, parts.nPort, 0) : nil;
	HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", fullPath.c_str(), nil,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0) : nil;
	bool ok = false;
	DWORD status = 0, statusSize = sizeof(status);
	if(request){
		// GitHub sends release assets through a redirect (https to https).
		DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
		WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
		ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
			WinHttpReceiveResponse(request, nil) &&
			WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, nil) && status == 200;
	}
	if(!ok)
		*error = status ? "the server answered " + std::to_string(status) : std::string("no connection");
	FILE *f = ok ? fopen(outPath.c_str(), "wb") : nil;
	if(ok && f == nil){
		*error = "can't write " + outPath;
		ok = false;
	}
	if(ok){
		char buf[65536];
		DWORD got = 0;
		for(;;){
			if(!WinHttpReadData(request, buf, sizeof(buf), &got)){
				ok = false;
				*error = "the download broke off";
				break;
			}
			if(got == 0)
				break;
			if(fwrite(buf, 1, got, f) != got){
				ok = false;
				*error = "can't write " + outPath;
				break;
			}
			gDownloadedBytes += got;
		}
		ok = fclose(f) == 0 && ok;
	}
	if(request) WinHttpCloseHandle(request);
	if(connect) WinHttpCloseHandle(connect);
	if(session) WinHttpCloseHandle(session);
	if(!ok)
		remove(outPath.c_str());
	return ok;
}
#else
// curl, run as a program: every Linux and macOS install has it, and it
// handles proxies and certificates the way the system does.
static bool
httpFetch(const std::string &url, const std::string &outPath, std::string *error)
{
	if(!urlAllowed(url)){
		*error = "refusing a non-https address";
		return false;
	}
	bool https = url.compare(0, 8, "https://") == 0;
	std::string ua = std::string("Haumea-Updater/") + HaumeaFullVersion();
	std::vector<std::string> args = { "curl", "-fsSL", "--connect-timeout", "15", "--max-time", "1800",
		"--proto", https ? "=https" : "=http", "--proto-redir", https ? "=https" : "=http",
		"-A", ua, "-o", outPath, url };
	std::vector<char*> argv;
	for(std::string &a : args)
		argv.push_back(&a[0]);
	argv.push_back(nil);
	remove(outPath.c_str());
	pid_t pid = fork();
	if(pid < 0){
		*error = "can't start curl";
		return false;
	}
	if(pid == 0){
		execvp("curl", argv.data());
		_exit(127);
	}
	long long last = 0;
	int status = 0;
	for(;;){
		pid_t done = waitpid(pid, &status, WNOHANG);
		struct stat st;
		if(stat(outPath.c_str(), &st) == 0 && st.st_size > last){
			gDownloadedBytes += st.st_size - last;
			last = st.st_size;
		}
		if(done == pid)
			break;
		if(done < 0){
			*error = "lost curl";
			return false;
		}
		usleep(100000);
	}
	int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	if(code == 0)
		return true;
	remove(outPath.c_str());
	*error = code == 127 ? "curl isn't installed" :
		code == 22 ? "the server refused the download" :
		code == 6 || code == 7 || code == 28 ? "no connection" : "the download failed (curl " + std::to_string(code) + ")";
	return false;
}
#endif

static bool
httpGetString(const std::string &url, const std::string &tempPath, std::string *out, std::string *error)
{
	bool ok = httpFetch(url, tempPath, error) && UpdateReadFile(tempPath, out);
	remove(tempPath.c_str());
	return ok;
}

// ---- Checking, downloading, staging ----

enum UpdateStatus {
	UPD_IDLE,
	UPD_CHECKING,
	UPD_UP_TO_DATE,
	UPD_OFFLINE,
	UPD_AVAILABLE,
	UPD_DOWNLOADING,
	UPD_READY,
	UPD_FAILED,
};

// Releases larger than this, unpacked, are refused.
static const size_t MAX_UNPACKED_BYTES = (size_t)1024 * 1024 * 1024;

static int
checkReleases(const std::string &work, const char *channel, const HaumeaVersion &current, const char *skip,
	UpdateRelease *rel, std::string *error)
{
	std::string json;
	if(!UpdateMakeDirs(work) || !httpGetString(releaseListUrl(), work + "/releases.json", &json, error))
		return UPD_OFFLINE;
	error->clear();
	if(UpdatePickRelease(json, channel, current, skip, rel, error))
		return UPD_AVAILABLE;
	return error->empty() ? UPD_UP_TO_DATE : UPD_FAILED;
}

static bool
moveDir(const std::string &src, const std::string &dst)
{
#ifdef _WIN32
	return MoveFileExA(src.c_str(), dst.c_str(), 0) != 0;
#else
	return rename(src.c_str(), dst.c_str()) == 0;
#endif
}

// Download this platform's bundle, check it against SHA256SUMS, unpack it
// and check it against its MANIFEST, then make it work/staged.
static bool
downloadAndStage(const std::string &work, const UpdateRelease &rel, std::string *error, bool *badChecksum)
{
	*badChecksum = false;
	const char *bundle = UpdateBundleName();
	const UpdateAsset *asset = bundle ? rel.Find(bundle) : nil;
	const UpdateAsset *sumsAsset = rel.Find("SHA256SUMS");
	if(asset == nil || sumsAsset == nil){
		*error = "the release has no " + std::string(bundle ? bundle : "bundle") + " or SHA256SUMS";
		return false;
	}
	std::string downloads = work + "/download";
	if(!UpdateMakeDirs(downloads)){
		*error = "can't make " + downloads;
		return false;
	}
	std::string sums, expected;
	if(!httpGetString(sumsAsset->url, downloads + "/SHA256SUMS", &sums, error))
		return false;
	if(!UpdateSumFor(sums, bundle, &expected)){
		*error = "SHA256SUMS doesn't list " + std::string(bundle);
		*badChecksum = true;
		return false;
	}
	std::string part = downloads + "/" + bundle + ".part";
	gDownloadedBytes = 0;
	if(!httpFetch(asset->url, part, error))
		return false;
	std::string actual;
	if(!Sha256File(part, &actual) || actual != expected){
		remove(part.c_str());
		*error = "the download doesn't match SHA256SUMS";
		*badChecksum = true;
		return false;
	}
	std::string unpacked = work + "/staged.new";
	UpdateRemoveTree(unpacked);
	bool ok = UpdateUnpackTarGz(part, unpacked, MAX_UNPACKED_BYTES, error);
	remove(part.c_str());
	if(!ok)
		return false;
	std::vector<ManifestEntry> manifest;
	if(!UpdateReadManifest(unpacked + "/MANIFEST", &manifest, error)){
		UpdateRemoveTree(unpacked);
		return false;
	}
	for(const ManifestEntry &e : manifest){
		std::string hex;
		if(!Sha256File(unpacked + "/" + e.path, &hex) || hex != e.sha256){
			UpdateRemoveTree(unpacked);
			*error = e.path + " doesn't match the manifest";
			*badChecksum = true;
			return false;
		}
	}
	// What the next start needs to know: the version and its notes.
	if(!UpdateWriteFile(unpacked + "/.haumea-release", rel.version + "\n" + rel.pageUrl + "\n") ||
	   !UpdateWriteFile(unpacked + "/.haumea-notes", rel.body)){
		UpdateRemoveTree(unpacked);
		*error = "can't write the staged update";
		return false;
	}
	UpdateRemoveTree(work + "/staged");
	if(!moveDir(unpacked, work + "/staged")){
		UpdateRemoveTree(unpacked);
		*error = "can't stage the update";
		return false;
	}
	return true;
}

static std::string
stagedVersion(const std::string &staged)
{
	std::string text;
	if(!UpdateReadFile(staged + "/.haumea-release", &text))
		return std::string();
	return text.substr(0, text.find('\n'));
}

// ---- When updating is allowed at all ----

static bool
hasArg(const char *arg)
{
	for(int i = 1; i < sk::args.argc; i++)
		if(strcmp(sk::args.argv[i], arg) == 0)
			return true;
	return false;
}

// Only an interactive start looks for, downloads or installs updates: never
// the tests, UI scripts, round trips or CI. The one exception is a UI script
// run against a test release server (test_updater.py drives the banner).
static bool
updatesAllowed(void)
{
	const char *off = getenv("HAUMEA_NO_UPDATE");
	if(off && off[0] && strcmp(off, "0") != 0)
		return false;
	bool script = UiScriptActive() || hasArg("--ui-script");
	return !SettingsAreReadOnly() && (!script || testServer()) &&
		!hasArg("--textdraw-roundtrip") && !hasArg("--pawn-roundtrip");
}

// What stops Restart now (the update then waits for the next start), or nil.
static const char*
restartBlocker(void)
{
	if(PawnMapHasUnsavedChanges() || TextDrawFileHasUnsavedChanges())
		return "Save your changes first.";
	if(InTextDrawMode())
		return "Leave TextDraw mode first.";
	if(TestServerIsRunning())
		return "Stop Test in game first.";
	if(AgentBridgeSessionActive())
		return "An agent edit session is open.";
	return nil;
}

// ---- After a restart: what the previous run did ----

// <config>/update/notice, written before the restart, read once:
//   updated <new> <old>, then "local <path>" lines (files kept as .local)
//   rolledback <from> <to>
//   failed <message>
static std::string gNoticeKind, gNoticeVersion, gNoticeOther, gNoticeError;
static std::vector<std::string> gNoticeLocal;
static std::string gNotes;	// What's new for the banner's version

static void
writeNotice(const std::string &text)
{
	UpdateMakeDirs(updateDir());
	UpdateWriteFile(updateDir() + "/notice", text);
}

static void
readNotice(void)
{
	std::string path = updateDir() + "/notice", text;
	if(!UpdateReadFile(path, &text))
		return;
	remove(path.c_str());
	size_t at = 0;
	while(at < text.size()){
		size_t eol = text.find('\n', at);
		if(eol == std::string::npos) eol = text.size();
		std::string line = text.substr(at, eol - at);
		at = eol + 1;
		size_t sp = line.find(' ');
		std::string word = line.substr(0, sp), rest = sp == std::string::npos ? "" : line.substr(sp + 1);
		if(word == "updated" || word == "rolledback"){
			gNoticeKind = word;
			size_t sp2 = rest.find(' ');
			gNoticeVersion = rest.substr(0, sp2);
			gNoticeOther = sp2 == std::string::npos ? "" : rest.substr(sp2 + 1);
		}else if(word == "local")
			gNoticeLocal.push_back(rest);
		else if(word == "failed"){
			gNoticeKind = word;
			gNoticeError = rest;
		}
	}
	// An "updated" notice is only this version's (not a stale one).
	if(gNoticeKind == "updated" && gNoticeVersion != HaumeaFullVersion())
		gNoticeKind.clear();
	if(gNoticeKind == "updated")
		UpdateReadFile(updateDir() + "/notes", &gNotes);
}

// ---- Startup: install what was downloaded ----

void
UpdaterInstallStaged(void)
{
	if(!updatesAllowed())
		return;
	std::string staged = updateDir() + "/staged";
	if(!UpdateIsDirectory(staged))
		return;
	std::string version = stagedVersion(staged);
	HaumeaVersion v;
	if(!ParseHaumeaVersion(version.c_str(), &v) || CompareHaumeaVersion(v, HaumeaCurrentVersion()) <= 0){
		UpdateRemoveTree(staged);
		return;
	}
	std::string dir = installDir();
	std::string reason = notifyOnlyReason(dir);
	if(!reason.empty()){
		UpdateRemoveTree(staged);
		writeNotice("failed " + reason + "\n");
		return;
	}
	std::string notes;
	UpdateReadFile(staged + "/.haumea-notes", &notes);
	UpdateInstallResult result;
	std::string error;
	if(!UpdateInstall(staged, dir, &result, &error)){
		UpdateRemoveTree(staged);
		writeNotice("failed " + error + "\n");
		log("Update to %s failed: %s\n", version.c_str(), error.c_str());
		return;
	}
	// The version Roll back returns to.
	UpdateWriteFile(dir + "/.haumea-previous/VERSION", std::string(HaumeaFullVersion()) + "\n");
	std::string notice = "updated " + version + " " + HaumeaFullVersion() + "\n";
	for(const std::string &p : result.keptLocal)
		notice += "local " + p + "\n";
	writeNotice(notice);
	UpdateWriteFile(updateDir() + "/notes", notes);
	UpdateRemoveTree(staged);
	log("Updated to %s; starting it\n", version.c_str());
	ExecHaumeaInPlace();
	log("Couldn't start the new version; it starts next time\n");
}

static std::string
previousVersion(void)
{
	std::string text;
	if(!UpdateReadFile(installDir() + "/.haumea-previous/VERSION", &text))
		return std::string();
	return text.substr(0, text.find('\n'));
}

// ---- The background check and download ----

// The worker writes the strings, then publishes them by setting gStatus;
// the GUI reads them only after seeing that status. One worker at a time.
static std::atomic<int> gStatus(UPD_IDLE);
static std::atomic<bool> gWorking(false);
static UpdateRelease gRelease;
static std::string gError, gNotifyOnly;
static bool gManualCheck;		// Help > Check for updates: also say "up to date"
static bool gBannerHidden;		// Later
static bool gShowWhatsNew;

static void
downloadWorker(void)
{
	std::string error;
	bool badChecksum;
	bool ok = downloadAndStage(updateDir(), gRelease, &error, &badChecksum);
	gError = error;
	// Free for the next job before the GUI can see this status and act on it.
	gWorking = false;
	gStatus = ok ? UPD_READY : UPD_FAILED;
}

static void
checkWorker(void)
{
	UpdateRelease rel;
	std::string error;
	int status = checkReleases(updateDir(), gUpdateChannel, HaumeaCurrentVersion(), gUpdateSkip, &rel, &error);
	gRelease = rel;
	gError = error;
	gNotifyOnly = status == UPD_AVAILABLE ? notifyOnlyReason(installDir()) : std::string();
	if(status == UPD_AVAILABLE && gNotifyOnly.empty() && UpdateBundleName() && !rel.Find(UpdateBundleName()))
		gNotifyOnly = "This release has no download for this platform.";
	// Already downloaded (the banner was put off with Later last time).
	if(status == UPD_AVAILABLE && stagedVersion(updateDir() + "/staged") == rel.version){
		gWorking = false;
		gStatus = UPD_READY;
		return;
	}
	if(status == UPD_AVAILABLE && gNotifyOnly.empty() && gUpdateDownloadAuto && !gManualCheck){
		gStatus = UPD_DOWNLOADING;
		downloadWorker();
		return;
	}
	gWorking = false;
	gStatus = status;
}

static void
startCheck(bool manual)
{
	if(gWorking.exchange(true))
		return;
	gManualCheck = manual;
	gBannerHidden = false;
	gStatus = UPD_CHECKING;
	startThread(checkWorker);
}

static void
startDownload(void)
{
	if(gWorking.exchange(true))
		return;
	gStatus = UPD_DOWNLOADING;
	gDownloadedBytes = 0;
	startThread(downloadWorker);
}

void
UpdaterCheckForUpdate(void)
{
	if(!updatesAllowed())
		return;
	readNotice();
	if(gUpdateCheckAuto)
		startCheck(false);
}

void
UpdaterCheckNow(void)
{
	if(!updatesAllowed()){
		Toast(TOAST_SELECTION, "Updates are off in this run");
		return;
	}
	startCheck(true);
}

static void
rollBack(void)
{
	std::string from = HaumeaFullVersion(), to = previousVersion();
	std::string error;
	if(!UpdateRollback(installDir(), &error)){
		Toast(TOAST_SELECTION, "Couldn't roll back: %s", error.c_str());
		return;
	}
	// Don't offer the version just left again.
	snprintf(gUpdateSkip, sizeof(gUpdateSkip), "%s", from.c_str());
	SaveEditorSettingsNow();
	writeNotice("rolledback " + from + " " + to + "\n");
	if(!RestartHaumeaKeepingArgs())
		Toast(TOAST_SELECTION, "Rolled back to %s: restart Haumea to use it", to.c_str());
}

// ---- GUI ----

static void
whatsNewWindow(void)
{
	if(!gShowWhatsNew)
		return;
	const std::string &version = gNoticeKind == "updated" ? gNoticeVersion : gRelease.version;
	const std::string &notes = gNoticeKind == "updated" ? gNotes : gRelease.body;
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, ImGui::GetFontSize() * 24.0f), ImGuiCond_FirstUseEver);
	char title[96];
	snprintf(title, sizeof(title), "What's new in %s###updater_whats_new", version.c_str());
	if(ImGui::Begin(title, &gShowWhatsNew)){
		if(notes.empty())
			ImGui::TextDisabled("This release has no notes.");
		else
			ImGui::TextWrapped("%s", notes.c_str());
		if(!gRelease.pageUrl.empty() && version == gRelease.version)
			ImGui::TextLinkOpenURL("Release page", gRelease.pageUrl.c_str());
	}
	ImGui::End();
}

static bool
bannerBegin(const char *id)
{
	ImGuiIO &io = ImGui::GetIO();
	KeepNextEditorOverlayOnMainWindow();
	ImGui::SetNextWindowPos(EditorUiPoint(io.DisplaySize.x - 10.0f, 40.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 22.0f, 0));
	return ImGui::Begin(id, nil, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing);
}

// After an update or a roll back: once, until dismissed.
static void
noticeBanner(void)
{
	if(gNoticeKind.empty())
		return;
	if(bannerBegin("##updater_notice")){
		if(gNoticeKind == "updated"){
			ImGui::Text(ICON_FA_CHECK " Updated to Haumea Studio %s", gNoticeVersion.c_str());
			for(const std::string &p : gNoticeLocal)
				ImGui::TextWrapped("You had changed %s: your copy is %s.local.", p.c_str(), p.c_str());
			if(ImGui::Button("What's new"))
				gShowWhatsNew = true;
			ImGui::SameLine();
		}else if(gNoticeKind == "rolledback"){
			ImGui::Text(ICON_FA_ROTATE_LEFT " Back to Haumea Studio %s", gNoticeOther.c_str());
			ImGui::TextDisabled("%s won't be offered again.", gNoticeVersion.c_str());
		}else{
			ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION " The update wasn't installed: %s", gNoticeError.c_str());
		}
		if(ImGui::Button("OK"))
			gNoticeKind.clear();
	}
	ImGui::End();
}

void
UpdaterDrawGui(void)
{
	noticeBanner();
	whatsNewWindow();
	int status = gStatus;
	if(gManualCheck && (status == UPD_UP_TO_DATE || status == UPD_OFFLINE || status == UPD_FAILED)){
		if(status == UPD_UP_TO_DATE)
			Toast(TOAST_SELECTION, "Haumea Studio %s is up to date", HaumeaFullVersion());
		else
			Toast(TOAST_SELECTION, "Couldn't check for updates: %s", gError.c_str());
		gManualCheck = false;
		gStatus = UPD_IDLE;
		return;
	}
	if(gBannerHidden || !(status == UPD_AVAILABLE || status == UPD_DOWNLOADING || status == UPD_READY ||
	   (status == UPD_FAILED && !gRelease.version.empty())))
		return;
	if(bannerBegin("##updater")){
		ImGui::Text(ICON_FA_BELL " Haumea Studio %s %s", gRelease.version.c_str(),
			status == UPD_DOWNLOADING ? "is downloading" : status == UPD_READY ? "is ready to install" : "is available");
		if(status == UPD_AVAILABLE){
			if(!gNotifyOnly.empty())
				ImGui::TextDisabled("%s", gNotifyOnly.c_str());
			if(ImGui::Button("What's new"))
				gShowWhatsNew = true;
			ImGui::SameLine();
			if(gNotifyOnly.empty()){
				if(ImGui::Button("Update now"))
					startDownload();
				ImGui::SetItemTooltip("Downloads it in the background; it installs when Haumea restarts.");
			}else if(!gRelease.pageUrl.empty())
				ImGui::TextLinkOpenURL("Release page", gRelease.pageUrl.c_str());
			ImGui::SameLine();
			if(ImGui::Button("Later"))
				gBannerHidden = true;
			ImGui::SameLine();
			if(ImGui::Button("Skip this version")){
				snprintf(gUpdateSkip, sizeof(gUpdateSkip), "%s", gRelease.version.c_str());
				SaveEditorSettingsNow();
				gBannerHidden = true;
			}
		}else if(status == UPD_DOWNLOADING){
			const UpdateAsset *asset = UpdateBundleName() ? gRelease.Find(UpdateBundleName()) : nil;
			long long total = asset ? asset->size : 0;
			if(total > 0)
				ImGui::ProgressBar((float)((double)gDownloadedBytes / total));
			else
				ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime());
			if(ImGui::Button("Hide"))
				gBannerHidden = true;
			ImGui::SetItemTooltip("The download goes on; the update installs when Haumea restarts.");
		}else if(status == UPD_READY){
			const char *blocker = restartBlocker();
			ImGui::BeginDisabled(blocker != nil);
			if(ImGui::Button("Restart now") && !RestartHaumeaKeepingArgs())
				Toast(TOAST_SELECTION, "Couldn't restart: the update installs next time");
			ImGui::EndDisabled();
			if(blocker)
				ImGui::SetItemTooltip("%s The update installs the next time Haumea starts.", blocker);
			ImGui::SameLine();
			if(ImGui::Button("Later"))
				gBannerHidden = true;
			ImGui::SetItemTooltip("It installs the next time Haumea starts.");
		}else{
			ImGui::TextWrapped("The update failed: %s", gError.c_str());
			if(ImGui::Button("Try again"))
				startDownload();
			ImGui::SameLine();
			if(ImGui::Button("Dismiss"))
				gBannerHidden = true;
		}
	}
	ImGui::End();
}

void
UpdaterHelpMenuItems(void)
{
	if(ImGui::MenuItem(ICON_FA_BELL " Check for updates", nil, false, !gWorking))
		UpdaterCheckNow();
	std::string dir = installDir();
	if(!dir.empty() && UpdateCanRollback(dir)){
		std::string prev = previousVersion();
		char label[96];
		snprintf(label, sizeof(label), ICON_FA_ROTATE_LEFT " Roll back to %s", prev.empty() ? "the previous version" : prev.c_str());
		const char *blocker = restartBlocker();
		if(ImGui::MenuItem(label, nil, false, blocker == nil))
			rollBack();
		ImGui::SetItemTooltip(blocker ? blocker : "Puts back the version before the last update, then restarts.");
	}
}

void
UpdaterPreferences(void)
{
	bool changed = false;
	changed |= ImGui::Checkbox("Check for updates when Haumea starts", &gUpdateCheckAuto);
	changed |= ImGui::Checkbox("Download updates automatically", &gUpdateDownloadAuto);
	ImGui::SetItemTooltip("Downloads in the background; the update still installs only when Haumea restarts.");
	static const char *channels[] = { "master", "PE", "agent-alpha" };
	if(ImGui::BeginCombo("Channel", gUpdateChannel)){
		for(const char *c : channels)
			if(ImGui::Selectable(c, strcmp(c, gUpdateChannel) == 0)){
				snprintf(gUpdateChannel, sizeof(gUpdateChannel), "%s", c);
				changed = true;
			}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("Which releases to follow. This build is %s.", HAUMEA_CHANNEL);
	if(gUpdateSkip[0]){
		ImGui::TextDisabled("Skipping %s", gUpdateSkip);
		ImGui::SameLine();
		if(ImGui::SmallButton("Offer it again")){
			gUpdateSkip[0] = '\0';
			changed = true;
		}
	}
	ImGui::TextDisabled("This is %s.", HaumeaVersionDisplay());
	if(ImGui::Button("Check now"))
		UpdaterCheckNow();
	if(changed)
		SaveEditorSettingsNow();
}

// ---- Command line (the tests) ----

static std::string
jsonString(const std::string &s)
{
	std::string out = "\"";
	for(unsigned char c : s){
		if(c == '"' || c == '\\'){ out += '\\'; out += (char)c; }
		else if(c == '\n') out += "\\n";
		else if(c < 0x20){ char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
		else out += (char)c;
	}
	return out + "\"";
}

static int
writeReport(const char *path, const std::string &result, const std::string &fields)
{
	std::string json = "{\"result\": " + jsonString(result) + fields + "}\n";
	if(!UpdateWriteFile(path, json)){
		fprintf(stderr, "can't write %s\n", path);
		return 2;
	}
	printf("%s", json.c_str());
	return 0;
}

//   --update-check <work dir> <report.json>
//       Check HAUMEA_UPDATE_URL (else GitHub) and, for a newer release,
//       download, verify and stage it in <work dir>/staged. The version to
//       compare with, the channel and the skipped version come from
//       HAUMEA_UPDATE_CURRENT, HAUMEA_UPDATE_CHANNEL and HAUMEA_UPDATE_SKIP
//       when set. result: up_to_date, offline, staged, bad_checksum, error.
//   --update-install <staged dir> <install dir> <report.json>
//   --update-rollback <install dir> <report.json>
int
UpdaterCommandLine(int argc, char **argv)
{
	for(int i = 1; i < argc; i++){
		if(strcmp(argv[i], "--update-check") == 0 && i + 2 < argc){
			std::string work = argv[i + 1];
			const char *report = argv[i + 2];
			HaumeaVersion current = HaumeaCurrentVersion();
			if(const char *c = getenv("HAUMEA_UPDATE_CURRENT"))
				ParseHaumeaVersion(c, &current);
			const char *channel = getenv("HAUMEA_UPDATE_CHANNEL");
			const char *skip = getenv("HAUMEA_UPDATE_SKIP");
			UpdateRelease rel;
			std::string error;
			int status = checkReleases(work, channel ? channel : HAUMEA_CHANNEL, current, skip ? skip : "",
				&rel, &error);
			if(status != UPD_AVAILABLE)
				return writeReport(report, status == UPD_UP_TO_DATE ? "up_to_date" :
					status == UPD_OFFLINE ? "offline" : "error", ", \"error\": " + jsonString(error));
			bool badChecksum;
			std::string fields = ", \"version\": " + jsonString(rel.version);
			if(!downloadAndStage(work, rel, &error, &badChecksum))
				return writeReport(report, badChecksum ? "bad_checksum" : "error",
					fields + ", \"error\": " + jsonString(error));
			return writeReport(report, "staged", fields + ", \"staged\": " + jsonString(work + "/staged"));
		}
		if(strcmp(argv[i], "--update-install") == 0 && i + 3 < argc){
			UpdateInstallResult result;
			std::string error;
			if(!UpdateInstall(argv[i + 1], argv[i + 2], &result, &error))
				return writeReport(argv[i + 3], "error", ", \"error\": " + jsonString(error));
			std::string local;
			for(const std::string &p : result.keptLocal)
				local += (local.empty() ? "" : ", ") + jsonString(p);
			return writeReport(argv[i + 3], "installed", ", \"replaced\": " + std::to_string(result.replaced) +
				", \"added\": " + std::to_string(result.added) + ", \"removed\": " + std::to_string(result.removed) +
				", \"kept_local\": [" + local + "]");
		}
		if(strcmp(argv[i], "--update-rollback") == 0 && i + 2 < argc){
			std::string error;
			if(!UpdateRollback(argv[i + 1], &error))
				return writeReport(argv[i + 2], "error", ", \"error\": " + jsonString(error));
			return writeReport(argv[i + 2], "rolled_back", "");
		}
	}
	return -1;
}
