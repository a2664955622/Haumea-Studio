#include "haumea.h"
#include "process.h"

#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

// Child processes with their output read line by line, on Windows and POSIX:
// the test server and its compiler, and Namaka's installer.


std::string
ProcessQuote(const std::string &arg)
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

void
ProcessMakeExecArgs(ProcessExecArgs *out, const std::vector<std::string> &argv, const std::vector<std::string> &env)
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
bool
ProcessSpawn(Process *p, const std::vector<std::string> &argv, const std::string &dir,
	const std::vector<std::string> &env)
{
	*p = Process();
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
		commandLine += (i ? " " : "") + ProcessQuote(argv[i]);
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
	ProcessExecArgs exec;
	ProcessMakeExecArgs(&exec, argv, env);
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

bool
ProcessRunning(const Process *p)
{
#ifdef _WIN32
	return p->process != nil;
#else
	return p->pid > 0;
#endif
}

// Reads what the process wrote, line by line into onLine. Returns false
// once it has exited (exitCode set).
bool
ProcessPoll(Process *p, const std::function<void(const std::string&)> &onLine)
{
	if(!ProcessRunning(p))
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

void
ProcessWriteInput(Process *p, const char *text)
{
	if(!ProcessRunning(p))
		return;
#ifdef _WIN32
	DWORD n;
	WriteFile(p->in, text, (DWORD)strlen(text), &n, nil);
#else
	ssize_t ignored = write(p->in, text, strlen(text));
	(void)ignored;
#endif
}

void
ProcessKill(Process *p, const std::function<void(const std::string&)> &onLine)
{
	if(!ProcessRunning(p))
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
	ProcessPoll(p, onLine);
}

