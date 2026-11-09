# FF Compressor

A small, native Windows video compressor built in C with a Game Boy-inspired
interface. It uses a portable FFmpeg build for analysis and encoding; the
application itself has no Python, .NET, Electron or Qt runtime.

**Status:** M1 (analysis) is implemented. Hardware tests, quality comparison,
sample estimates and compression arrive in M2–M6.

## What works in M1

- Choose a video with the file picker or drag and drop it onto the window.
- Read it with `ffprobe` in a background worker.
- Show codec, resolution, frame rate, VFR/CFR, bit depth, HDR/HLG/Dolby Vision,
  colour metadata, rotation, scan type, pixel aspect, average bitrate, audio
  codec/channels/sample rate, track counts and duration.
- Friendly errors for missing tools, unreadable files, timeouts and unsupported
  containers.
- Unicode paths, fixed 646×485 Game Boy layout, per-monitor DPI.

The second and third wizard steps are visible but intentionally inactive until
the next milestones.

## Requirements

- Windows 10 or 11, x64.
- MinGW-w64 GCC with `windres` (the project was built with MSYS2 MinGW).
- 7-Zip (only for extracting the pinned FFmpeg archive).
- Internet access once, to fetch FFmpeg.

## Build from source

```cmd
tools\fetch-ffmpeg.ps1
build.bat
test.bat
```

`tools\fetch-ffmpeg.ps1` downloads a pinned **static GPLv3** FFmpeg 9.0.2
full build from gyan.dev, verifies its SHA256, and copies only
`bin\ffmpeg.exe` and `bin\ffprobe.exe` into the project. `bin/` is not tracked
by git.

The executable is written to `dist\ff-compressor.exe`. The build fails if the
executable exceeds 1 MB.

## Test media

`tools\make-test-media.ps1` creates synthetic clips in `tests\media\`
(gitignored):

- normal 1080p H.264/AAC,
- old low-bitrate 480p AVI,
- interlaced 576i,
- variable frame rate 720p,
- subtitled MKV,
- portrait/rotated MP4,
- 10-bit HDR10-style HEVC,
- high-bitrate 4K phone-style MP4.

They are generated with FFmpeg's `testsrc2` and `sine` sources; no third-party
media is included.

## Design

The interface follows the same Game Boy DMG language as `ff-dlp-viewer`: four
green shades, 1 px dither, stepped pixel corners, hard 2 px frames/shadows,
`Consolas` with anti-aliasing off, owner-drawn controls, and a fixed
646×485 client area. All user-visible text is stored in `src/strings.c`.

## Privacy

No telemetry, no network calls from the application. FFmpeg runs locally on the
file you choose.

## License

GPL-3.0-or-later. See `LICENSE`.

Bundled FFmpeg is GPLv3. Build source, checksum and notices are in
`THIRD_PARTY_NOTICES.md` and `licenses/FFmpeg-BUILD.txt`.
