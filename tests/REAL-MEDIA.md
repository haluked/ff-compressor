# Real test media (M3)

These files live in `tests/media/real/`, which is gitignored. They are used only
for local estimate verification and are not part of the shipped application.

| File | Source | Licence / credit | Use |
|---|---|---|---|
| `bbb_1080p_10s.mp4` | https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/1080/Big_Buck_Bunny_1080_10s_20MB.mp4 | Big Buck Bunny © Blender Foundation, CC BY 3.0 (peach.blender.org). Test-Videos states its files are free to download and use. | 1080p60 H.264 reference |
| `sintel_1080p_10s.mp4` | https://test-videos.co.uk/vids/sintel/mp4/h264/1080/Sintel_1080_10s_20MB.mp4 | Sintel © Blender Foundation, CC BY 3.0 (durian.blender.org). Test-Videos states its files are free to download and use. | 1080p24 H.264 reference |
| `jellyfish_1080p_10s.mp4` | https://test-videos.co.uk/vids/jellyfish/mp4/h264/1080/Jellyfish_1080_10s_20MB.mp4 | Jellyfish test clip; Test-Videos states its files are free to download and use. The original jell.yfish.us source is currently offline. | 1080p29.97 H.264 reference |
| `jellyfin_1080p_hevc10_5m.mp4` | https://repo.jellyfin.org/test-videos/SDR/HEVC%2010bit/Test%20Jellyfin%201080p%20HEVC%2010bit%205M.mp4 | Jellyfin test videos by Gnattu, CC BY-SA 4.0. See https://jellusion.baranov.org/credits/ | 1080p60 HEVC 10-bit SDR |
| `jellyfin_4k_hdr10_40m.mp4` | https://repo.jellyfin.org/test-videos/HDR/HDR10/HEVC/Test%20Jellyfin%204K%20HEVC%20HDR10%2040M.mp4 | Jellyfin test videos by Gnattu, CC BY-SA 4.0. See https://jellusion.baranov.org/credits/ | 4K60 HEVC 10-bit HDR10 |
| `jellyfin_1080p_hdr10_5m.mp4` | https://repo.jellyfin.org/test-videos/HDR/HDR10/HEVC/Test%20Jellyfin%201080p%20HEVC%20HDR10%205M.mp4 | Jellyfin test videos by Gnattu, CC BY-SA 4.0. See https://jellusion.baranov.org/credits/ | 1080p60 HEVC 10-bit HDR10 |

`bbb_1080p_30s.mp4`, `sintel_1080p_30s.mp4` and `jellyfish_1080p_30s.mp4`
are local 3× loops of the corresponding 10s files, created with
`-stream_loop 2 -c copy`; they inherit the same licences.

## Xiph.org

Xiph hosts lossless versions of the Blender open movies (CC BY 3.0) and the
Derf test collection. The lossless files are 0.7–46 GB each, and the Derf
collection has no explicit content licence statement, so no Xiph-only file was
redistributed. The same Blender footage is represented here via the small
CC BY encodes above.

The 10–30 s real clips are deliberately not committed. Re-download them into
`tests/media/real/` if you need to repeat the estimate verification.

## M3 verification result

5 clips × 4 quality levels = 20 checks, all with the recommended AMD AMF HEVC
encoder and the Balanced profile. Every check passed the targets:

- size error: **+2.1% to -11.0%** (target ±20%)
- time error: **-23.2% to +29.0%** (target ±30%)
- sample-test durations: 9.8–36.5 s per clip (Balanced: 3 clips × 4 s × 4 levels)

These clips have no audio track, so the audio part of the estimate (source
audio at copy bitrate) was not exercised on real audio. The phone clip will
cover that path.
