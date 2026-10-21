$ErrorActionPreference = 'Stop'
$Bundle = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Bundle
$Wheel = Get-ChildItem -File 'haumea_agent-*.whl' | Select-Object -First 1
if (-not $Wheel) { throw 'Haumea Agent Alpha wheel is missing from this bundle.' }

if (-not (Test-Path '.venv-agent')) { py -3 -m venv .venv-agent }
$Python = Join-Path $Bundle '.venv-agent\Scripts\python.exe'
if (Test-Path 'wheelhouse') {
    & $Python -m pip install --no-index --find-links '.\wheelhouse' "$($Wheel.FullName)[mcp]"
    if ($LASTEXITCODE -ne 0) {
        Write-Warning 'No compatible offline wheels found; trying the configured Python package index.'
        & $Python -m pip install "$($Wheel.FullName)[mcp]"
    }
} else {
    & $Python -m pip install "$($Wheel.FullName)[mcp]"
}
Write-Host "Agent Alpha is installed. MCP command: $Bundle\.venv-agent\Scripts\haumea-mcp.exe"
