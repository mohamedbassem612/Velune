param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$Destination,
    [Parameter(Mandatory=$true)][string]$QtBin,
    [Parameter(Mandatory=$true)][string]$RuntimeManifest,
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
# This script consumes already acquired artifacts. It never downloads runtime code.
$manifestPath = (Resolve-Path $RuntimeManifest).Path
$manifestRoot = Split-Path $manifestPath
$manifest = Get-Content -Raw $manifestPath | ConvertFrom-Json
foreach ($entry in $manifest.files) {
    if (!$entry.file -or !$entry.target -or $entry.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or !$entry.license -or !$entry.source -or !$entry.notice) {
        throw 'Every supplied runtime needs a SHA-256, license, corresponding-source reference, and notice file.'
    }
    $source = Join-Path $manifestRoot $entry.file
    if ((Get-FileHash -Algorithm SHA256 $source).Hash -ne $entry.sha256) { throw "Runtime hash mismatch: $($entry.file)" }
    if (!(Test-Path (Join-Path $manifestRoot $entry.notice))) { throw "Missing notice: $($entry.notice)" }
    if ([IO.Path]::IsPathRooted($entry.target) -or $entry.target -match '(^|[\\/])\.\.([\\/]|$)') {
        throw 'Runtime destinations must remain inside the package.'
    }
}
cmake --install $BuildDirectory --config $Configuration --prefix $Destination
if ($LASTEXITCODE) { throw 'CMake installation failed' }
$binary = Join-Path $Destination 'bin/velune.exe'
$mode = if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }
& (Join-Path $QtBin 'windeployqt.exe') $mode --compiler-runtime --dir (Join-Path $Destination 'bin') $binary
if ($LASTEXITCODE) { throw 'windeployqt failed' }
New-Item -ItemType Directory -Force (Join-Path $Destination 'notices') | Out-Null
foreach ($entry in $manifest.files) {
    $target = Join-Path $Destination $entry.target
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item (Join-Path $manifestRoot $entry.file) $target -Force
    $noticeName = $entry.sha256.Substring(0, 12) + '-' + [IO.Path]::GetFileName($entry.notice)
    Copy-Item (Join-Path $manifestRoot $entry.notice) (Join-Path $Destination ('notices/' + $noticeName)) -Force
}
Copy-Item $manifestPath (Join-Path $Destination 'dependency-manifest.json')
# WebEngine is mandatory even for the minimal application.
foreach ($required in @('QtWebEngineProcess.exe', 'resources/qtwebengine_resources.pak', 'translations/qtwebengine_locales')) {
    if (!(Test-Path (Join-Path $Destination ('bin/' + $required)))) { throw "Qt WebEngine deployment incomplete: $required" }
}
$version = & (Join-Path $QtBin 'qmake.exe') -query QT_VERSION
Set-Content -Path (Join-Path $Destination 'qt-version.txt') -Value $version
Write-Output "Package staged at $Destination. Perform a clean-machine startup, login-window, TLS, and native playback smoke test."
