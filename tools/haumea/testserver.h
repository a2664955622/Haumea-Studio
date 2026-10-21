// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <vector>

// Test in game: a private open.mp server under Haumea's config folder that
// runs the current map. It is set up from the workspace server or a server
// package (folder, Linux .tar.gz or Windows .zip); the user's own server is
// never written to.

// Exports the current map (unsaved changes included), writes a test
// gamemode, compiles it and starts the server, or reloads it when running.
void TestServerRunMap(int pawnFormat);
void TestServerStop(void);
void TestServerRestart(void);
bool TestServerIsRunning(void);
// Starts the SA-MP client through the game folder's wine prefix (or
// natively on Windows), connecting to the test server.
void TestServerLaunchGame(void);

// Per frame: reads the server and compiler output, keeps /tp's camera file.
void TestServerUpdate(void);
// Stops the server; call once on quit.
void TestServerShutdown(void);

// For the agent: state ("idle", "compiling" or "running"), the status line,
// the compiler's errors and warnings ("file:line: text") and the log's last
// lines (all of them when logLines is 0).
struct TestServerReport {
	std::string state, status, source;
	std::vector<std::string> compiler, log;
	int port;
};
void TestServerGetReport(TestServerReport *report, int logLines);

// The "Test server" panel and the Preferences section.
void TestServerWindow(bool *open, int pawnFormat);
void TestServerPreferences(void);
