# Phone test footage

The `tests/media/phone/` folder is ready. Put a **copy** of short phone clips
there (10–30 s each, never the original). These scenarios cannot be exercised
with synthetic sources:

1. **Normal 4K30** — HEVC/H.265, 8- or 10-bit, typical phone bitrate.
2. **Portrait 4K30** — real display-matrix rotation metadata.
3. **Highest-quality mode** — HDR10/HLG or 4K60, depending on the phone.
4. **Optional:** 120/240 fps slow motion.
5. **Optional:** a clip with extra metadata/data tracks or multiple audio tracks.

Useful facts to report with the files: phone model, recording mode, codec,
resolution, fps, HDR on/off, and whether the file was copied unchanged.

`tests/media/phone/` is gitignored; the files are for local testing only.
