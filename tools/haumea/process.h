#pragma once

#include <functional>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

// A child process with stdout and stderr piped together and stdin open
// (process.cpp): the test server and its compiler, Namaka's installer.
struct Process {
#ifdef _WIN32
	HANDLE process = nullptr, out = nullptr, in = nullptr;
#else
	pid_t pid = -1;
	int out = -1, in = -1;
#endif
	std::string output;	// everything read so far, with keepOutput
	std::string partial;	// the line being read
	bool keepOutput = false;
	int exitCode = 0;
};

// One argument quoted for a command line (Windows' rules on Windows).
std::string ProcessQuote(const std::string &arg);
// Starts argv in dir; env is a list of NAME=value added to the environment.
bool ProcessSpawn(Process *p, const std::vector<std::string> &argv, const std::string &dir,
	const std::vector<std::string> &env);
bool ProcessRunning(const Process *p);
// Reads what it wrote, a line at a time into onLine. False once it has
// exited (exitCode set).
bool ProcessPoll(Process *p, const std::function<void(const std::string&)> &onLine);
void ProcessWriteInput(Process *p, const char *text);
// Stops it and its children; onLine gets what it still had to say.
void ProcessKill(Process *p, const std::function<void(const std::string&)> &onLine);

#ifndef _WIN32
// argv and an environment (the current one plus env's NAME=value entries) as
// the char* arrays exec wants, built before fork: the child of a process with
// threads (the GL driver's) must not allocate.
struct ProcessExecArgs {
	std::vector<std::string> strings;
	std::vector<char*> argv, envp;
};
void ProcessMakeExecArgs(ProcessExecArgs *out, const std::vector<std::string> &argv,
	const std::vector<std::string> &env);
#endif
