$ErrorActionPreference = 'Stop'
$Bundle = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Bundle
$Wheel = Get-ChildItem -File 'haumea_agent-*.whl' | Select-Object -First 1
if (-not $Wheel) { throw 'Haumea Agent Alpha wheel is missing from this bundle.' }

# Python 3.14 installs fully offline from the wheelhouse, so take it when the
# py launcher has it; any other 3.10+ downloads only its binary wheels.
if (-not (Test-Path '.venv-agent')) {
    & py -3.14 -c "" 2>$null
    if ($LASTEXITCODE -eq 0) { py -3.14 -m venv .venv-agent } else { py -3 -m venv .venv-agent }
}
$Python = Join-Path $Bundle '.venv-agent\Scripts\python.exe'
$PyVer = (& $Python -c "import sys; print('%d.%d' % sys.version_info[:2])").Trim()
& $Python -c "import sys; sys.exit(sys.version_info < (3, 10))"
if ($LASTEXITCODE -ne 0) { throw "Python 3.10 or newer is needed; this is Python $PyVer." }

$Constraints = ".\wheelhouse\constraints-$PyVer.txt"
$Installed = $false
if ((Test-Path 'wheelhouse') -and $PyVer -eq '3.14') {
    & $Python -m pip install --no-input --no-index --find-links '.\wheelhouse' "$($Wheel.FullName)[mcp]"
    $Installed = $LASTEXITCODE -eq 0
}
if (-not $Installed -and (Test-Path $Constraints)) {
    # Download only the pins no bundled wheel fits (the binary ones), then
    # install from the two folders with no index.
    $Missing = @(& $Python missing_wheels.py wheelhouse $Constraints | Where-Object { $_ })
    Write-Host "Python ${PyVer}: downloading the $($Missing.Count) parts Agent Alpha bundles only for Python 3.14..."
    if (Test-Path '.wheels-download') { Remove-Item -Recurse -Force '.wheels-download' }
    $ok = $true
    if ($Missing.Count -gt 0) {
        & $Python -m pip download --no-input --no-deps --only-binary=:all: -d '.wheels-download' @Missing
        $ok = $LASTEXITCODE -eq 0
    }
    if ($ok) {
        & $Python -m pip install --no-input --no-index --find-links '.\wheelhouse' --find-links '.\.wheels-download' -c $Constraints "$($Wheel.FullName)[mcp]"
        $ok = $LASTEXITCODE -eq 0
    }
    if (Test-Path '.wheels-download') { Remove-Item -Recurse -Force '.wheels-download' }
    if (-not $ok) { throw 'Agent Alpha needs Python 3.14 to install offline, or an internet connection.' }
    $Installed = $true
}
if (-not $Installed) {
    # A Python the bundle has no pins for: everything from the package index.
    & $Python -m pip install --no-input "$($Wheel.FullName)[mcp]"
    if ($LASTEXITCODE -ne 0) { throw 'Agent Alpha could not be installed; see the pip messages above.' }
}
Write-Host "Agent Alpha is installed. MCP command: $Bundle\.venv-agent\Scripts\haumea-mcp.exe"
