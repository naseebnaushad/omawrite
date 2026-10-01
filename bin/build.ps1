# Builds omawrite.exe into build\ (and build\release\ when qmake uses a release subdir).
. "$PSScriptRoot\common.ps1"

$buildDir = Join-Path $Root 'build'
New-Item -ItemType Directory -Force $buildDir | Out-Null
Push-Location $buildDir
try {
    Invoke-Checked (Find-Qmake) @((Join-Path $Root 'omawrite.pro'))
    Invoke-Checked (Find-Make) @()
} finally {
    Pop-Location
}

$exe = Get-ChildItem -Path $buildDir -Recurse -Filter omawrite.exe | Select-Object -First 1
if (-not $exe) { throw 'Build finished but omawrite.exe was not found.' }
Write-Host "Built $($exe.FullName)"