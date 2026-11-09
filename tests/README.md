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

## Developer diagnostics

- `hwtest_cli.c` — runs the first-launch hardware test N times with the same
  code as the UI and prints the measured speeds/profile.
- `hw_detect.c` — prints CPU, GPU, driver and FFmpeg encoder detection.
- `dxgi_test.c` — minimal DXGI adapter enumeration check.

These are console-only developer tools, not part of the shipped application.
