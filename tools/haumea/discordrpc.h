// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace DiscordRPC
{
enum class Workspace { Map, TextDraws, Code };
enum class Status { Disabled, Waiting, Connected };
std::vector<std::string> Endpoints();
// Local IPC only. Connection/retry/IO run off the rendering thread. The
// injectable endpoints and intervals let tests use a private fake Discord.
class Client
{
      public:
	explicit Client(std::vector<std::string> endpoints = Endpoints(),
			std::chrono::milliseconds retry = std::chrono::seconds(10),
			std::chrono::milliseconds update = std::chrono::seconds(15));
	~Client();
	void Update(bool enabled, Workspace workspace);
	Status GetStatus() const;
	void Shutdown();

      private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace DiscordRPC
