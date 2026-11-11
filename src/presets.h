/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PRESETS_H
#define PRESETS_H

#include <wchar.h>
#include "probe.h"
#include "hw.h"

typedef enum {
    QUALITY_TINY = 0,
    QUALITY_SMALL = 1,
    QUALITY_BALANCED = 2,
    QUALITY_HIGH = 3,
    QUALITY_LEVEL_COUNT = 4
} QualityLevel;

#define PRESETS_VIDEO_HEVC 0
#define PRESETS_VIDEO_H264 1

int presets_video_args(const SourceInfo *src, int encoder, int level,
                       wchar_t *out, size_t cap);

int presets_video_args_ex(const SourceInfo *src, int encoder, int level,
                          int video_codec, wchar_t *out, size_t cap);

int presets_video_args_crf(const SourceInfo *src, int encoder, int video_codec,
                           int quality, wchar_t *out, size_t cap);

int presets_quality_value(int encoder, int level, int video_codec);

int presets_build_sample_cmd(const SourceInfo *src, int encoder, int level,
                             double start_sec, double duration_sec,
                             const wchar_t *out_path,
                             wchar_t *cmd, size_t cap);

int presets_build_sample_cmd_crf(const SourceInfo *src, int encoder,
                                 int video_codec, int quality,
                                 double start_sec, double duration_sec,
                                 const wchar_t *out_path,
                                 wchar_t *cmd, size_t cap);

int presets_build_full_cmd(const SourceInfo *src, int encoder, int level,
                           const wchar_t *out_path, int include_audio,
                           wchar_t *cmd, size_t cap);

int presets_available_levels(int profile, int *levels);
const wchar_t *presets_level_name(int level);
const wchar_t *presets_level_note(int level);

#endif /* PRESETS_H */
