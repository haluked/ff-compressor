/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Small developer inspector: ffprobe a file and print the parsed SourceInfo. */
#include "probe.h"

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        SourceInfo s;
        ProbeError e = probe_run(argv[i], &s);
        wprintf(L"=== %ls\n", argv[i]);
        if (e != PROBE_OK) {
            wprintf(L"error=%d (%ls)\n", (int)e, probe_error_text(e));
            continue;
        }
        wprintf(L"file=%ls\n", s.file_name);
        wprintf(L"container=%ls\n", s.container);
        wprintf(L"size=%lld duration=%.3f\n", s.file_size, s.duration);
        wprintf(L"coded=%dx%d display=%dx%d rotation=%d\n",
                s.coded_width, s.coded_height, s.display_width, s.display_height, s.rotation);
        wprintf(L"fps=%.3f r_fps=%.3f vfr=%d\n", s.fps, s.r_fps, s.is_vfr);
        wprintf(L"video=%ls profile=%ls pix=%ls depth=%d\n",
                s.video_codec, s.profile, s.pix_fmt, s.bit_depth);
        wprintf(L"hdr=%d hlg=%d dv=%d prim=%ls trc=%ls space=%ls range=%ls\n",
                s.is_hdr, s.is_hlg, s.is_dolby_vision, s.color_primaries,
                s.color_transfer, s.color_space, s.color_range);
        wprintf(L"interlaced=%d field=%ls sar=%ls anamorphic=%d\n",
                s.is_interlaced, s.field_order, s.sar, s.is_anamorphic);
        wprintf(L"bitrate=%lld audio_tracks=%d subs=%d bpp=%.4f\n",
                s.video_bitrate, s.audio_track_count, s.subtitle_track_count, s.bits_per_pixel);
        wprintf(L"audio=%ls %dch %dHz %lld lang=%ls\n",
                s.audio_codec, s.audio_channels, s.audio_sample_rate,
                s.audio_bitrate, s.audio_language);
    }
    return 0;
}
