# Haumea Studio Namaka companion for Windows

This Windows x64 package contains Namaka's Python CLI and MCP server. The
CI build also includes the Haumea editor (`haumea.exe`, with its fonts and
starter prefabs) and dependency wheels for an offline setup; `BUILD_INFO.json`
says which (`editor_included`, `dependency_wheelhouse_included`). It does not
contain GTA files. Without `haumea.exe`, use it with a Windows Haumea build that
includes the authenticated agent bridge.

## Install

Install Python 3.10 or newer with the Windows `py` launcher; Python 3.14 sets up
fully offline. Extract this ZIP,
open PowerShell in the extracted folder, then run:

```powershell
.\setup-agent.ps1
```

The setup creates `.venv-agent` and installs the package and its MCP
dependencies. With Python 3.14 (picked when the `py` launcher has it) it
installs offline from the `wheelhouse` folder. Python 3.10–3.13 downloads only
the few compiled parts the wheelhouse has for 3.14 alone, so the first setup
needs the internet; without it, setup says so and stops.

## Connect the Windows editor

The editor and MCP process must share a port and token. In one PowerShell
session, create them and register the MCP server with your AI assistant. Any
MCP client works; Claude Code and Codex are shown here.

```powershell
$Port = '48731'
$Token = [guid]::NewGuid().ToString('N') + [guid]::NewGuid().ToString('N')
$Mcp = (Resolve-Path '.\.venv-agent\Scripts\haumea-mcp.exe').Path
```

**Claude Code:**

```powershell
claude mcp add haumea --scope user `
  -e "HAUMEA_ENGINE_TCP_PORT=$Port" `
  -e "HAUMEA_ENGINE_TOKEN=$Token" -- $Mcp
```

If an older `haumea` entry exists, remove it first with
`claude mcp remove haumea --scope user`. Restart Claude Code after registering.

**Codex** (the CLI, its IDE extension and the ChatGPT desktop app share
`%USERPROFILE%\.codex\config.toml`):

```powershell
codex mcp add haumea `
  --env "HAUMEA_ENGINE_TCP_PORT=$Port" `
  --env "HAUMEA_ENGINE_TOKEN=$Token" -- $Mcp
codex mcp list
```

If an older `haumea` entry exists, remove it first with `codex mcp remove haumea`.
The same entry written by hand in `config.toml` (single quotes keep the
backslashes as they are):

```toml
[mcp_servers.haumea]
command = 'C:\Tools\Haumea-Namaka\.venv-agent\Scripts\haumea-mcp.exe'
args = []

[mcp_servers.haumea.env]
HAUMEA_ENGINE_TCP_PORT = "48731"
HAUMEA_ENGINE_TOKEN = "the token"
```

Start a new Codex session after adding it.

**Other MCP clients** (Claude Desktop's `claude_desktop_config.json` and most
others) take an `mcpServers` JSON entry with `"command"` set to the
`haumea-mcp.exe` path, `"args": []`, and `HAUMEA_ENGINE_TCP_PORT` and
`HAUMEA_ENGINE_TOKEN` in `"env"`.

In that same PowerShell session, start the editor so it uses the same port
and token. Haumea runs from its own folder; on its first start it asks for the
GTA San Andreas folder and the workspace (your server folder):

```powershell
$env:HAUMEA_ENGINE_TCP_PORT = $Port
$env:HAUMEA_ENGINE_TOKEN = $Token
& 'C:\Tools\Haumea\haumea.exe'
```

Replace the example path with the editor's actual location (`.\haumea.exe` when this package includes it). Pass
`--game 'C:\Games\GTA San Andreas'` to use another game folder for that run. The
Windows editor must be built with Namaka support. The TCP listener binds
to loopback and requires the shared token.

Once the editor is open, check the connection from PowerShell:

```powershell
$env:HAUMEA_ENGINE_TCP_PORT = $Port
$env:HAUMEA_ENGINE_TOKEN = $Token
& (Join-Path (Split-Path $Mcp) 'haumeactl.exe') ping
```

The CLI can build a local GTA asset index with:

```powershell
& (Join-Path (Split-Path $Mcp) 'haumeactl.exe') assets index --gta-dir 'C:\Games\GTA San Andreas'
```

