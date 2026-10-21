#include "haumea.h"
#include "icons.h"
#include "testserver.h"
#include "workspace.h"
#include "encoding.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <deque>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

// The test server lives in <config>/testserver/server. Its files come from a
// source server: the package set in Preferences (a server folder, or a Linux
// .tar.gz / Windows .zip extracted once to <config>/testserver/package), or
// else the workspace server. Only open.mp's own components are copied, plus
// the streamer plugin, recordings, node files and custom models when the map
// needs them.

// ---- paths ----------------------------------------------------------------

static std::string
tsConfigDir(void)
{
	return GetConfigDir();
}

static std::string
tsWorkspaceDir(void)
{
	return GetWorkspaceDir();
}

static std::string
tsGameDir(void)
{
	return GetGameDir();
}

static std::string
tsJoin(const std::string &a, const std::string &b)
{
	if(a.empty())
		return b;
	char last = a[a.size()-1];
	return last == '/' || last == '\\' ? a + b : a + "/" + b;
}

static bool
tsIsDir(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static bool
tsIsFile(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

static bool
tsEndsWith(const std::string &s, const char *suffix)
{
	size_t n = strlen(suffix);
	if(s.size() < n)
		return false;
	for(size_t i = 0; i < n; i++)
		if(tolower((unsigned char)s[s.size()-n+i]) != tolower((unsigned char)suffix[i]))
			return false;
	return true;
}

// Names in a folder (files and folders), without "." and "..".
static std::vector<std::string>
tsListDir(const std::string &dir)
{
	std::vector<std::string> names;
#ifdef _WIN32
	WIN32_FIND_DATAA data;
	HANDLE h = FindFirstFileA(tsJoin(dir, "*").c_str(), &data);
	if(h == INVALID_HANDLE_VALUE)
		return names;
	do{
		if(strcmp(data.cFileName, ".") != 0 && strcmp(data.cFileName, "..") != 0)
			names.push_back(data.cFileName);
	}while(FindNextFileA(h, &data));
	FindClose(h);
#else
	DIR *d = opendir(dir.c_str());
	if(d == nil)
		return names;
	while(struct dirent *e = readdir(d))
		if(strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0)
			names.push_back(e->d_name);
	closedir(d);
#endif
	std::sort(names.begin(), names.end());
	return names;
}

// A name in a folder, ignoring case (Windows packages and the GTA folder
// mix cases); "" when it is not there.
static std::string
tsFindName(const std::string &dir, const char *name)
{
	std::vector<std::string> names = tsListDir(dir);
	for(size_t i = 0; i < names.size(); i++)
		if(rw::strncmp_ci(names[i].c_str(), name, 1024) == 0)
			return tsJoin(dir, names[i]);
	return "";
}

static bool
tsMakeDirs(const std::string &dir)
{
	return EnsureParentDirectoriesForPath(tsJoin(dir, "x").c_str());
}

// Copies a file unless the copy already has the same size and is newer.
static bool
tsCopyFile(const std::string &from, const std::string &to)
{
	struct stat a, b;
	if(stat(from.c_str(), &a) != 0)
		return false;
	if(stat(to.c_str(), &b) == 0 && a.st_size == b.st_size && b.st_mtime >= a.st_mtime)
		return true;
	if(!EnsureParentDirectoriesForPath(to.c_str()))
		return false;
	FILE *in = fopen(from.c_str(), "rb");
	if(in == nil)
		return false;
	FILE *out = fopen(to.c_str(), "wb");
	if(out == nil){
		fclose(in);
		return false;
	}
	char buf[65536];
	size_t n;
	bool ok = true;
	while((n = fread(buf, 1, sizeof(buf), in)) > 0)
		if(fwrite(buf, 1, n, out) != n){
			ok = false;
			break;
		}
	fclose(in);
	fclose(out);
#ifndef _WIN32
	chmod(to.c_str(), a.st_mode & 0777);
#endif
	return ok;
}

static bool
tsCopyTree(const std::string &from, const std::string &to)
{
	if(tsIsFile(from))
		return tsCopyFile(from, to);
	if(!tsIsDir(from) || !tsMakeDirs(to))
		return false;
	bool ok = true;
	std::vector<std::string> names = tsListDir(from);
	for(size_t i = 0; i < names.size(); i++)
		ok = tsCopyTree(tsJoin(from, names[i]), tsJoin(to, names[i])) && ok;
	return ok;
}

static bool
tsWriteText(const std::string &path, const std::string &text)
{
	if(!EnsureParentDirectoriesForPath(path.c_str()))
		return false;
	FILE *f = fopen(path.c_str(), "wb");
	if(f == nil)
		return false;
	bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
	return fclose(f) == 0 && ok;
}

static std::string
tsReadText(const std::string &path)
{
	std::string text;
	FILE *f = fopen(path.c_str(), "rb");
	if(f == nil)
		return text;
	char buf[4096];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		text.append(buf, n);
	fclose(f);
	return text;
}

// ---- settings -------------------------------------------------------------

static struct {
	char package[1024];	// server folder, .tar.gz or .zip; empty: the workspace server
	char nickname[24];
	char winePrefix[1024];	// empty: the one the game folder is in
	int port;
	bool loaded;
} gTsSettings;

static std::string
tsSettingsPath(void)
{
	return tsJoin(tsConfigDir(), "testserver.cfg");
}

static void
tsLoadSettings(void)
{
	if(gTsSettings.loaded)
		return;
	gTsSettings.loaded = true;
	gTsSettings.port = 7790;
	strncpy(gTsSettings.nickname, "Haumea", sizeof(gTsSettings.nickname)-1);
	std::string text = tsReadText(tsSettingsPath());
	size_t pos = 0;
	while(pos < text.size()){
		size_t end = text.find('\n', pos);
		if(end == std::string::npos)
			end = text.size();
		std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		if(!line.empty() && line[line.size()-1] == '\r')
			line.erase(line.size()-1);
		size_t eq = line.find('=');
		if(eq == std::string::npos)
			continue;
		std::string key = line.substr(0, eq), value = line.substr(eq + 1);
		if(key == "package")
			strncpy(gTsSettings.package, value.c_str(), sizeof(gTsSettings.package)-1);
		else if(key == "nickname")
			strncpy(gTsSettings.nickname, value.c_str(), sizeof(gTsSettings.nickname)-1);
		else if(key == "wineprefix")
			strncpy(gTsSettings.winePrefix, value.c_str(), sizeof(gTsSettings.winePrefix)-1);
		else if(key == "port")
			gTsSettings.port = clamp(atoi(value.c_str()), 1024, 65535);
	}
}

static void
tsSaveSettings(void)
{
	char port[16];
	snprintf(port, sizeof(port), "%d", gTsSettings.port);
	tsWriteText(tsSettingsPath(), std::string("package=") + gTsSettings.package +
		"\nnickname=" + gTsSettings.nickname + "\nwineprefix=" + gTsSettings.winePrefix +
		"\nport=" + port + "\n");
}

// The wine prefix the GTA folder is in (…/drive_c/…), unless one is set.
static std::string
tsWinePrefix(void)
{
	if(gTsSettings.winePrefix[0])
		return gTsSettings.winePrefix;
	std::string game = tsGameDir();
	size_t at = game.find("/drive_c/");
	return at == std::string::npos ? "" : game.substr(0, at);
}

// ---- log ------------------------------------------------------------------

struct TsCompileMessage {
	std::string file;
	int line;
	bool error;
	std::string text;
};

static std::deque<std::string> gTsLog;
static bool gTsLogScroll;
static std::vector<TsCompileMessage> gTsCompileMessages;
static std::string gTsStatus = "Not running";

static void
tsLogLine(const std::string &line)
{
	// Drop terminal colour codes.
	std::string clean;
	for(size_t i = 0; i < line.size(); i++){
		if(line[i] == '\x1b' && i + 1 < line.size() && line[i+1] == '['){
			i += 2;
			while(i < line.size() && !(line[i] >= '@' && line[i] <= '~'))
				i++;
			continue;
		}
		if(line[i] != '\r')
			clean += line[i];
	}
	gTsLog.push_back(clean);
	while(gTsLog.size() > 5000)
		gTsLog.pop_front();
	gTsLogScroll = true;
}

static void
tsLogf(const char *fmt, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	tsLogLine(std::string("[Haumea] ") + buf);
	log("TestServer: %s\n", buf);
}

// ---- processes ------------------------------------------------------------

struct TsProcess {
#ifdef _WIN32
	HANDLE process = nil, out = nil, in = nil;
#else
	pid_t pid = -1;
	int out = -1, in = -1;
#endif
	std::string output;	// everything read so far (the compiler's is short)
	std::string partial;	// the line being read
	bool keepOutput = false;
	int exitCode = 0;
};

static std::string
tsQuote(const std::string &arg)
{
	std::string q = "\"";
#ifdef _WIN32
	// Windows' rule (CommandLineToArgvW, the C runtime): backslashes are
	// literal unless they come before a quote, so C:\dir stays C:\dir and
	// only runs before a quote, or the closing one, are doubled.
	size_t backslashes = 0;
	for(size_t i = 0; i < arg.size(); i++){
		if(arg[i] == '\\'){
			backslashes++;
			continue;
		}
		if(arg[i] == '"'){
			q.append(backslashes * 2 + 1, '\\');
			q += '"';
		}else{
			q.append(backslashes, '\\');
			q += arg[i];
		}
		backslashes = 0;
	}
	q.append(backslashes * 2, '\\');
#else
	for(size_t i = 0; i < arg.size(); i++){
		if(arg[i] == '"' || arg[i] == '\\')
			q += '\\';
		q += arg[i];
	}
#endif
	return q + "\"";
}

#ifndef _WIN32
extern char **environ;

// argv and an environment (the current one plus env's NAME=value entries) as
// the char* arrays exec wants, built before fork: the child of a process with
// threads (the GL driver's) must not allocate.
struct TsExecArgs {
	std::vector<std::string> strings;
	std::vector<char*> argv, envp;
};

static void
tsMakeExecArgs(TsExecArgs *out, const std::vector<std::string> &argv, const std::vector<std::string> &env)
{
	out->strings = argv;
	for(char **e = environ; *e; e++){
		std::string entry = *e;
		bool replaced = false;
		for(size_t i = 0; i < env.size(); i++)
			replaced |= entry.compare(0, env[i].find('=') + 1, env[i], 0, env[i].find('=') + 1) == 0;
		if(!replaced)
			out->strings.push_back(entry);
	}
	out->strings.insert(out->strings.end(), env.begin(), env.end());
	for(size_t i = 0; i < argv.size(); i++)
		out->argv.push_back((char*)out->strings[i].c_str());
	out->argv.push_back(nil);
	for(size_t i = argv.size(); i < out->strings.size(); i++)
		out->envp.push_back((char*)out->strings[i].c_str());
	out->envp.push_back(nil);
}
#endif

// Starts argv in dir with stdout and stderr piped together and stdin open.
// env is a list of NAME=value added to the environment.
static bool
tsSpawn(TsProcess *p, const std::vector<std::string> &argv, const std::string &dir,
	const std::vector<std::string> &env)
{
	*p = TsProcess();
#ifdef _WIN32
	SECURITY_ATTRIBUTES sa = { sizeof(sa), nil, TRUE };
	HANDLE outRead, outWrite, inRead, inWrite;
	if(!CreatePipe(&outRead, &outWrite, &sa, 0))
		return false;
	if(!CreatePipe(&inRead, &inWrite, &sa, 0)){
		CloseHandle(outRead);
		CloseHandle(outWrite);
		return false;
	}
	SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
	std::string commandLine;
	for(size_t i = 0; i < argv.size(); i++)
		commandLine += (i ? " " : "") + tsQuote(argv[i]);
	for(size_t i = 0; i < env.size(); i++){
		size_t eq = env[i].find('=');
		SetEnvironmentVariableA(env[i].substr(0, eq).c_str(), env[i].substr(eq + 1).c_str());
	}
	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = outWrite;
	si.hStdError = outWrite;
	si.hStdInput = inRead;
	PROCESS_INFORMATION pi;
	std::vector<char> cmd(commandLine.begin(), commandLine.end());
	cmd.push_back('\0');
	BOOL ok = CreateProcessA(nil, cmd.data(), nil, nil, TRUE, CREATE_NO_WINDOW, nil,
		dir.c_str(), &si, &pi);
	CloseHandle(outWrite);
	CloseHandle(inRead);
	if(!ok){
		CloseHandle(outRead);
		CloseHandle(inWrite);
		return false;
	}
	CloseHandle(pi.hThread);
	p->process = pi.hProcess;
	p->out = outRead;
	p->in = inWrite;
	return true;
#else
	TsExecArgs exec;
	tsMakeExecArgs(&exec, argv, env);
	int outPipe[2], inPipe[2];
	if(pipe(outPipe) != 0)
		return false;
	if(pipe(inPipe) != 0){
		close(outPipe[0]);
		close(outPipe[1]);
		return false;
	}
	pid_t pid = fork();
	if(pid < 0){
		close(outPipe[0]); close(outPipe[1]);
		close(inPipe[0]); close(inPipe[1]);
		return false;
	}
	if(pid == 0){
		// Its own process group, so stopping it also stops wine's children,
		// and on Linux it goes when Haumea does, even if Haumea crashes.
		setpgid(0, 0);
#ifdef __linux__
		prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
		dup2(outPipe[1], 1);
		dup2(outPipe[1], 2);
		dup2(inPipe[0], 0);
		close(outPipe[0]); close(outPipe[1]);
		close(inPipe[0]); close(inPipe[1]);
		if(chdir(dir.c_str()) != 0)
			_exit(127);
		environ = exec.envp.data();
		execvp(exec.argv[0], exec.argv.data());
		// ENOENT for a file that is there: its ELF loader is missing, as with
		// open.mp's 32-bit Linux server and compiler on a 64-bit system.
		static const char failed[] = "could not start the program\n";
		static const char noLoader[] = "could not start the program: it is a 32-bit program and the "
			"32-bit C library is missing. On Debian or Ubuntu: sudo dpkg --add-architecture i386 && "
			"sudo apt update && sudo apt install libc6:i386 libatomic1:i386\n";
		bool missingLoader = errno == ENOENT && access(exec.argv[0], F_OK) == 0;
		ssize_t ignored = missingLoader ? write(2, noLoader, sizeof(noLoader) - 1) :
			write(2, failed, sizeof(failed) - 1);
		(void)ignored;
		_exit(127);
	}
	setpgid(pid, pid);
	close(outPipe[1]);
	close(inPipe[0]);
	fcntl(outPipe[0], F_SETFL, fcntl(outPipe[0], F_GETFL) | O_NONBLOCK);
	p->pid = pid;
	p->out = outPipe[0];
	p->in = inPipe[1];
	return true;
#endif
}

static bool
tsRunning(const TsProcess *p)
{
#ifdef _WIN32
	return p->process != nil;
#else
	return p->pid > 0;
#endif
}

// Reads what the process wrote, line by line into onLine. Returns false
// once it has exited (exitCode set).
template <typename F> static bool
tsPoll(TsProcess *p, F onLine)
{
	if(!tsRunning(p))
		return false;
	std::string chunk;
	char buf[4096];
#ifdef _WIN32
	DWORD avail = 0, n = 0;
	while(PeekNamedPipe(p->out, nil, 0, nil, &avail, nil) && avail > 0 &&
	      ReadFile(p->out, buf, avail < sizeof(buf) ? avail : (DWORD)sizeof(buf), &n, nil) && n > 0)
		chunk.append(buf, n);
	bool exited = WaitForSingleObject(p->process, 0) == WAIT_OBJECT_0;
	if(exited){
		while(PeekNamedPipe(p->out, nil, 0, nil, &avail, nil) && avail > 0 &&
		      ReadFile(p->out, buf, avail < sizeof(buf) ? avail : (DWORD)sizeof(buf), &n, nil) && n > 0)
			chunk.append(buf, n);
		DWORD code = 0;
		GetExitCodeProcess(p->process, &code);
		p->exitCode = (int)code;
	}
#else
	ssize_t n;
	while((n = read(p->out, buf, sizeof(buf))) > 0)
		chunk.append(buf, n);
	int status;
	bool exited = waitpid(p->pid, &status, WNOHANG) == p->pid;
	if(exited){
		fcntl(p->out, F_SETFL, fcntl(p->out, F_GETFL) & ~O_NONBLOCK);
		while((n = read(p->out, buf, sizeof(buf))) > 0)
			chunk.append(buf, n);
		p->exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	}
#endif
	if(p->keepOutput)
		p->output += chunk;
	p->partial += chunk;
	size_t nl;
	while((nl = p->partial.find('\n')) != std::string::npos){
		onLine(p->partial.substr(0, nl));
		p->partial.erase(0, nl + 1);
	}
	if(exited){
		if(!p->partial.empty())
			onLine(p->partial);
		p->partial.clear();
#ifdef _WIN32
		CloseHandle(p->process); CloseHandle(p->out); CloseHandle(p->in);
		p->process = nil;
#else
		close(p->out); close(p->in);
		p->pid = -1;
#endif
	}
	return !exited;
}

static void
tsWriteInput(TsProcess *p, const char *text)
{
	if(!tsRunning(p))
		return;
#ifdef _WIN32
	DWORD n;
	WriteFile(p->in, text, (DWORD)strlen(text), &n, nil);
#else
	ssize_t ignored = write(p->in, text, strlen(text));
	(void)ignored;
#endif
}

static void
tsKill(TsProcess *p)
{
	if(!tsRunning(p))
		return;
#ifdef _WIN32
	TerminateProcess(p->process, 1);
	WaitForSingleObject(p->process, 3000);
#else
	kill(-p->pid, SIGTERM);
	for(int i = 0; i < 30; i++){
		if(waitpid(p->pid, nil, WNOHANG) == p->pid){
			p->pid = 0;	// reaped; the poll below just closes the pipes
			break;
		}
		usleep(100000);
	}
	if(p->pid > 0){
		kill(-p->pid, SIGKILL);
		waitpid(p->pid, nil, 0);
	}
	close(p->out); close(p->in);
	p->pid = -1;
	return;
#endif
	tsPoll(p, [](const std::string &line){ tsLogLine(line); });
}

// ---- the server -----------------------------------------------------------

struct TsSource {
	std::string dir;	// has omp-server or omp-server.exe
	bool windows;		// only the Windows build is there
	std::string label;
};

enum TsState { TS_IDLE, TS_COMPILING, TS_RUNNING };

static TsState gTsState = TS_IDLE;
static TsProcess gTsCompiler, gTsServer;
static TsSource gTsSource;
static bool gTsReloadAfterCompile;	// the server is up: gmx instead of a start
static bool gTsWantsRestart;
static std::string gTsNeeds;	// what the last setup copied, to see when to restart
static std::string gTsCameraText;

static std::string
tsServerDir(void)
{
	return tsJoin(tsJoin(tsConfigDir(), "testserver"), "server");
}

static bool
tsHasServer(const std::string &dir, bool *windows)
{
#ifdef _WIN32
	*windows = true;
	return tsIsFile(tsJoin(dir, "omp-server.exe"));
#else
	if(tsIsFile(tsJoin(dir, "omp-server"))){
		*windows = false;
		return true;
	}
	*windows = true;
	return tsIsFile(tsJoin(dir, "omp-server.exe"));
#endif
}

// The server folder inside an extracted package (it is usually "Server").
static bool
tsFindServerIn(const std::string &dir, int depth, TsSource *out)
{
	bool windows;
	if(tsHasServer(dir, &windows)){
		out->dir = dir;
		out->windows = windows;
		return true;
	}
	if(depth == 0)
		return false;
	std::vector<std::string> names = tsListDir(dir);
	for(size_t i = 0; i < names.size(); i++)
		if(tsIsDir(tsJoin(dir, names[i])) && tsFindServerIn(tsJoin(dir, names[i]), depth - 1, out))
			return true;
	return false;
}

static bool
tsExtractPackage(const std::string &package, const std::string &dest, std::string *error)
{
	// Extract once per package file: the stamp names the file and its size.
	struct stat st;
	stat(package.c_str(), &st);
	char stamp[1200];
	snprintf(stamp, sizeof(stamp), "%s\n%lld\n", package.c_str(), (long long)st.st_size);
	std::string stampPath = tsJoin(dest, ".haumea-package");
	if(tsReadText(stampPath) == stamp)
		return true;
	if(!tsMakeDirs(dest)){
		*error = "could not create " + dest;
		return false;
	}
	bool zip = tsEndsWith(package, ".zip");
	std::string command;
#ifdef _WIN32
	// Windows 10's tar (bsdtar) reads both zip and tar.gz.
	command = "tar -xf " + tsQuote(package) + " -C " + tsQuote(dest);
#else
	command = zip ? "unzip -q -o " + tsQuote(package) + " -d " + tsQuote(dest)
		: "tar -xzf " + tsQuote(package) + " -C " + tsQuote(dest);
#endif
	tsLogf("Extracting %s", package.c_str());
	if(system(command.c_str()) != 0){
		*error = "could not extract " + package + (zip ? " (is unzip installed?)" : "");
		return false;
	}
	tsWriteText(stampPath, stamp);
	return true;
}

static bool
tsFindSource(TsSource *source, std::string *error)
{
	tsLoadSettings();
	std::string package = gTsSettings.package;
	if(!package.empty()){
		std::string dir = package;
		if(tsIsFile(package)){
			if(!tsEndsWith(package, ".zip") && !tsEndsWith(package, ".tar.gz") &&
			   !tsEndsWith(package, ".tgz")){
				*error = "the server package must be a folder, .tar.gz or .zip";
				return false;
			}
			dir = tsJoin(tsJoin(tsConfigDir(), "testserver"), "package");
			if(!tsExtractPackage(package, dir, error))
				return false;
		}
		if(tsFindServerIn(dir, 2, source)){
			source->label = package;
			return true;
		}
		*error = "no omp-server in " + package;
		return false;
	}
	std::string workspace = tsWorkspaceDir();
	if(!workspace.empty() && tsFindServerIn(workspace, 1, source)){
		source->label = "workspace " + workspace;
		return true;
	}
	*error = "set an open.mp server package (folder, .tar.gz or .zip) in Preferences > Test server";
	return false;
}

// What the map uses, from its exported code.
struct TsNeeds {
	bool npcs, streamer, customModels;
	std::string prefix;
	bool removeWorldObjects, showForPlayer;
	bool textDraws, soundTick, soundArea;	// helpers the gamemode has to call
	std::string textDrawFile;	// the textdraw file's prefix, when it has textdraws
};

static TsNeeds
tsScanMap(const std::string &code)
{
	TsNeeds needs = {};
	needs.npcs = code.find("NPC_") != std::string::npos;
	needs.streamer = code.find("#include <streamer>") != std::string::npos;
	// "stock <prefix>_CreateObjects()" (the header comments name it too).
	size_t at = code.find("_CreateObjects()\n{");
	if(at != std::string::npos){
		size_t begin = code.rfind("stock ", at);
		if(begin != std::string::npos)
			needs.prefix = code.substr(begin + 6, at - begin - 6);
	}
	needs.removeWorldObjects = code.find(needs.prefix + "_RemoveWorldObjectsForPlayer(") != std::string::npos;
	needs.showForPlayer = code.find(needs.prefix + "_ShowForPlayer(") != std::string::npos;
	needs.textDraws = code.find("stock " + needs.prefix + "_CreateTextDraws(") != std::string::npos;
	needs.soundTick = code.find("stock " + needs.prefix + "_SoundTick(") != std::string::npos;
	needs.soundArea = code.find("stock " + needs.prefix + "_SoundArea(") != std::string::npos;
	return needs;
}

// Copies a file found in the first of the folders that has it.
static bool
tsCopyFirst(const std::vector<std::string> &dirs, const char *name, const std::string &to)
{
	for(size_t i = 0; i < dirs.size(); i++){
		if(dirs[i].empty())
			continue;
		std::string from = tsFindName(dirs[i], name);
		if(!from.empty() && tsIsFile(from))
			return tsCopyFile(from, to);
	}
	return false;
}

static const char *tsBaseComponents[] = {
	"$CAPI", "Actors", "Checkpoints", "Classes", "Console", "Databases", "Dialogs",
	"GangZones", "LegacyConfig", "LegacyNetwork", "Menus", "Objects", "Pawn",
	"Pickups", "TextDraws", "TextLabels", "Timers", "Variables", "Vehicles",
};

static bool
tsSetup(const TsSource &source, TsNeeds *needs, std::string *error)
{
	std::string server = tsServerDir();
	std::string workspace = tsWorkspaceDir();
	const char *ext = source.windows ? ".dll" : ".so";
	const char *exe = source.windows ? "omp-server.exe" : "omp-server";
	if(!tsCopyFile(tsJoin(source.dir, exe), tsJoin(server, exe))){
		*error = std::string("could not copy ") + exe;
		return false;
	}
	// Windows builds keep their DLLs next to the exe.
	std::vector<std::string> names = tsListDir(source.dir);
	for(size_t i = 0; i < names.size(); i++)
		if(source.windows && tsEndsWith(names[i], ".dll"))
			tsCopyFile(tsJoin(source.dir, names[i]), tsJoin(server, names[i]));

	// Components: open.mp's own, then the ones the map needs.
	std::vector<std::string> components(tsBaseComponents,
		tsBaseComponents + sizeof(tsBaseComponents)/sizeof(tsBaseComponents[0]));
	std::string customModels = workspace.empty() ? "" : tsJoin(workspace, "models");
	needs->customModels = !customModels.empty() && tsIsFile(tsJoin(customModels, "artconfig.txt"));
	if(needs->npcs){
		components.push_back("NPCs");
		components.push_back("Recordings");
	}
	if(needs->customModels)
		components.push_back("CustomModels");
	std::string componentDir = tsJoin(server, "components");
	std::vector<std::string> old = tsListDir(componentDir);
	for(size_t i = 0; i < old.size(); i++)
		remove(tsJoin(componentDir, old[i]).c_str());
	for(size_t i = 0; i < components.size(); i++){
		std::string name = components[i] + ext;
		if(!tsCopyFile(tsJoin(tsJoin(source.dir, "components"), name), tsJoin(componentDir, name)) &&
		   components[i] != "$CAPI")
			tsLogf("The server package has no %s", name.c_str());
	}

	// The compiler and its includes.
	if(!tsCopyTree(tsJoin(source.dir, "qawno"), tsJoin(server, "qawno"))){
		*error = "the server package has no qawno folder (pawncc and includes)";
		return false;
	}

	std::vector<std::string> plugins;
	if(needs->streamer){
		std::string plugin = std::string("streamer") + ext;
		if(tsCopyFirst({ tsJoin(source.dir, "plugins"), workspace.empty() ? "" : tsJoin(workspace, "plugins") },
		               plugin.c_str(), tsJoin(tsJoin(server, "plugins"), plugin)))
			plugins.push_back("streamer");
		else
			tsLogf("No %s in the package or workspace plugins folder; streamer objects will not load", plugin.c_str());
		std::string include = tsJoin(tsJoin(tsJoin(server, "qawno"), "include"), "streamer.inc");
		if(!tsCopyFirst({ tsJoin(tsJoin(source.dir, "qawno"), "include"),
		                  workspace.empty() ? "" : tsJoin(tsJoin(workspace, "qawno"), "include"),
		                  workspace.empty() ? "" : tsJoin(tsJoin(workspace, "pawno"), "include") },
		                "streamer.inc", include)){
			*error = "the map uses the streamer, but no streamer.inc was found in the package or workspace";
			return false;
		}
	}

	if(needs->npcs){
		// Recordings and node files: the package's, the workspace's, then the
		// game's own path nodes (the NPC component reads the same files).
		std::string recordings = tsJoin(tsJoin(server, "npcmodes"), "recordings");
		tsCopyTree(tsJoin(tsJoin(source.dir, "npcmodes"), "recordings"), recordings);
		if(!workspace.empty())
			tsCopyTree(tsJoin(tsJoin(workspace, "npcmodes"), "recordings"), recordings);
		std::string nodes = tsJoin(tsJoin(tsJoin(server, "scriptfiles"), "NPCs"), "nodes");
		std::vector<std::string> nodeDirs = {
			workspace.empty() ? "" : tsJoin(tsJoin(tsJoin(workspace, "scriptfiles"), "NPCs"), "nodes"),
			tsJoin(tsJoin(tsJoin(source.dir, "scriptfiles"), "NPCs"), "nodes"),
		};
		std::string data = tsFindName(tsGameDir(), "data");
		std::string paths = data.empty() ? "" : tsFindName(data, "Paths");
		nodeDirs.push_back(paths);
		for(int i = 0; i < 64; i++){
			char name[32];
			snprintf(name, sizeof(name), "NODES%d.DAT", i);
			tsCopyFirst(nodeDirs, name, tsJoin(nodes, name));
		}
	}
	if(needs->customModels)
		tsCopyTree(customModels, tsJoin(server, "models"));

	// config.json: our gamemode, our port, nothing announced.
	std::string pluginList;
	for(size_t i = 0; i < plugins.size(); i++)
		pluginList += (i ? ", \"" : "\"") + plugins[i] + "\"";
	char config[2048];
	snprintf(config, sizeof(config),
		"{\n"
		"\t\"announce\": false,\n"
		"\t\"enable_query\": true,\n"
		"\t\"max_players\": 10,\n"
		"\t\"max_bots\": %d,\n"
		"\t\"name\": \"Haumea test server\",\n"
		"\t\"artwork\": { \"enable\": %s, \"models_path\": \"models\", \"port\": %d, \"web_server_bind\": \"127.0.0.1\" },\n"
		"\t\"game\": { \"use_all_animations\": true, \"use_player_ped_anims\": true },\n"
		"\t\"logging\": { \"enable\": true, \"file\": \"log.txt\" },\n"
		"\t\"network\": { \"port\": %d, \"bind\": \"127.0.0.1\", \"public_addr\": \"127.0.0.1\" },\n"
		"\t\"pawn\": {\n"
		"\t\t\"legacy_plugins\": [%s],\n"
		"\t\t\"main_scripts\": [\"haumea_test 1\"],\n"
		"\t\t\"side_scripts\": []\n"
		"\t},\n"
		"\t\"rcon\": { \"enable\": false, \"password\": \"\" }\n"
		"}\n",
		needs->npcs ? 100 : 0, needs->customModels ? "true" : "false", gTsSettings.port,
		gTsSettings.port, pluginList.c_str());
	tsWriteText(tsJoin(server, "config.json"), config);
	return true;
}

// The camera, ground under it and facing, for the spawn and /tp.
struct TsSpawn {
	float x, y, z, angle;
	int interior;
};

static TsSpawn
tsCameraSpawn(void)
{
	TsSpawn s;
	rw::V3d pos = TheCamera.m_position;
	rw::V3d at = TheCamera.m_at;
	rw::V3d hit;
	s.x = pos.x;
	s.y = pos.y;
	s.z = pos.z;
	if(GetGroundPlacementSurface(pos, &hit, nil, true) && hit.z < pos.z)
		s.z = hit.z + 1.0f;
	s.angle = atan2f(-at.x, at.y) * 180.0f / 3.14159265f;
	if(s.angle < 0.0f)
		s.angle += 360.0f;
	s.interior = currentArea > 0 ? currentArea : 0;
	return s;
}

static std::string
tsGamemode(const TsNeeds &needs)
{
	TsSpawn s = tsCameraSpawn();
	int weather = Weather::newWeather;
	char head[1024];
	snprintf(head, sizeof(head),
		"// Written by Haumea's Test in game; rewritten on every run.\n"
		"#include <open.mp>\n"
		"#include \"haumea_map.pwn\"\n"
		"%s"
		"\n"
		"#define HT_SPAWN_X (%.4f)\n"
		"#define HT_SPAWN_Y (%.4f)\n"
		"#define HT_SPAWN_Z (%.4f)\n"
		"#define HT_SPAWN_A (%.4f)\n"
		"#define HT_INTERIOR (%d)\n"
		"#define HT_HOUR (%d)\n"
		"#define HT_MINUTE (%d)\n"
		"#define HT_WEATHER (%d)\n"
		"\n",
		needs.textDrawFile.empty() ? "" : "#include \"haumea_textdraws.pwn\"\n",
		s.x, s.y, s.z, s.angle, s.interior, currentHour, currentMinute, weather);
	std::string p = needs.prefix;
	std::string code = head;
	code +=
		"main() {}\n"
		"\n"
		"public OnGameModeInit()\n"
		"{\n"
		"\tSetGameModeText(\"Haumea test\");\n"
		"\tSetWorldTime(HT_HOUR);\n"
		"\tSetWeather(HT_WEATHER);\n"
		"\tAddPlayerClass(0, HT_SPAWN_X, HT_SPAWN_Y, HT_SPAWN_Z, HT_SPAWN_A);\n"
		"\t" + p + "_CreateObjects();\n";
	// Whatever else the map's header asks for.
	if(needs.textDraws)
		code += "\t" + p + "_CreateTextDraws();\n";
	if(!needs.textDrawFile.empty())
		code += "\t" + needs.textDrawFile + "_CreateTextDraws();\n";
	if(needs.soundTick)
		code += "\tSetTimer(\"HT_SoundTick\", 500, true);\n";
	code +=
		"\tprint(\"[Haumea] map loaded\");\n"
		"\treturn 1;\n"
		"}\n"
		"\n";
	if(needs.soundTick)
		code +=
			"forward HT_SoundTick();\n"
			"public HT_SoundTick()\n"
			"{\n"
			"\tfor(new i = 0; i < MAX_PLAYERS; i++)\n"
			"\t\tif(IsPlayerConnected(i) && !IsPlayerNPC(i))\n"
			"\t\t\t" + p + "_SoundTick(i);\n"
			"}\n"
			"\n";
	// Untagged areaid matches the streamer's forward with tags off, whichever
	// way the include spells STREAMER_TAG_AREA.
	if(needs.soundArea)
		code +=
			"public OnPlayerEnterDynamicArea(playerid, areaid)\n"
			"{\n"
			"\t" + p + "_SoundArea(playerid, areaid, true);\n"
			"\treturn 1;\n"
			"}\n"
			"\n"
			"public OnPlayerLeaveDynamicArea(playerid, areaid)\n"
			"{\n"
			"\t" + p + "_SoundArea(playerid, areaid, false);\n"
			"\treturn 1;\n"
			"}\n"
			"\n";
	code +=
		"public OnPlayerConnect(playerid)\n"
		"{\n";
	if(needs.removeWorldObjects)
		code += "\t" + p + "_RemoveWorldObjectsForPlayer(playerid);\n";
	if(needs.showForPlayer)
		code += "\t" + p + "_ShowForPlayer(playerid);\n";
	code +=
		"\treturn 1;\n"
		"}\n"
		"\n"
		"// Straight to the spawn: no class selection.\n"
		"public OnPlayerRequestClass(playerid, classid)\n"
		"{\n"
		"\tSetSpawnInfo(playerid, NO_TEAM, 0, HT_SPAWN_X, HT_SPAWN_Y, HT_SPAWN_Z, HT_SPAWN_A);\n"
		"\tTogglePlayerSpectating(playerid, true);\n"
		"\tSetTimerEx(\"HT_Spawn\", 100, false, \"i\", playerid);\n"
		"\treturn 1;\n"
		"}\n"
		"\n"
		"forward HT_Spawn(playerid);\n"
		"public HT_Spawn(playerid)\n"
		"{\n"
		"\tTogglePlayerSpectating(playerid, false);\n"
		"}\n"
		"\n"
		"public OnPlayerSpawn(playerid)\n"
		"{\n"
		"\tSetPlayerInterior(playerid, HT_INTERIOR);\n"
		"\tSetPlayerPos(playerid, HT_SPAWN_X, HT_SPAWN_Y, HT_SPAWN_Z);\n"
		"\tSetPlayerFacingAngle(playerid, HT_SPAWN_A);\n"
		"\tSetPlayerTime(playerid, HT_HOUR, HT_MINUTE);\n"
		"\tSetPlayerWeather(playerid, HT_WEATHER);\n"
		"\tSetCameraBehindPlayer(playerid);\n"
		+ (needs.textDraws ? "\t" + p + "_ShowTextDraws(playerid);\n" : std::string()) +
		(needs.textDrawFile.empty() ? std::string() : "\t" + needs.textDrawFile + "_ShowTextDraws(playerid);\n") +
		"\tSendClientMessage(playerid, 0xA0C0FFFF, \"Haumea test: /v <model> spawns a vehicle, /tp goes to the editor camera, /reload reloads the map.\");\n"
		"\treturn 1;\n"
		"}\n"
		"\n"
		"// The editor camera, written by Haumea: x y z angle interior, one per line.\n"
		"static bool:HT_ReadCamera(&Float:x, &Float:y, &Float:z, &Float:angle, &interior)\n"
		"{\n"
		"\tnew File:file = fopen(\"haumea_camera.txt\", io_read);\n"
		"\tif(!file)\n"
		"\t\treturn false;\n"
		"\tnew line[32], Float:values[4], count;\n"
		"\twhile(count < 5 && fread(file, line))\n"
		"\t{\n"
		"\t\tif(count < 4)\n"
		"\t\t\tvalues[count] = floatstr(line);\n"
		"\t\telse\n"
		"\t\t\tinterior = strval(line);\n"
		"\t\tcount++;\n"
		"\t}\n"
		"\tfclose(file);\n"
		"\tx = values[0];\n"
		"\ty = values[1];\n"
		"\tz = values[2];\n"
		"\tangle = values[3];\n"
		"\treturn count == 5;\n"
		"}\n"
		"\n"
		"public OnPlayerCommandText(playerid, cmdtext[])\n"
		"{\n"
		"\tif(!strcmp(cmdtext, \"/tp\", true))\n"
		"\t{\n"
		"\t\tnew Float:x, Float:y, Float:z, Float:angle, interior;\n"
		"\t\tif(!HT_ReadCamera(x, y, z, angle, interior))\n"
		"\t\t\treturn SendClientMessage(playerid, 0xFF8080FF, \"No camera position from Haumea yet.\");\n"
		"\t\tSetPlayerInterior(playerid, interior);\n"
		"\t\tif(IsPlayerInAnyVehicle(playerid))\n"
		"\t\t{\n"
		"\t\t\tnew vehicleid = GetPlayerVehicleID(playerid);\n"
		"\t\t\tLinkVehicleToInterior(vehicleid, interior);\n"
		"\t\t\tSetVehiclePos(vehicleid, x, y, z);\n"
		"\t\t\tSetVehicleZAngle(vehicleid, angle);\n"
		"\t\t}\n"
		"\t\telse\n"
		"\t\t{\n"
		"\t\t\tSetPlayerPos(playerid, x, y, z);\n"
		"\t\t\tSetPlayerFacingAngle(playerid, angle);\n"
		"\t\t}\n"
		"\t\tSetCameraBehindPlayer(playerid);\n"
		"\t\treturn 1;\n"
		"\t}\n"
		"\tif(!strcmp(cmdtext, \"/reload\", true))\n"
		"\t{\n"
		"\t\tSendClientMessageToAll(0xA0C0FFFF, \"Reloading the map...\");\n"
		"\t\tSendRconCommand(\"gmx\");\n"
		"\t\treturn 1;\n"
		"\t}\n"
		"\tif(!strcmp(cmdtext, \"/v \", true, 3) || !strcmp(cmdtext, \"/v\", true))\n"
		"\t{\n"
		"\t\tnew model = strval(cmdtext[2]);\n"
		"\t\tif(model < 400 || model > 611)\n"
		"\t\t\treturn SendClientMessage(playerid, 0xFF8080FF, \"Use /v <model 400-611>.\");\n"
		"\t\tnew Float:x, Float:y, Float:z, Float:angle;\n"
		"\t\tGetPlayerPos(playerid, x, y, z);\n"
		"\t\tGetPlayerFacingAngle(playerid, angle);\n"
		"\t\tif(IsPlayerInAnyVehicle(playerid))\n"
		"\t\t\tDestroyVehicle(GetPlayerVehicleID(playerid));\n"
		"\t\tnew vehicleid = CreateVehicle(model, x, y, z + 0.5, angle, -1, -1, -1);\n"
		"\t\tLinkVehicleToInterior(vehicleid, GetPlayerInterior(playerid));\n"
		"\t\tSetVehicleVirtualWorld(vehicleid, GetPlayerVirtualWorld(playerid));\n"
		"\t\tPutPlayerInVehicle(playerid, vehicleid, 0);\n"
		"\t\treturn 1;\n"
		"\t}\n"
		"\treturn 0;\n"
		"}\n";
	if(needs.npcs)
		code +=
			"\n"
			"public OnNPCSpawn(npcid)\n"
			"{\n"
			"\tnew name[MAX_PLAYER_NAME];\n"
			"\tGetPlayerName(npcid, name, sizeof(name));\n"
			"\tprintf(\"[Haumea] NPC %d (%s) spawned\", npcid, name);\n"
			"\treturn 1;\n"
			"}\n";
	return code;
}

// pawncc's "file(line) : error 017: ..." lines.
static void
tsParseCompilerLine(const std::string &line)
{
	size_t open = line.find('(');
	size_t colon = line.find(") : ");
	if(open == std::string::npos || colon == std::string::npos || colon < open)
		return;
	std::string rest = line.substr(colon + 4);
	bool error = rest.compare(0, 5, "error") == 0 || rest.compare(0, 11, "fatal error") == 0;
	bool warning = rest.compare(0, 7, "warning") == 0;
	if(!error && !warning)
		return;
	TsCompileMessage m;
	m.file = line.substr(0, open);
	size_t slash = m.file.find_last_of("/\\");
	if(slash != std::string::npos)
		m.file = m.file.substr(slash + 1);
	m.line = atoi(line.c_str() + open + 1);
	m.error = error;
	m.text = rest;
	gTsCompileMessages.push_back(m);
}

// pawncc or wine pawncc.exe, and omp-server likewise.
static std::vector<std::string>
tsCommand(const std::string &program)
{
	std::vector<std::string> argv;
#ifndef _WIN32
	if(gTsSource.windows){
		argv.push_back("wine");
		argv.push_back(program + ".exe");
		return argv;
	}
	argv.push_back("./" + program);
#else
	argv.push_back(tsJoin(tsServerDir(), program + ".exe"));
#endif
	return argv;
}

static std::vector<std::string>
tsEnvironment(void)
{
	std::vector<std::string> env;
#ifndef _WIN32
	if(gTsSource.windows){
		std::string prefix = tsWinePrefix();
		if(!prefix.empty())
			env.push_back("WINEPREFIX=" + prefix);
		env.push_back("WINEDEBUG=-all");
	}else
		env.push_back("LD_LIBRARY_PATH=" + tsJoin(tsServerDir(), "qawno"));
#endif
	return env;
}

static void
tsStartServer(void)
{
	static bool stopOnExit;
	if(!stopOnExit){
		stopOnExit = true;
		atexit(TestServerShutdown);
	}
	std::vector<std::string> argv = tsCommand("omp-server");
	if(!tsSpawn(&gTsServer, argv, tsServerDir(), tsEnvironment())){
		gTsStatus = "Could not start omp-server";
		tsLogf("Could not start %s in %s", argv[0].c_str(), tsServerDir().c_str());
		gTsState = TS_IDLE;
		return;
	}
	gTsState = TS_RUNNING;
	gTsStatus = "Running";
	char status[64];
	snprintf(status, sizeof(status), "Running on 127.0.0.1:%d", gTsSettings.port);
	gTsStatus = status;
	tsLogf("Server started (%s), port %d", gTsSource.windows ? "Windows build under wine" : "native",
		gTsSettings.port);
}

static void
tsFail(const std::string &message)
{
	gTsStatus = "Failed: " + message;
	tsLogf("%s", message.c_str());
	ToastFor(TOAST_SAVE, 6.0f, "Test in game: %s", message.c_str());
}

void
TestServerRunMap(int pawnFormat)
{
	tsLoadSettings();
	if(gTsState == TS_COMPILING)
		return;
	std::string error;
	TsSource source;
	if(!tsFindSource(&source, &error)){
		tsFail(error);
		return;
	}
	gTsSource = source;
	std::string server = tsServerDir();
	std::string gamemodes = tsJoin(server, "gamemodes");
	if(!tsMakeDirs(gamemodes)){
		tsFail("could not create " + gamemodes);
		return;
	}

	// The map as it is now, saved only into the test server.
	std::string mapPath = tsJoin(gamemodes, "haumea_map.pwn");
	std::string code;
	int skipped = 0;
	if(BuildPawnCode(mapPath.c_str(), (PawnCodeFormat)pawnFormat, false, code, &skipped) < 0 ||
	   !tsWriteText(mapPath, EncodePawnFileText(code, GetPawnTextEncoding()))){
		tsFail("could not export the map");
		return;
	}
	TsNeeds needs = tsScanMap(code);
	if(needs.prefix.empty()){
		tsFail("the exported map has no _CreateObjects function");
		return;
	}
	// The textdraw file as it is now, beside the map.
	if(!GetTextDraws().empty()){
		std::string tdPath = tsJoin(gamemodes, "haumea_textdraws.pwn");
		if(!tsWriteText(tdPath, EncodePawnFileText(BuildTextDrawFileCode(tdPath.c_str()), GetPawnTextEncoding()))){
			tsFail("could not export the textdraws");
			return;
		}
		needs.textDrawFile = TextDrawFilePrefix(tdPath.c_str());
	}
	if(!tsSetup(source, &needs, &error)){
		tsFail(error);
		return;
	}
	if(!tsWriteText(tsJoin(gamemodes, "haumea_test.pwn"), tsGamemode(needs))){
		tsFail("could not write the test gamemode");
		return;
	}
	TestServerUpdate();	// the camera file for /tp

	// A change in components or plugins needs a fresh server; a map change
	// only a gamemode reload.
	std::string needKey = std::string(needs.npcs ? "n" : "") + (needs.streamer ? "s" : "") +
		(needs.customModels ? "c" : "") + source.dir;
	gTsReloadAfterCompile = tsRunning(&gTsServer) && needKey == gTsNeeds;
	if(tsRunning(&gTsServer) && !gTsReloadAfterCompile){
		tsLogf("The map's needs changed; restarting the server");
		tsKill(&gTsServer);
	}
	gTsNeeds = needKey;

	gTsCompileMessages.clear();
	std::vector<std::string> argv = tsCommand("qawno/pawncc");
	argv.push_back("gamemodes/haumea_test.pwn");
	argv.push_back("-iqawno/include");
	argv.push_back("-igamemodes");
	argv.push_back("-ogamemodes/haumea_test.amx");
	argv.push_back("-d3");
	argv.push_back("-;+");
	argv.push_back("-(+");
	tsLogf("Compiling %s with %s", tsJoin(gamemodes, "haumea_test.pwn").c_str(), argv[0].c_str());
	if(!tsSpawn(&gTsCompiler, argv, server, tsEnvironment())){
		tsFail("could not run pawncc");
		return;
	}
	gTsCompiler.keepOutput = true;
	gTsState = TS_COMPILING;
	gTsStatus = "Compiling...";
}

void
TestServerStop(void)
{
	if(tsRunning(&gTsServer)){
		tsLogf("Stopping the server");
		tsKill(&gTsServer);
	}
	if(gTsState == TS_RUNNING)
		gTsState = TS_IDLE;
	gTsStatus = "Stopped";
}

void
TestServerRestart(void)
{
	if(gTsState == TS_COMPILING)
		return;
	TestServerStop();
	if(!tsIsFile(tsJoin(tsJoin(tsServerDir(), "gamemodes"), "haumea_test.amx"))){
		tsFail("nothing to run yet; use Test in game first");
		return;
	}
	tsStartServer();
}

bool
TestServerIsRunning(void)
{
	return tsRunning(&gTsServer);
}

void
TestServerLaunchGame(void)
{
	tsLoadSettings();
	std::string game = tsGameDir();
	std::string client = tsFindName(game, "samp.exe");
	if(client.empty()){
		tsFail("no samp.exe in the game folder");
		return;
	}
	char address[64];
	snprintf(address, sizeof(address), "127.0.0.1:%d", gTsSettings.port);
	std::string nick = gTsSettings.nickname[0] ? gTsSettings.nickname : "Haumea";
#ifdef _WIN32
	// samp.exe takes the address; the nickname comes from its registry key.
	HKEY key;
	if(RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\SAMP", 0, nil, 0, KEY_SET_VALUE, nil, &key, nil) == ERROR_SUCCESS){
		RegSetValueExA(key, "PlayerName", 0, REG_SZ, (const BYTE*)nick.c_str(), (DWORD)nick.size() + 1);
		RegCloseKey(key);
	}
	std::string commandLine = tsQuote(client) + " " + address;
	std::vector<char> cmd(commandLine.begin(), commandLine.end());
	cmd.push_back('\0');
	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	if(!CreateProcessA(nil, cmd.data(), nil, nil, FALSE, 0, nil, game.c_str(), &si, &pi)){
		tsFail("could not start samp.exe");
		return;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
#else
	std::string prefix = tsWinePrefix();
	if(prefix.empty()){
		tsFail("the game folder is not in a wine prefix; set one in Preferences > Test server");
		return;
	}
	// The nickname goes to wine reg as an argument, never through a shell:
	// SA-MP names may contain $.
	std::vector<std::string> env = { "WINEPREFIX=" + prefix, "WINEDEBUG=-all" };
	TsExecArgs reg, play;
	tsMakeExecArgs(&reg, { "wine", "reg", "add", "HKCU\\Software\\SAMP", "/v", "PlayerName",
		"/d", nick, "/f" }, env);
	tsMakeExecArgs(&play, { "wine", "samp.exe", address }, env);
	pid_t pid = fork();
	if(pid == 0){
		setsid();
		if(chdir(game.c_str()) != 0)
			_exit(127);
		int devnull = open("/dev/null", O_RDWR);
		dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2);
		pid_t setName = fork();
		if(setName == 0){
			environ = reg.envp.data();
			execvp("wine", reg.argv.data());
			_exit(127);
		}
		if(setName > 0)
			waitpid(setName, nil, 0);
		environ = play.envp.data();
		execvp("wine", play.argv.data());
		_exit(127);
	}
	if(pid < 0){
		tsFail("could not start wine");
		return;
	}
#endif
	tsLogf("Launching %s to %s as %s", client.c_str(), address, nick.c_str());
}

void
TestServerUpdate(void)
{
	if(gTsState == TS_COMPILING &&
	   !tsPoll(&gTsCompiler, [](const std::string &line){ tsLogLine(line); tsParseCompilerLine(line); })){
		int errors = 0, warnings = 0;
		for(size_t i = 0; i < gTsCompileMessages.size(); i++)
			(gTsCompileMessages[i].error ? errors : warnings)++;
		bool built = gTsCompiler.exitCode == 0 && errors == 0 &&
			tsIsFile(tsJoin(tsJoin(tsServerDir(), "gamemodes"), "haumea_test.amx"));
		int compilerExit = gTsCompiler.exitCode;
		gTsCompiler = TsProcess();
		if(!built){
			gTsState = tsRunning(&gTsServer) ? TS_RUNNING : TS_IDLE;
			char message[128];
			if(compilerExit == 127 && errors == 0 && warnings == 0)
				snprintf(message, sizeof(message), "the compiler could not start (see the log)");
			else
				snprintf(message, sizeof(message), "the map did not compile (%d error(s), %d warning(s))",
					errors, warnings);
			tsFail(message);
		}else{
			tsLogf("Compiled: %d warning(s)", warnings);
			if(gTsReloadAfterCompile && tsRunning(&gTsServer)){
				gTsState = TS_RUNNING;
				tsWriteInput(&gTsServer, "gmx\n");
				tsLogf("Reloading the gamemode (gmx)");
				Toast(TOAST_SAVE, "Test server: map reloaded");
			}else{
				tsStartServer();
				Toast(TOAST_SAVE, "Test server started on port %d", gTsSettings.port);
			}
		}
	}
	if(tsRunning(&gTsServer) &&
	   !tsPoll(&gTsServer, [](const std::string &line){ tsLogLine(line); })){
		tsLogf("The server exited (code %d)", gTsServer.exitCode);
		gTsServer = TsProcess();
		gTsState = TS_IDLE;
		gTsStatus = "Exited";
	}

	// The camera for /tp, written when it moves while the server runs.
	if(tsRunning(&gTsServer) || gTsState == TS_COMPILING){
		TsSpawn s = tsCameraSpawn();
		char text[160];
		snprintf(text, sizeof(text), "%.3f\n%.3f\n%.3f\n%.1f\n%d\n", s.x, s.y, s.z, s.angle, s.interior);
		if(gTsCameraText != text){
			static double lastWrite;
			double now = ImGui::GetTime();
			if(now - lastWrite > 0.5){
				lastWrite = now;
				gTsCameraText = text;
				tsWriteText(tsJoin(tsJoin(tsServerDir(), "scriptfiles"), "haumea_camera.txt"), text);
			}
		}
	}
}

void
TestServerGetReport(TestServerReport *report, int logLines)
{
	tsLoadSettings();
	report->state = gTsState == TS_COMPILING ? "compiling" : tsRunning(&gTsServer) ? "running" : "idle";
	report->status = gTsStatus;
	report->source = gTsSource.label;
	report->port = gTsSettings.port;
	report->compiler.clear();
	for(size_t i = 0; i < gTsCompileMessages.size(); i++){
		const TsCompileMessage &m = gTsCompileMessages[i];
		report->compiler.push_back(m.file + ":" + std::to_string(m.line) + ": " + m.text);
	}
	size_t first = logLines > 0 && gTsLog.size() > (size_t)logLines ? gTsLog.size() - logLines : 0;
	report->log.assign(gTsLog.begin() + first, gTsLog.end());
}

void
TestServerShutdown(void)
{
	tsKill(&gTsCompiler);
	tsKill(&gTsServer);
}

// ---- UI -------------------------------------------------------------------

void
TestServerWindow(bool *open, int pawnFormat)
{
	if(!ImGui::Begin(ICON_FA_GAMEPAD " Test server", open)){
		ImGui::End();
		return;
	}
	tsLoadSettings();
	ImGui::TextUnformatted(gTsStatus.c_str());
	if(!gTsSource.label.empty()){
		ImGui::SameLine();
		ImGui::TextDisabled("(%s)", gTsSource.label.c_str());
	}
	bool running = tsRunning(&gTsServer);
	if(ImGui::Button(running ? ICON_FA_ROTATE " Reload map" : ICON_FA_PLAY " Test in game"))
		TestServerRunMap(pawnFormat);
	ImGui::SameLine();
	ImGui::BeginDisabled(!running);
	if(ImGui::Button(ICON_FA_STOP " Stop"))
		TestServerStop();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(gTsState == TS_COMPILING);
	if(ImGui::Button(ICON_FA_ROTATE " Restart"))
		TestServerRestart();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if(ImGui::Button(ICON_FA_GAMEPAD " Launch game"))
		TestServerLaunchGame();
	ImGui::SameLine();
	if(ImGui::Button("Clear log"))
		gTsLog.clear();

	if(!gTsCompileMessages.empty()){
		ImGui::SeparatorText("Compiler");
		for(size_t i = 0; i < gTsCompileMessages.size(); i++){
			const TsCompileMessage &m = gTsCompileMessages[i];
			ImGui::TextColored(m.error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
				"%s:%d", m.file.c_str(), m.line);
			ImGui::SameLine();
			ImGui::TextWrapped("%s", m.text.c_str());
		}
	}
	ImGui::SeparatorText("Server log");
	ImGui::BeginChild("##testserverlog", ImVec2(0, 0), ImGuiChildFlags_Borders,
		ImGuiWindowFlags_HorizontalScrollbar);
	ImGuiListClipper clipper;
	clipper.Begin((int)gTsLog.size());
	while(clipper.Step())
		for(int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++){
			const std::string &line = gTsLog[i];
			bool bad = line.find("rror") != std::string::npos || line.find("arning") != std::string::npos;
			if(bad)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.4f, 1.0f));
			ImGui::TextUnformatted(line.c_str());
			if(bad)
				ImGui::PopStyleColor();
		}
	if(gTsLogScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetTextLineHeight() * 4)
		ImGui::SetScrollHereY(1.0f);
	gTsLogScroll = false;
	ImGui::EndChild();
	ImGui::End();
}

void
TestServerPreferences(void)
{
	tsLoadSettings();
	bool changed = false;
	changed |= ImGui::InputTextWithHint("Server package", "folder, open.mp .tar.gz or .zip",
		gTsSettings.package, sizeof(gTsSettings.package));
	ImGui::SetItemTooltip("Test in game runs the map on a private open.mp server in Haumea's config\n"
		"folder. Its files come from this package, or from the workspace server when empty.");
	changed |= ImGui::InputInt("Port", &gTsSettings.port);
	gTsSettings.port = clamp(gTsSettings.port, 1024, 65535);
	changed |= ImGui::InputText("Nickname", gTsSettings.nickname, sizeof(gTsSettings.nickname));
#ifndef _WIN32
	std::string prefix = tsWinePrefix();
	changed |= ImGui::InputTextWithHint("Wine prefix", prefix.empty() ? "for samp.exe" : prefix.c_str(),
		gTsSettings.winePrefix, sizeof(gTsSettings.winePrefix));
#endif
	if(changed)
		tsSaveSettings();
}
