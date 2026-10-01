# Shared helpers for the Windows build scripts. Dot-source this file.
$ErrorActionPreference = 'Stop'

$script:Root = Split-Path -Parent $PSScriptRoot

function Find-Qmake {
    foreach ($name in 'qmake6', 'qmake') {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    throw 'qmake6 or qmake was not found on PATH. Install Qt 6 (MSVC or MinGW kit) and add its bin directory to PATH.'
}

# Pick the make tool matching the compiler qmake was configured for.
function Find-Make {
    foreach ($name in 'jom', 'nmake', 'mingw32-make') {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    throw 'No make tool found. Run from an "x64 Native Tools" prompt (MSVC) or put MinGW''s bin directory on PATH.'
}

function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}