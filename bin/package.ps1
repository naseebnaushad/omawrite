# Builds omawrite.exe, bundles the Qt runtime with windeployqt, and zips it
# into build\Omawrite-windows.zip so it runs without Qt installed.
. "$PSScriptRoot\common.ps1"

$qmakeDir = Split-Path -Parent (Find-Qmake)
$windeploy = Join-Path $qmakeDir 'windeployqt6.exe'
if (-not (Test-Path $windeploy)) { $windeploy = Join-Path $qmakeDir 'windeployqt.exe' }
if (-not (Test-Path $windeploy)) { throw "windeployqt was not found next to qmake in $qmakeDir." }

& "$PSScriptRoot\build.ps1"
if ($LASTEXITCODE -ne 0) { throw 'build.ps1 failed' }

$buildDir = Join-Path $Root 'build'
$stage = Join-Path $buildDir 'Omawrite'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

$exe = Get-ChildItem -Path $buildDir -Recurse -Filter omawrite.exe |
    Where-Object { $_.FullName -notlike "$stage*" } | Select-Object -First 1
Copy-Item $exe.FullName (Join-Path $stage 'Omawrite.exe')

# -qmldir points the QML import scanner at the real source tree (the QML is
# bundled into a .qrc, so windeployqt can't see the imports otherwise).
Invoke-Checked $windeploy @('--release', '--qmldir', (Join-Path $Root 'src'), (Join-Path $stage 'Omawrite.exe'))

$zip = Join-Path $buildDir 'Omawrite-windows.zip'
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Built $zip"