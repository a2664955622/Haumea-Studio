#pragma once

// Native, main-thread control primitives for local visual agent loops. The
// engine owns scene safety and rendering; CLI/MCP concerns stay out-of-process.
void AgentBridgeUpdate(void);
void AgentBridgeCaptureAfterWorldRender(void);
void AgentBridgeCaptureAfterUiRender(void);	// capture_screen: with the panels
// The socket this editor listens on for Namaka, or "" (not listening, or TCP).
const char *AgentBridgeSocketPath(void);
