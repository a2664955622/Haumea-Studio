// SPDX-License-Identifier: GPL-2.0-or-later
#include "discordrpc.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <condition_variable>
#include <mutex>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

// Discord's documented RPC-over-IPC protocol, without credentials or HTTP:
// https://docs.discord.com/developers/topics/rpc#rpc-over-ipc
namespace DiscordRPC
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr size_t MaxPayload = 64 * 1024;
const char *ApplicationId = "1482423448121643200";

uint32_t read32(const char *bytes)
{
	const auto *p = (const unsigned char *)bytes;
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void append32(std::string &out, uint32_t n)
{
	for (int i = 0; i < 4; i++)
		out += (char)(n >> (8 * i));
}
// Only inspect root command/event fields; nested user data is never retained.
std::string rootString(const std::string &json, const char *key)
{
	int depth = 0;
	for (size_t i = 0; i < json.size(); i++) {
		char c = json[i];
		if (c == '{' || c == '[') {
			depth++;
			continue;
		}
		if (c == '}' || c == ']') {
			depth--;
			continue;
		}
		if (c != '"')
			continue;
		size_t start = ++i;
		while (i < json.size() && json[i] != '"') {
			if (json[i] == '\\')
				i++;
			i++;
		}
		if (i >= json.size())
			return {};
		if (depth != 1 || json.compare(start, i - start, key) != 0)
			continue;
		size_t at = i + 1;
		while (at < json.size() && strchr(" \t\r\n", json[at]))
			at++;
		if (at >= json.size() || json[at++] != ':')
			continue;
		while (at < json.size() && strchr(" \t\r\n", json[at]))
			at++;
		if (at >= json.size() || json[at++] != '"')
			continue;
		size_t end = json.find('"', at);
		if (end == std::string::npos)
			return {};
		return json.substr(at, end - at);
	}
	return {};
}

class Connection
{
#ifdef _WIN32
	HANDLE pipe = INVALID_HANDLE_VALUE;
	bool io(bool writing, void *data, DWORD size, DWORD &done)
	{
		OVERLAPPED request = {};
		request.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!request.hEvent)
			return false;
		BOOL ok = writing ? WriteFile(pipe, data, size, &done, &request)
				  : ReadFile(pipe, data, size, &done, &request);
		if (!ok && GetLastError() == ERROR_IO_PENDING) {
			DWORD wait = WaitForSingleObject(request.hEvent, 100);
			if (wait != WAIT_OBJECT_0)
				CancelIoEx(pipe, &request);
			// Completion must be drained before the stack OVERLAPPED/buffer is freed.
			ok = GetOverlappedResult(pipe, &request, &done, TRUE);
			if (wait != WAIT_OBJECT_0)
				ok = FALSE;
		}
		CloseHandle(request.hEvent);
		return ok != FALSE;
	}
#else
	int socket = -1;
#endif
      public:
	~Connection()
	{
		Close();
	}
	bool Open(const std::string &path)
	{
		Close();
#ifdef _WIN32
		pipe = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
				   FILE_FLAG_OVERLAPPED, nullptr);
		return pipe != INVALID_HANDLE_VALUE;
#else
		sockaddr_un address = {};
		address.sun_family = AF_UNIX;
		if (path.size() >= sizeof(address.sun_path))
			return false;
		memcpy(address.sun_path, path.c_str(), path.size() + 1);
		socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (socket < 0)
			return false;
		if (fcntl(socket, F_SETFL, O_NONBLOCK) < 0 || fcntl(socket, F_SETFD, FD_CLOEXEC) < 0) {
			Close();
			return false;
		}
#ifdef SO_NOSIGPIPE
		int on = 1;
		setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
		if (connect(socket, (sockaddr *)&address, sizeof(address)) == 0)
			return true;
		if (errno == EINPROGRESS) {
			pollfd fd = {socket, POLLOUT, 0};
			int error = 0;
			socklen_t length = sizeof(error);
			if (poll(&fd, 1, 50) > 0 && getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &length) == 0 &&
			    !error)
				return true;
		}
		Close();
		return false;
#endif
	}
	bool IsOpen() const
	{
#ifdef _WIN32
		return pipe != INVALID_HANDLE_VALUE;
#else
		return socket >= 0;
#endif
	}
	void Close()
	{
#ifdef _WIN32
		if (IsOpen())
			CloseHandle(pipe);
		pipe = INVALID_HANDLE_VALUE;
#else
		if (IsOpen())
			close(socket);
		socket = -1;
#endif
	}
	bool Send(uint32_t opcode, const std::string &payload)
	{
		if (!IsOpen() || payload.size() > MaxPayload)
			return false;
		std::string bytes;
		append32(bytes, opcode);
		append32(bytes, (uint32_t)payload.size());
		bytes += payload;
		size_t at = 0;
		auto deadline = Clock::now() + std::chrono::milliseconds(150);
		while (at < bytes.size() && Clock::now() < deadline) {
#ifdef _WIN32
			DWORD done = 0;
			if (!io(true, &bytes[at], (DWORD)(bytes.size() - at), done) || !done)
				return false;
			at += done;
#else
#ifdef MSG_NOSIGNAL
			int flags = MSG_NOSIGNAL;
#else
			int flags = 0;
#endif
			ssize_t done = send(socket, bytes.data() + at, bytes.size() - at, flags);
			if (done > 0) {
				at += (size_t)done;
				continue;
			}
			if (done < 0 && errno == EINTR)
				continue;
			if (done < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				pollfd fd = {socket, POLLOUT, 0};
				poll(&fd, 1, 20);
				continue;
			}
			return false;
#endif
		}
		return at == bytes.size();
	}
	// Positive: bytes read; -2: no data yet; -1: disconnect/error.
	int Read(char *buffer, size_t size)
	{
#ifdef _WIN32
		DWORD available = 0, done = 0;
		if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
			return -1;
		if (!available)
			return -2;
		if (!io(false, buffer, (DWORD)std::min(size, (size_t)available), done) || !done)
			return -1;
		return (int)done;
#else
		ssize_t done = recv(socket, buffer, size, 0);
		if (done > 0)
			return (int)done;
		if (done < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
			return -2;
		return -1;
#endif
	}
};
} // namespace

std::vector<std::string> Endpoints()
{
	std::vector<std::string> paths;
#ifdef _WIN32
	for (int i = 0; i < 10; i++)
		paths.push_back("\\\\?\\pipe\\discord-ipc-" + std::to_string(i));
#else
	std::vector<std::string> dirs;
	for (const char *key : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
		const char *value = getenv(key);
		if (value && *value && value[0] == '/')
			dirs.emplace_back(value);
	}
	dirs.emplace_back("/tmp");
	if (const char *runtime = getenv("XDG_RUNTIME_DIR")) {
		if (runtime[0] == '/') {
			std::string root = runtime;
			while (!root.empty() && root.back() == '/')
				root.pop_back();
			dirs.push_back(root + "/app/com.discordapp.Discord");
			dirs.push_back(root + "/snap.discord");
		}
	}
	for (std::string dir : dirs) {
		while (!dir.empty() && dir.back() == '/')
			dir.pop_back();
		for (int i = 0; i < 10; i++) {
			std::string path = dir + "/discord-ipc-" + std::to_string(i);
			if (std::find(paths.begin(), paths.end(), path) == paths.end())
				paths.push_back(std::move(path));
		}
	}
#endif
	return paths;
}

struct Client::Impl {
	std::vector<std::string> endpoints;
	std::chrono::milliseconds retryInterval, updateInterval;
	std::mutex mutex;
	std::condition_variable wake;
	bool quit = false, enabled = false;
	Workspace workspace = Workspace::Map;
	std::atomic<Status> status{Status::Disabled};
	std::thread worker;
	const long long started = (long long)std::time(nullptr);
#ifdef _WIN32
	const unsigned pid = GetCurrentProcessId();
#else
	const unsigned pid = (unsigned)getpid();
#endif
	unsigned nonce = 0;
	Impl(std::vector<std::string> endpoints, std::chrono::milliseconds retry, std::chrono::milliseconds update)
	    : endpoints(std::move(endpoints)), retryInterval(retry), updateInterval(update)
	{
		worker = std::thread(&Impl::Run, this);
	}
	~Impl()
	{
		Shutdown();
	}
	std::string Activity(Workspace mode, bool clear = false)
	{
		const char *state = mode == Workspace::Map	   ? "Map"
				    : mode == Workspace::TextDraws ? "TextDraws"
								   : "Code";
		const char *details = mode == Workspace::Map	     ? "Editing a map"
				      : mode == Workspace::TextDraws ? "Designing TextDraws"
								     : "Editing Pawn code";
		std::string activity = "null";
		if (!clear)
			activity = "{\"type\":0,\"details\":\"" + std::string(details) + "\",\"state\":\"" + state +
				   "\",\"timestamps\":{\"start\":" + std::to_string(started) + "},\"instance\":false}";
		return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) +
		       ",\"activity\":" + activity + "},\"nonce\":\"" + std::to_string(++nonce) + "\"}";
	}
	void Shutdown()
	{
		{
			std::lock_guard<std::mutex> guard(mutex);
			quit = true;
		}
		wake.notify_one();
		if (worker.joinable())
			worker.join();
	}
	void Run()
	{
		Connection connection;
		std::string received;
		bool ready = false, sent = false;
		Workspace last = Workspace::Map;
		auto retryAt = Clock::now(), sendAt = retryAt, handshakeDeadline = retryAt;
		auto disconnect = [&] {
			connection.Close();
			received.clear();
			ready = false;
			sent = false;
			retryAt = Clock::now() + retryInterval;
			status = Status::Waiting;
		};
		for (;;) {
			bool active, stop;
			Workspace mode;
			{
				std::lock_guard<std::mutex> guard(mutex);
				active = enabled;
				mode = workspace;
				stop = quit;
			}
			if (stop || !active) {
				if (ready)
					connection.Send(1, Activity(mode, true));
				connection.Close();
				received.clear();
				ready = false;
				sent = false;
				retryAt = Clock::now();
				status = Status::Disabled;
				if (stop)
					break;
				std::unique_lock<std::mutex> lock(mutex);
				wake.wait(lock, [&] { return quit || enabled; });
				continue;
			}
			if (!connection.IsOpen() && Clock::now() >= retryAt) {
				status = Status::Waiting;
				for (const std::string &path : endpoints)
					if (connection.Open(path))
						break;
				if (connection.IsOpen()) {
					if (!connection.Send(0, "{\"v\":1,\"client_id\":\"" +
								    std::string(ApplicationId) + "\"}"))
						disconnect();
					handshakeDeadline = Clock::now() + std::chrono::seconds(5);
				} else
					retryAt = Clock::now() + retryInterval;
			}
			if (connection.IsOpen()) {
				char buffer[4096];
				for (int iteration = 0; iteration < 8 && connection.IsOpen(); iteration++) {
					int length = connection.Read(buffer, sizeof(buffer));
					if (length == -2)
						break;
					if (length < 0) {
						disconnect();
						break;
					}
					received.append(buffer, (size_t)length);
					if (received.size() > MaxPayload + 8) {
						disconnect();
						break;
					}
					while (received.size() >= 8) {
						uint32_t opcode = read32(received.data()),
							 size = read32(received.data() + 4);
						if (size > MaxPayload) {
							disconnect();
							break;
						}
						if (received.size() < size + 8)
							break;
						std::string payload = received.substr(8, size);
						received.erase(0, size + 8);
						if (opcode == 2 || opcode > 4) {
							disconnect();
							break;
						}
						if (opcode == 3) {
							if (!connection.Send(4, payload)) {
								disconnect();
								break;
							}
						} else if (opcode == 1) {
							std::string event = rootString(payload, "evt");
							if (event == "ERROR") {
								disconnect();
								break;
							}
							if (!ready && event == "READY" &&
							    rootString(payload, "cmd") == "DISPATCH") {
								ready = true;
								status = Status::Connected;
								sendAt = Clock::now();
							}
						}
					}
				}
				if (connection.IsOpen() && !ready && Clock::now() >= handshakeDeadline)
					disconnect();
				if (ready && (!sent || last != mode) && Clock::now() >= sendAt) {
					if (connection.Send(1, Activity(mode))) {
						sent = true;
						last = mode;
						sendAt = Clock::now() + updateInterval;
					} else
						disconnect();
				}
			}
			std::unique_lock<std::mutex> lock(mutex);
			wake.wait_for(lock, std::chrono::milliseconds(100));
		}
	}
};
Client::Client(std::vector<std::string> endpoints, std::chrono::milliseconds retry, std::chrono::milliseconds update)
    : impl(new Impl(std::move(endpoints), retry, update))
{
}
Client::~Client() = default;
void Client::Update(bool enabled, Workspace workspace)
{
	{
		std::lock_guard<std::mutex> guard(impl->mutex);
		if (impl->enabled == enabled && impl->workspace == workspace)
			return;
		impl->enabled = enabled;
		impl->workspace = workspace;
	}
	impl->wake.notify_one();
}
Status Client::GetStatus() const
{
	return impl->status.load();
}
void Client::Shutdown()
{
	impl->Shutdown();
}
} // namespace DiscordRPC
