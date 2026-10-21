#pragma once

// Native, main-thread control primitives for local visual agent loops. The
// engine owns scene safety and rendering; CLI/MCP concerns stay out-of-process.
void AgentBridgeUpdate(void);
void AgentBridgeCaptureAfterWorldRender(void);
void AgentBridgeCaptureAfterUiRender(void);	// capture_screen: with the panels
// The socket this editor listens on for Namaka, or "" (not listening, or TCP).
const char *AgentBridgeSocketPath(void);
// Another process (another Haumea?) answered on the socket, so this one isn't listening.
bool AgentBridgeSocketBusy(void);
// Listening for an agent, on a socket or loopback TCP.
bool AgentBridgeListening(void);
