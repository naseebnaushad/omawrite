# Builds and runs the unit tests headlessly.
. "$PSScriptRoot\common.ps1"

$buildDir = Join-Path $Root 'build-tests'
New-Item -ItemType Directory -Force $buildDir | Out-Null
Push-Location $buildDir
try {
    Invoke-Checked (Find-Qmake) @((Join-Path $Root 'tests\tests.pro'))
    Invoke-Checked (Find-Make) @()
    $env:QT_QPA_PLATFORM = 'offscreen'
    $exe = Get-ChildItem -Recurse -Filter tst_omawrite.exe | Select-Object -First 1
    if (-not $exe) { throw 'tst_omawrite.exe was not found.' }
    $env:QT_DEBUG_PLUGINS = '1'
    $log = Join-Path $buildDir 'test-output.txt'
    & $exe.FullName -o "$log,txt"
    $code = $LASTEXITCODE
    if (Test-Path $log) { Get-Content $log | Out-Host } else { Write-Host 'No test log was written.' }
    $LASTEXITCODE = $code
    if ($LASTEXITCODE -ne 0) { throw ('tst_omawrite failed with exit code 0x{0:X}' -f $LASTEXITCODE) }
} finally {
    Pop-Location
}