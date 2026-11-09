# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generate synthetic test clips for analysis, sampling, SSIM and preview.
# These are our own files (ffmpeg testsrc2 + sine), so no third-party license
# applies. They are intentionally gitignored; run this script to recreate them.

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Ffmpeg = Join-Path $ProjectRoot "bin\ffmpeg.exe"
$Out = Join-Path $ProjectRoot "tests\media"

if (-not (Test-Path $Ffmpeg)) {
    throw "bin\ffmpeg.exe not found. Run tools\fetch-ffmpeg.ps1 first."
}
New-Item -ItemType Directory -Force $Out | Out-Null

function Run-Ffmpeg([string[]]$FArgs) {
    Write-Host "> ffmpeg $($FArgs -join ' ')"
    & $Ffmpeg -hide_banner -loglevel error -y @FArgs
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed with exit $LASTEXITCODE" }
}

# 1. Normal 1080p H.264 / AAC source.
Run-Ffmpeg @(
    "-f", "lavfi", "-i", "testsrc2=size=1920x1080:rate=30:duration=20",
    "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=20",
    "-c:v", "libx264", "-preset", "veryfast", "-crf", "23",
    "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "128k",
    "-shortest", (Join-Path $Out "h264_1080p_20s.mp4")
)

# 2. Old-style low-bitrate 480p AVI.
Run-Ffmpeg @(
    "-f", "lavfi", "-i", "testsrc2=size=640x480:rate=25:duration=24",
    "-f", "lavfi", "-i", "sine=frequency=330:sample_rate=44100:duration=24",
    "-c:v", "mpeg4", "-q:v", "8", "-c:a", "libmp3lame", "-b:a", "64k",
    "-shortest", (Join-Path $Out "old_480p_24s.avi")
)

# 3. Interlaced 576i-style clip.
Run-Ffmpeg @(
    "-f", "lavfi", "-i", "testsrc2=size=720x576:rate=25:duration=20",
    "-vf", "tinterlace=interleave_top,fieldorder=tff",
    "-c:v", "libx264", "-preset", "veryfast", "-crf", "24",
    "-flags", "+ilme+ildct", "-x264-params", "tff=1",
    "-an", (Join-Path $Out "interlaced_576i_20s.mkv")
)

# 4. Variable frame rate clip: drop 3 of every 10 frames but keep timestamps.
Run-Ffmpeg @(
    "-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=30:duration=20",
    "-vf", "select='lt(mod(n,10),7)'", "-fps_mode", "vfr",
    "-c:v", "libx264", "-preset", "veryfast", "-crf", "26",
    "-an", (Join-Path $Out "vfr_720p_20s.mkv")
)

# 5. Subtitled clip (text subtitle track in MKV).
$srt = Join-Path $Out "sample.srt"
@"
1
00:00:01,000 --> 00:00:04,000
This is a test subtitle.

2
00:00:06,000 --> 00:00:09,000
FF Compressor subtitle passthrough test.
"@ | Set-Content -Path $srt -Encoding UTF8

Run-Ffmpeg @(
    "-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=30:duration=20",
    "-f", "lavfi", "-i", "sine=frequency=550:sample_rate=48000:duration=20",
    "-i", $srt,
    "-map", "0:v", "-map", "1:a", "-map", "2:s",
    "-c:v", "libx264", "-preset", "veryfast", "-crf", "26",
    "-c:a", "aac", "-b:a", "128k", "-c:s", "srt",
    "-t", "20", (Join-Path $Out "subtitles_720p_20s.mkv")
)

# 6. Portrait clip with a display-matrix rotation tag.
Run-Ffmpeg @(
    "-f", "lavfi", "-i", "testsrc2=size=720x1280:rate=30:duration=20",
    "-c:v", "libx264", "-preset", "veryfast", "-crf", "24",
    "-an", (Join-Path $Out "rotated_portrait_20s.mp4")
)
# Add the display-matrix rotation in a lossless remux.
Run-Ffmpeg @(
    "-display_rotation", "90",
    "-i", (Join-Path $Out "rotated_portrait_20s.mp4"),
    "-c", "copy", (Join-Path $Out "rotated_portrait_20s_tagged.mp4")
)

# 7. 10-bit HDR10-ish HEVC clip (colour tags + mastering metadata).
Run-Ffmpeg @(
    "-f", "lavfi", "-i", "testsrc2=size=1920x1080:rate=24:duration=10",
    "-c:v", "libx265", "-preset", "veryfast", "-crf", "24",
    "-pix_fmt", "yuv420p10le",
    "-vf", "setparams=color_primaries=bt2020:color_trc=smpte2084:colorspace=bt2020nc:range=tv",
    "-color_primaries", "bt2020", "-color_trc", "smpte2084",
    "-colorspace", "bt2020nc", "-color_range", "tv",
    "-x265-params", "log-level=error:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:hdr10=1:master-display=G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1):max-cll=1000,400",
    "-an", (Join-Path $Out "hdr10_10bit_1080p_10s.mkv")
)

# 9. Anamorphic (non-square pixel) clip.
Run-Ffmpeg @(
    "-f", "lavfi", "-i", "testsrc2=size=720x480:rate=30:duration=10",
    "-vf", "setsar=4/3",
    "-c:v", "libx264", "-preset", "veryfast", "-crf", "26",
    "-an", (Join-Path $Out "anamorphic_720x480_10s.mp4")
)

# 8. High-bitrate 4K phone-style clip (20 s, ~60 Mbps).
Run-Ffmpeg @(
    "-f", "lavfi", "-i", "testsrc2=size=3840x2160:rate=30:duration=20",
    "-f", "lavfi", "-i", "sine=frequency=660:sample_rate=48000:duration=20",
    "-c:v", "libx264", "-preset", "ultrafast", "-b:v", "60M",
    "-maxrate", "80M", "-bufsize", "160M", "-pix_fmt", "yuv420p",
    "-c:a", "aac", "-b:a", "128k", "-shortest",
    (Join-Path $Out "phone_4k_60mbps_20s.mp4")
)

Write-Host ""
Write-Host "Generated test media:"
Get-ChildItem $Out -File | Sort-Object Name | Select-Object Name, Length
