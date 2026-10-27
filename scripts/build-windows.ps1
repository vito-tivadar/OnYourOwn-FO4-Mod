# Native Windows build: runs the host tests, builds OnYourOwn.dll and packages dist/OnYourOwn.zip.
# Requires xmake 3.x, Visual Studio 2022 (C++ workload) or Build Tools, Python 3 and git.
# Usage: scripts/build-windows.ps1 [-SkipTests]
param([switch]$SkipTests)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)

# PowerShell does not stop on failing native commands by itself.
function Invoke-Checked {
    $command, $arguments = $args
    & $command @arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $($args -join ' ')" }
}

if (-not (Test-Path 'lib/commonlibf4/xmake.lua')) {
    Invoke-Checked git submodule update --init --recursive
}
if (-not (Test-Path 'lib/commonlibf4/xmake.lua')) {
    throw 'lib/commonlibf4 is missing: the CommonLibF4 submodule is not registered in this repository (git submodule add https://github.com/libxse/commonlibf4.git lib/commonlibf4).'
}

if (-not $SkipTests) {
    Invoke-Checked xmake f -p windows -a x64 -m debug -o build/host-tests -y
    Invoke-Checked xmake build -g tests
    Invoke-Checked xmake run -g tests
}

Invoke-Checked xmake f -p windows -a x64 -m release -o build -y
Invoke-Checked xmake build OnYourOwn

New-Item -ItemType Directory -Force dist | Out-Null
Invoke-Checked python scripts/package.py build/windows/x64/release/OnYourOwn.dll dist/OnYourOwn.zip --data-root Data
Write-Host 'Ready: dist/OnYourOwn.zip'
