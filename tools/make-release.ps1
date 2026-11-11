# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a self-contained Windows release folder from a clean, committed tree.
# The release contains the app, the pinned FFmpeg binaries, licences, notices,
# checksums and a source archive for GPL compliance.

param(
    [string]$Version = "1.1.0"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Dist = Join-Path $ProjectRoot "dist"
$StageName = "ff-compressor-$Version-win64"
$Stage = Join-Path $Dist $StageName
$Exe = Join-Path $Dist "ff-compressor.exe"
$Ffmpeg = Join-Path $ProjectRoot "bin\ffmpeg.exe"
$Ffprobe = Join-Path $ProjectRoot "bin\ffprobe.exe"
$FfmpegBuild = Join-Path $ProjectRoot "bin\FFMPEG_BUILD.txt"

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
}

# Rebuild so the packaged EXE always comes from the same commit as source.zip.
Push-Location $ProjectRoot
try {
    $build = Start-Process -FilePath "cmd.exe" -ArgumentList "/c", "build.bat" `
        -NoNewWindow -Wait -PassThru
    if ($build.ExitCode -ne 0) {
        throw "build.bat failed (exit $($build.ExitCode))"
    }
} finally {
    Pop-Location
}

Require-File $Exe "App executable"
Require-File $Ffmpeg "ffmpeg.exe"
Require-File $Ffprobe "ffprobe.exe"
Require-File $FfmpegBuild "FFmpeg build record"
Require-File (Join-Path $ProjectRoot "LICENSE") "LICENSE"
Require-File (Join-Path $ProjectRoot "THIRD_PARTY_NOTICES.md") "Third-party notices"
Require-File (Join-Path $ProjectRoot "README.md") "README"

$appSize = (Get-Item $Exe).Length
if ($appSize -ge 1MB) { throw "App executable exceeds 1 MB: $appSize bytes" }
$fileVersion = (Get-Item $Exe).VersionInfo.FileVersion
if (-not $fileVersion.StartsWith($Version)) {
    throw "Executable version $fileVersion does not match release $Version"
}

$status = (& git -C $ProjectRoot status --porcelain) -join "`n"
if ($status.Trim().Length -ne 0) {
    throw "Working tree is not clean. Commit changes first so source.zip matches the release."
}
$commit = (& git -C $ProjectRoot rev-parse HEAD).Trim()

if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force $Stage | Out-Null
New-Item -ItemType Directory -Force (Join-Path $Stage "bin") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $Stage "licenses") | Out-Null

Copy-Item $Exe $Stage
Copy-Item $Ffmpeg (Join-Path $Stage "bin\ffmpeg.exe")
Copy-Item $Ffprobe (Join-Path $Stage "bin\ffprobe.exe")
Copy-Item $FfmpegBuild (Join-Path $Stage "bin\FFMPEG_BUILD.txt")
Copy-Item (Join-Path $ProjectRoot "LICENSE") $Stage
Copy-Item (Join-Path $ProjectRoot "THIRD_PARTY_NOTICES.md") $Stage
Copy-Item (Join-Path $ProjectRoot "README.md") $Stage
if (Test-Path (Join-Path $ProjectRoot "docs")) {
    Copy-Item (Join-Path $ProjectRoot "docs") $Stage -Recurse
}
Copy-Item (Join-Path $ProjectRoot "licenses\FFmpeg-BUILD.txt") (Join-Path $Stage "licenses")

$sourceZip = Join-Path $Stage "source.zip"
& git -C $ProjectRoot archive --format=zip "--output=$sourceZip" HEAD
if ($LASTEXITCODE -ne 0) { throw "git archive failed (exit $LASTEXITCODE)" }

$sourceText = @"
FF Compressor $Version - source and build information
=====================================================

Application source
------------------
The complete corresponding source code for this release is in source.zip.
Git commit: $commit
License: GPL-3.0-or-later (see LICENSE)

Build
-----
1. Install MinGW-w64 GCC with windres and 7-Zip.
2. Run tools\fetch-ffmpeg.ps1 to fetch the pinned FFmpeg build.
3. Run build.bat; the app is written to dist\ff-compressor.exe.
4. Run test.bat for the core test suite.

The shipped app is a single static Windows x64 executable. It reads the
portable FFmpeg executables from the bin folder next to it.

FFmpeg
------
Version: 9.0.2 static full build, GPLv3
Source: https://github.com/FFmpeg/FFmpeg/commit/946fcce07b
Source package: https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.2-full_build.7z
Archive SHA256: F0E46253C70DFE902BAC915DFB4224F0CF1B7C6EEAB9DA2CCC9A5581F9A71B13
Build details and the full configuration are in licenses\FFmpeg-BUILD.txt.

Written source offer: the complete corresponding source for the pinned FFmpeg
build is available from the project author on request for at least three years
from the release date, and from the FFmpeg source URLs above.

The application makes no network calls and has no telemetry. FFmpeg runs
locally on the file the user chooses.
"@
Set-Content -Path (Join-Path $Stage "SOURCE.txt") -Value $sourceText -Encoding UTF8

# Verify the source archive contains the application and licence.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($sourceZip)
try {
    $names = @($zip.Entries | ForEach-Object { $_.FullName })
    foreach ($need in @("src/encode.c", "src/ui.c", "LICENSE",
                        "tools/make-release.ps1", "tests/test_core.c")) {
        if ($names -notcontains $need) { throw "source.zip is missing $need" }
    }
} finally {
    $zip.Dispose()
}

$files = Get-ChildItem -Path $Stage -Recurse -File | Sort-Object FullName
$lines = New-Object System.Collections.Generic.List[string]
foreach ($file in $files) {
    if ($file.Name -eq "SHA256SUMS.txt") { continue }
    $rel = $file.FullName.Substring($Stage.Length + 1)
    $hash = (Get-FileHash -Algorithm SHA256 -Path $file.FullName).Hash.ToUpperInvariant()
    $lines.Add("$hash  $rel")
}
Set-Content -Path (Join-Path $Stage "SHA256SUMS.txt") -Value $lines -Encoding ASCII

$total = (Get-ChildItem -Path $Stage -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ""
Write-Host "Release: $Stage"
Write-Host ("App: {0} bytes ({1:N1} KB)" -f $appSize, ($appSize / 1KB))
Write-Host ("Files: {0}, total {1:N1} MB" -f $files.Count, ($total / 1MB))
Write-Host "Source archive: source.zip"
Write-Host "Checksums: SHA256SUMS.txt"
