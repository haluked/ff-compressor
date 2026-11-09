# Third-party notices

## FFmpeg 9.0.2 (static full build)

- Project: https://ffmpeg.org/
- Source: https://github.com/FFmpeg/FFmpeg/commit/946fcce07b
- Build provider: https://www.gyan.dev/ffmpeg/builds/
- Downloaded package:
  https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.2-full_build.7z
- SHA256:
  `F0E46253C70DFE902BAC915DFB4224F0CF1B7C6EEAB9DA2CCC9A5581F9A71B13`
- License: GPLv3. The build configuration includes `--enable-gpl
  --enable-version3 --enable-static`.
- Bundled files: `bin/ffmpeg.exe`, `bin/ffprobe.exe`.
- Full build information: `licenses/FFmpeg-BUILD.txt`.

FFmpeg is a trademark of Fabrice Bellard, originator of the FFmpeg project.
This project is not affiliated with or endorsed by the FFmpeg project.

## cJSON

`src/cjson.c` and `src/cjson.h` are the single-file cJSON library, MIT licensed.
Copyright (c) 2009-2017 Dave Gamble and cJSON contributors. The original
copyright notice is kept in the source files.

## Game Boy drawing primitives

`src/gb_ui.c` and `src/gb_ui.h` are adapted from the author's `ff-dlp-viewer`
project and are distributed here under GPL-3.0-or-later.

## Windows system libraries

The application imports only Windows system DLLs (user32, gdi32, shell32,
ole32, uuid, advapi32). It does not bundle a C runtime DLL.
