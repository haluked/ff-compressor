/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ESTIMATE_H
#define ESTIMATE_H

#include <wchar.h>
#include "probe.h"
#include "hw.h"
#include "presets.h"

#define ESTIMATE_MAX_LEVELS 4

typedef struct {
    int level;
    int available;
    double est_size_bytes;
    double est_size_min;
    double est_size_max;
    double est_seconds;
    double est_seconds_min;
    double est_seconds_max;
    double sample_video_bps;
    double sample_speed;
    int clips;
    double clip_seconds;
} EstimateLevel;

typedef struct {
    EstimateLevel levels[ESTIMATE_MAX_LEVELS];
    int levels_used[ESTIMATE_MAX_LEVELS];
    int level_count;
    int clips;
    double clip_seconds;
    int encoder;
    int used_fallback;
    int profile;
    double total_seconds;
    int complete;
} EstimateSet;

typedef struct {
    int level;
    int level_index;
    int level_total;
    int clip_index;
    int clip_total;
    double speed;
    double elapsed_seconds;
} EstimateProgress;

typedef void (*EstimateProgressCallback)(const EstimateProgress *progress, void *user);

typedef enum {
    ESTIMATE_OK = 0,
    ESTIMATE_CANCELLED,
    ESTIMATE_ERROR
} EstimateResult;

int estimate_plan(const SourceInfo *src, int profile,
                  int *levels, int *level_count,
                  int *clips, double *clip_seconds);

int estimate_choose_encoder(const SourceInfo *src, const HwInfo *hw);
int estimate_demo_seconds(const SourceInfo *src, const HwInfo *hw, int profile_override);
int estimate_next_encoder(int current, int failed, int *fell_back);

EstimateResult estimate_run(const SourceInfo *src, const HwInfo *hw,
                            int profile_override, EstimateSet *out,
                            EstimateProgressCallback cb, void *user,
                            volatile int *cancel);

#endif /* ESTIMATE_H */
