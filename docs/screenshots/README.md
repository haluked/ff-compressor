# M2 screenshots

Captured from the M2 build (218 KB) on Windows 11 at 96 DPI.

- `m2-main.png` — main step-1 screen after a completed computer test:
  `Ready. Computer profile: BALANCED.`, `RE-TEST PC`, `TEST: AUTO`.
- `m2-hwtest.png` — first-launch/all-computer test with progress bar and
  `SKIP (USE LIGHT)`.
- `m2-retest.png` — `RE-TEST PC` completed from the main screen; the analyzed
  video (if any) is preserved.

## M3 — quality estimates

- `m3-quality-progress.png` — sample-test progress with a time range.
- `m3-quality-cards.png` — four quality cards with estimated size, time and
  savings, plus the H.265 compatibility note.
- `m3-card-selected.png` — a different level selected; the summary updates.
- `m3-test-again.png` — `TEST AGAIN` confirmation showing the previous measured
  duration.
- `m3-cancelled.png` — a cancelled sample test; no estimates are kept.

The test screens are transient; the cards are the normal quality-step state.

## M4 — SSIM and frame preview

- `m4-quality-cards.png` — quality cards now show the median SSIM of the sample
  encodes next to the size/time estimates.
- `m4-quality-preview.png` — two-view quality/preview layout: the original and
  compressed frames side by side, level chips with SSIM, and the selected
  level's SSIM label.
- `m4-quality-preview-tiny.png` — the same view after switching to the Tiny
  level; both the frame and the SSIM summary update.
- `m4-hdr-cards.png` — HDR10 source: SSIM cards before preview.
- `m4-hdr-preview.png` — HDR10 preview tone-mapped to SDR with the
  `HDR preview converted to SDR for display.` note.

## M5 — compression

- `m5-settings.png` — compression step defaults: MKV, H.265, Opus at Music
  36 kbps, with the selected quality, estimate, destination and audio summary.
- `m5-settings-mp4-h264.png` — MP4 + H.264 selected; audio switches to AAC and
  the compatibility note changes.
- `m5-settings-multichannel.png` — 5.1 source with the `DOWNMIX TO STEREO`
  proposal and note.
- `m5-custom-bitrate.png` — custom bitrate dialog with the Game Boy-styled
  input field.
- `m5-settings-custom-bitrate.png` — settings after applying a custom
  48 kbps value.
- `m5-progress.png` — compression in progress with percentage, elapsed time,
  speed, time estimate and `CANCEL`.
- `m5-done.png` — completed and verified output with the destination folder,
  file name, size and `OPEN FOLDER`.

## M6 — packaged release

- `m6-release-real-done.png` — the packaged v1.0.0 EXE compressing a real
  1080p 60 fps HEVC10 clip from the release folder using its own `bin\ffmpeg`
  and `bin\ffprobe`, with the verified result shown.

## M7 — advanced quality, archive and track editing

- `m7-quality-details.png` — the quality screen `DETAILS` view: each preset's
  x265 CRF, QSV, NVENC and AMF values, plus the `DEEP ANALYZE` action.
- `m7-advanced.png` — the `ADVANCED` panel with encoder and quality selection.
- `m7-archive.png` — `Archive (CPU x265 CRF 19)` selected; the CRF field shows 19.
- `m7-audio-tracks.png` — audio track menu with `No audio`, absolute ffprobe
  indices, language/title, codec, channel layout and bitrate.
- `m7-subtitle-tracks.png` — subtitle track menu with `No subtitles` and the
  absolute stream indices.

The tutorial images `02-quality.png` and `04-settings.png` were refreshed for
the same build. The quality cards now show the active encoder's CRF/CQ/gq/QP
value under every preset. They also show video-only source→target estimates;
the compress screen adds the selected audio and the total.
