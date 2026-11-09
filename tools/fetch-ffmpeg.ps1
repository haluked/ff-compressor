# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fetch a pinned, static, GPLv3 ffmpeg build from gyan.dev.
# The app ships only bin\ffmpeg.exe and bin\ffprobe.exe.
#
# Why this build:
#   - gyan.dev "release full" is a static single-exe build (no extra DLLs).
#   - It includes libx264, libx265, NVENC, QSV, AMF and libopus.
#   - It is GPLv3, matching this project's license.
#
# The version and SHA256 below are pinned. Do not use "latest" URLs here.

$ErrorActionPreference = "Stop"

$Version  = "9.0.2"
$Url      = "https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-$Version-full_build.7z"
$Sha256   = "F0E46253C70DFE902BAC915DFB4224F0CF1B7C6EEAB9DA2CCC9A5581F9A71B13"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$CacheDir    = Join-Path $PSScriptRoot "cache"
$Archive     = Join-Path $CacheDir "ffmpeg-$Version-full_build.7z"
$ExtractDir  = Join-Path $CacheDir "extract-$Version"
$BinDir      = Join-Path $ProjectRoot "bin"

New-Item -ItemType Directory -Force $CacheDir, $BinDir | Out-Null

function Find-SevenZip {
    $candidates = @(
        "C:\Program Files\7-Zip\7z.exe",
        "C:\Program Files (x86)\7-Zip\7z.exe",
        (Get-Command 7z.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue)
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    throw "7-Zip not found. Install 7-Zip or extract the archive manually."
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToUpperInvariant()
}

Write-Host "ffmpeg build: $Version (static full, GPLv3)"

if (-not (Test-Path $Archive)) {
    Write-Host "Downloading $Url"
    & curl.exe -L --fail --retry 3 --retry-delay 2 -o $Archive $Url
    if ($LASTEXITCODE -ne 0) { throw "Download failed (curl exit $LASTEXITCODE)." }
} else {
    Write-Host "Using cached archive: $Archive"
}

$actual = Get-Sha256 $Archive
if ($actual -ne $Sha256) {
    throw "SHA256 mismatch!`nExpected: $Sha256`nActual:   $actual`nDelete the cached archive and retry."
}
Write-Host "SHA256 OK: $actual"

if (-not (Test-Path (Join-Path $ExtractDir "ffmpeg-$Version-full_build\bin\ffmpeg.exe"))) {
    $sevenZip = Find-SevenZip
    Write-Host "Extracting with $sevenZip"
    & $sevenZip x -y "-o$ExtractDir" $Archive | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "7-Zip extraction failed (exit $LASTEXITCODE)." }
}

$srcBin = Join-Path $ExtractDir "ffmpeg-$Version-full_build\bin"
$ffmpeg = Join-Path $srcBin "ffmpeg.exe"
$ffprobe = Join-Path $srcBin "ffprobe.exe"
if (-not (Test-Path $ffmpeg) -or -not (Test-Path $ffprobe)) {
    throw "Extracted archive does not contain ffmpeg.exe and ffprobe.exe."
}

Copy-Item $ffmpeg (Join-Path $BinDir "ffmpeg.exe") -Force
Copy-Item $ffprobe (Join-Path $BinDir "ffprobe.exe") -Force

# Record exactly what was fetched for the release's third-party notices.
$versionText = (& (Join-Path $BinDir "ffmpeg.exe") -version 2>&1) -join "`r`n"
$buildInfo = @"
FFmpeg build information
=======================
Source package : $Url
Version pin    : $Version
SHA256         : $Sha256
License        : GPLv3 (static build by gyan.dev)
FFmpeg source  : https://github.com/FFmpeg/FFmpeg
Build scripts  : https://www.gyan.dev/ffmpeg/builds/
Downloaded     : $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

$versionText
"@
Set-Content -Path (Join-Path $BinDir "FFMPEG_BUILD.txt") -Value $buildInfo -Encoding UTF8

Write-Host ""
Write-Host "Installed:"
Write-Host "  bin\ffmpeg.exe"
Write-Host "  bin\ffprobe.exe"
Write-Host "  bin\FFMPEG_BUILD.txt"
Write-Host ""
Write-Host "Standalone check:"
& (Join-Path $BinDir "ffmpeg.exe") -hide_banner -version | Select-Object -First 3
& (Join-Path $BinDir "ffprobe.exe") -hide_banner -version | Select-Object -First 3
