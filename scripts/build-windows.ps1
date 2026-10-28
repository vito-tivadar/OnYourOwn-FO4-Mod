# Native Windows build: runs the host tests, builds OnYourOwn.dll for game 1.11.240 and 1.10.163 (old-gen)
# and packages dist/OnYourOwn-<version>.zip and dist/OnYourOwn-<version>-OldGen.zip.
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
    Invoke-Checked xmake f -p windows -a x64 -m debug --oldgen=n -o build/host-tests -y
    Invoke-Checked xmake build -g tests
    Invoke-Checked xmake run -g tests
}

Invoke-Checked xmake f -p windows -a x64 -m release --oldgen=n -o build -y
Invoke-Checked xmake build OnYourOwn
Invoke-Checked python scripts/package.py build/windows/x64/release/OnYourOwn.dll dist --data-root Data

Invoke-Checked python scripts/oldgen.py headers build/oldgen/ids
Invoke-Checked xmake f -p windows -a x64 -m release --oldgen=y -o build/oldgen -y
Invoke-Checked xmake build OnYourOwn
Invoke-Checked python scripts/oldgen.py check build/oldgen/windows/x64/release/OnYourOwn.dll
Invoke-Checked python scripts/package.py build/oldgen/windows/x64/release/OnYourOwn.dll dist --data-root Data --old-gen
