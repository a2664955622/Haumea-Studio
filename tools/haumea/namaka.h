// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

// Namaka, Haumea's agent companion (formerly Agent Alpha): a Python MCP
// server installed from the wheel and wheelhouse the editor bundle ships in
// <install>/namaka/, into a venv at <config>/namaka/venv that editor updates
// never move. namaka.cpp.

// A venv exists at <config>/namaka/venv (not the legacy
// haumea-agent-alpha/.venv-agent, which keeps working but is never updated).
bool NamakaInstalled(void);
// The bundled wheel is newer than the venv's (PEP 440: 1.0.0.100 > 1.0.0.99,
// a +local label ignored for the comparison).
bool NamakaUpdateAvailable(void);
// After an editor update, when NamakaInstalled() && NamakaUpdateAvailable():
// reinstalls from the bundle in the background into a fresh venv and swaps it
// in only on success, so offline or failing it keeps the old one and says so.
// Waits behind an Install/Repair already running.
void NamakaUpdateAfterEditorUpdate(void);

// Listening for Namaka on a plain start: when it is installed, "Let Namaka
// connect" is on (Preferences, the panel) and this isn't a test or script run.
// The bridge then opens NamakaDefaultSocketPath() (agentbridge.cpp).
bool NamakaShouldListen(void);
bool NamakaListenEnabled(void);	// the switch, on by default
void SetNamakaListenEnabled(bool on);	// from the next start
// ~/.cache/haumea-agent.sock, haumea_ipc's default too, so MCP clients need
// no env; HAUMEA_NAMAKA_SOCKET moves it (tests). Creates ~/.cache (0700) if missing.
std::string NamakaDefaultSocketPath(void);
void NamakaListenSwitch(void);	// the checkbox, for Preferences too

// Tools > Namaka: Install / Repair / Uninstall, status and MCP config.
void DrawNamakaWindow(bool *open);
