# Haumea Studio Agent Alpha companion for Windows

This is a separate Windows x64 companion package. It contains Agent Alpha's
Python CLI and MCP server; it does not contain the Haumea editor or GTA files.
Use it with a Windows Haumea build that includes the authenticated agent bridge.

## Install

Install Python 3.10 or newer with the Windows `py` launcher. Extract this ZIP,
open PowerShell in the extracted folder, then run:

```powershell
.\setup-agent.ps1
```

The setup creates `.venv-agent` and installs the package and its MCP
dependencies. This package does not include a Windows dependency wheelhouse, so
the first setup needs access to the configured Python package index.

## Connect the Windows editor

The editor and MCP process must share a port and token. In one PowerShell
session, create them and register the MCP server with Claude Code:

```powershell
$Port = '48731'
$Token = [guid]::NewGuid().ToString('N') + [guid]::NewGuid().ToString('N')
$Mcp = (Resolve-Path '.\.venv-agent\Scripts\haumea-mcp.exe').Path

claude mcp add haumea --scope user `
  -e "HAUMEA_ENGINE_TCP_PORT=$Port" `
  -e "HAUMEA_ENGINE_TOKEN=$Token" -- $Mcp
```

If an older `haumea` entry exists, remove it first with
`claude mcp remove haumea --scope user`. Restart Claude Code after registering.

In that same PowerShell session, start the editor so it uses the same port
and token. Haumea runs from its own folder; on its first start it asks for the
GTA San Andreas folder and the workspace (your server folder):

```powershell
$env:HAUMEA_ENGINE_TCP_PORT = $Port
$env:HAUMEA_ENGINE_TOKEN = $Token
& 'C:\Tools\Haumea\haumea.exe'
```

Replace the example path with the editor's actual location. Pass
`--game 'C:\Games\GTA San Andreas'` to use another game folder for that run. The
Windows editor must be built with Agent Alpha support. The TCP listener binds
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

