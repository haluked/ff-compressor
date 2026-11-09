# Test media

`tests/media/` is gitignored. Recreate it with:

```cmd
tools\make-test-media.ps1
```

All files are generated locally from FFmpeg's `testsrc2` and `sine` sources.
They contain no third-party footage, so there is no external media licence to
carry. They are synthetic on purpose: deterministic, small enough to keep the
repository clean, and they cover the metadata shapes the app must handle.

| File | Purpose |
|---|---|
| `h264_1080p_20s.mp4` | Normal 8-bit SDR H.264/AAC source |
| `old_480p_24s.avi` | Old low-bitrate MPEG-4 Part 2 / MP3 source |
| `interlaced_576i_20s.mkv` | Interlaced scan detection |
| `vfr_720p_20s.mkv` | Variable frame rate detection (irregular packet timestamps) |
| `subtitles_720p_20s.mkv` | Text subtitle track present |
| `rotated_portrait_20s.mp4` | Portrait source before rotation metadata |
| `rotated_portrait_20s_tagged.mp4` | Display-matrix rotation (90°) |
| `hdr10_10bit_1080p_10s.mkv` | 10-bit HEVC, BT.2020/PQ, HDR10 metadata |
| `anamorphic_720x480_10s.mp4` | Non-square pixel aspect ratio |
| `phone_4k_60mbps_20s.mp4` | High-bitrate 4K phone-style source |

Real-world clips (Big Buck Bunny, Jellyfish, Xiph) will only be added if a
milestone needs footage that synthetic sources cannot represent. If added, each
file's source URL and licence will be recorded here and in the milestone report.

## Real media and estimates

`tests/media/real/` holds the gitignored Big Buck Bunny, Sintel, Jellyfish and
Jellyfin clips used for M3/M4 verification, including 1080p and 4K HDR10 HEVC.
Sources and licences are listed in `tests/REAL-MEDIA.md`. `tests/media/phone/`
is reserved for the phone clips described in `tests/PHONE-VIDEO.md`.

`estimate_check.c` runs the same sample-estimate engine as the UI, including
the SSIM measurement and preview-frame extraction, then a real full encode per
level. It prints estimated vs actual size/time and the median SSIM:

```cmd
gcc -std=c11 -O2 -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 ^
    -Isrc tests\estimate_check.c src\probe.c src\hw.c src\presets.c ^
    src\estimate.c src\util.c src\strings.c src\cjson.c ^
    -o tests\build\estimate_check.exe ^
    -lole32 -luuid -ldxguid -lshell32 -ladvapi32 -lgdi32 -luser32

tests\build\estimate_check.exe tests\media\real\bbb_1080p_30s.mp4 ...
```

The M4 verification run is saved in `tests/media/estimate_results_m4_final.txt`
(gitignored). It covers BBB, Sintel, Jellyfish, 1080p HEVC10 and 4K HDR10.
All 20 size checks passed; 19/20 time checks passed. The remaining case,
Sintel Balanced, measured -30.7% against a ±30% target and is treated as a
borderline miss rather than tuning the model to one clip.

## Developer diagnostics

- `hwtest_cli.c` — runs the first-launch hardware test N times with the same
  code as the UI and prints the measured speeds/profile.
- `estimate_check.c` — verifies sample estimates against real full encodes.
- `fallback_check.c` — advertises NVENC on this AMD machine, forces a failing
  sample and verifies the CPU x265 fallback path.
- `hw_detect.c` — prints CPU, GPU, driver and FFmpeg encoder detection.
- `dxgi_test.c` — minimal DXGI adapter enumeration check.

These are console-only developer tools, not part of the shipped application.
