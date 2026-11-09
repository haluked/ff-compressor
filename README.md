# FF Compressor

A small, native Windows video compressor built in C with a Game Boy-inspired
interface. It uses a portable FFmpeg build for analysis and encoding; the
application itself has no Python, .NET, Electron or Qt runtime.

**Status:** M3 (analysis, hardware test and sample-based quality estimates) is
implemented. SSIM/frame preview and compression arrive in M4–M6.

## What works in M1–M3

- Choose a video with the file picker or drag and drop it onto the window.
- Read it with `ffprobe` in a background worker.
- Show codec, resolution, frame rate, VFR/CFR, bit depth, HDR/HLG/Dolby Vision,
  colour metadata, rotation, scan type, pixel aspect, average bitrate, audio
  codec/channels/sample rate, track counts and duration.
- Friendly errors for missing tools, unreadable files, timeouts and unsupported
  containers.
- Unicode paths, fixed 646×485 Game Boy layout, per-monitor DPI.
- First-launch computer test: CPU/GPU detection, FFmpeg encoder listing, and
  real 3-second encodes for NVENC, QSV, AMF and CPU x265 (8-bit, plus 10-bit
  for GPU encoders).
- One warm-up run, then three measured runs per encoder; the median is used.
  A 20% hysteresis margin keeps a stored profile stable near a threshold.
- 10-bit GPU tests write a temporary MP4, then `ffprobe` confirms the actual
  `pix_fmt` is 10-bit and the profile is Main 10. The temporary file is deleted.
- The progress screen shows the duration measured on the previous successful
  run; before the first run it shows a conservative estimate.
- The log marks an encoder as `tested_on_hardware`, `tested_failed` or
  `untested_on_hardware`. On this AMD-only development PC, NVENC and QSV are
  `untested_on_hardware`; they are tested at runtime on the user's machine.
- The log prints a `near_boundary=` note when a measured value is within 15%
  of a Light/Thorough threshold.
- Profiles: Light / Balanced / Thorough. The result is stored in
  `HKCU\Software\ff-compressor`; the app re-tests automatically when the GPU,
  driver, CPU or FFmpeg version changes.
- `RE-TEST PC` re-runs the test. `TEST: AUTO/LIGHT/BALANCED/THOROUGH` overrides
  the test depth used by per-video tests.
- `Skip` selects the Light profile immediately.
- Measurement log: `%LOCALAPPDATA%\ff-compressor\hwtest.log`.
- Quality step: 3–4 real clips from different points, 3–4 quality levels, and
  estimates for the full video size/time. The chosen encoder comes from the
  stored hardware profile; the default is H.265.
- `TEST AGAIN` re-runs the sample test for the current video after a
  confirmation that shows the previous measured duration (or a conservative
  estimate on the first run). It is cancellable and caches results for the
  current file/profile.
- Estimate ranges are shown as guidance. Results are verified against real
  footage in `tests/REAL-MEDIA.md`. On the M3 set (5 clips × 4 levels = 20
  checks), size error was **+2.1% to -11.0%** and time error was **-23.2% to
  +29.0%**, all inside the ±20% / ±30% targets.
- The quality-demo label shows a conservative range before the first run and the
  previous measured duration on repeat runs.
- NVENC/QSV are tested at runtime on the user's machine. On the AMD-only
  development PC they are marked `untested_on_hardware`; a failing GPU encoder
  falls back to CPU x265 with a clear status message.

The second step is active; the third (full compression) is intentionally a
placeholder until M5.

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

## Hardware test harness

`tests\hwtest_cli.c` runs the same `hw_run_test()` as the UI without opening a
window. It prints the per-run speeds, medians, chosen profile and appends the
same log entry:

```cmd
gcc -std=c11 -O2 -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 ^
    -Isrc tests\hwtest_cli.c src\hw.c src\util.c src\strings.c ^
    -o tests\build\hwtest_cli.exe ^
    -lole32 -luuid -ldxguid -lshell32 -ladvapi32 -lgdi32 -luser32

tests\build\hwtest_cli.exe 3
```

The capability-test settings are fixed in `src/hw.c` and logged verbatim.

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
