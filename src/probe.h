/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PROBE_H
#define PROBE_H

#include <wchar.h>
#include "util.h"

typedef enum {
    PROBE_OK = 0,
    PROBE_ERR_NOT_FOUND,
    PROBE_ERR_START,
    PROBE_ERR_TIMEOUT,
    PROBE_ERR_FAILED,
    PROBE_ERR_BAD_JSON,
    PROBE_ERR_NO_VIDEO,
    PROBE_ERR_OUT_OF_MEMORY
} ProbeError;

#define PROBE_MAX_AUDIO_TRACKS 32
#define PROBE_MAX_SUBTITLE_TRACKS 64

typedef struct {
    wchar_t codec[64];
    wchar_t language[32];
    int channels;
    int sample_rate;
    long long bitrate;
} AudioTrackInfo;

typedef struct {
    wchar_t codec[64];
    wchar_t language[32];
} SubtitleTrackInfo;

typedef struct {
    wchar_t path[UTIL_PATH_MAX];
    wchar_t file_name[256];

    wchar_t container[128];
    wchar_t container_long[256];
    long long file_size;
    double duration;

    int coded_width;
    int coded_height;
    int display_width;
    int display_height;
    int rotation;

    double fps;
    double r_fps;
    int is_vfr;

    wchar_t video_codec[64];
    wchar_t video_codec_long[128];
    wchar_t profile[64];
    wchar_t pix_fmt[64];
    int bit_depth;

    int is_hdr;
    int is_hlg;
    int is_dolby_vision;
    wchar_t color_primaries[64];
    wchar_t color_transfer[64];
    wchar_t color_space[64];
    wchar_t color_range[32];

    int is_interlaced;
    wchar_t field_order[32];
    wchar_t sar[32];
    wchar_t dar[32];
    int is_anamorphic;

    long long video_bitrate;
    long long format_bitrate;
    double bits_per_pixel;
    long long nb_frames;

    int video_track_count;
    int audio_track_count;
    int subtitle_track_count;

    AudioTrackInfo audio_tracks[PROBE_MAX_AUDIO_TRACKS];
    int audio_tracks_stored;
    SubtitleTrackInfo subtitle_tracks[PROBE_MAX_SUBTITLE_TRACKS];
    int subtitle_tracks_stored;

    wchar_t audio_codec[64];
    wchar_t audio_codec_long[128];
    int audio_channels;
    int audio_sample_rate;
    long long audio_bitrate;
    wchar_t audio_language[32];
} SourceInfo;

/* Pure parser: no process, no I/O. Used by unit tests. */
ProbeError probe_parse_json(const char *json, SourceInfo *out);

/* Synchronous ffprobe run. Call from the engine worker, never the UI thread. */
ProbeError probe_run(const wchar_t *path, SourceInfo *out);

const wchar_t *probe_error_text(ProbeError err);

#endif /* PROBE_H */
