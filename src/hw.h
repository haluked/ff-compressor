/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HW_H
#define HW_H

#include <wchar.h>
#include "util.h"

#define HW_ENC_COUNT 4
#define HW_TEST_VERSION 1
#define HW_PROFILE_AUTO 255

typedef enum {
    HW_ENC_NVENC = 0,
    HW_ENC_QSV = 1,
    HW_ENC_AMF = 2,
    HW_ENC_X265 = 3
} HwEncoderId;

typedef enum {
    HW_PROFILE_LIGHT = 0,
    HW_PROFILE_BALANCED = 1,
    HW_PROFILE_THOROUGH = 2
} HwProfile;

typedef enum {
    HW_TEST_OK = 0,
    HW_TEST_CANCELLED,
    HW_TEST_ERROR
} HwTestResult;

typedef enum {
    HW_ERR_NONE = 0,
    HW_ERR_FFMPEG,
    HW_ERR_NO_ENCODER,
    HW_ERR_CANCELLED
} HwError;

typedef enum {
    HW_EVT_START = 0,
    HW_EVT_RUN,
    HW_EVT_DONE,
    HW_EVT_ERROR
} HwEvent;

typedef struct {
    HwEvent event;
    HwError error;
    int run_done;
    int run_total;
    int encoder;
    int bit_depth;
    int warmup;
    double speed;
    double elapsed_seconds;
} HwProgress;

typedef void (*HwProgressCallback)(const HwProgress *progress, void *user);

typedef struct {
    int test_version;

    wchar_t cpu_name[128];
    wchar_t gpu_name[128];
    wchar_t gpu_driver[64];
    int logical_cores;
    int physical_cores;
    unsigned int gpu_vendor;
    unsigned long long gpu_memory_mb;

    int enc_listed[HW_ENC_COUNT];
    int enc_ok8[HW_ENC_COUNT];
    int enc_ok10[HW_ENC_COUNT];
    int enc_10bit_verified[HW_ENC_COUNT];
    wchar_t pix_fmt10[HW_ENC_COUNT][32];
    wchar_t profile10[HW_ENC_COUNT][64];

    double speed8_median[HW_ENC_COUNT];
    double speed8_min[HW_ENC_COUNT];
    double speed8_max[HW_ENC_COUNT];
    double speed8_runs[HW_ENC_COUNT][3];
    int speed8_run_count[HW_ENC_COUNT];

    double speed10_median[HW_ENC_COUNT];
    double speed10_min[HW_ENC_COUNT];
    double speed10_max[HW_ENC_COUNT];
    double speed10_runs[HW_ENC_COUNT][3];
    int speed10_run_count[HW_ENC_COUNT];

    double wall8_median_ms[HW_ENC_COUNT];
    double wall10_median_ms[HW_ENC_COUNT];

    int best8;
    int best10;
    int recommended;
    int hdr_encoder;

    int profile_raw;
    int profile;
    int skipped;
    double total_seconds;

    wchar_t ffmpeg_version[128];
    wchar_t tested_at[32];
} HwInfo;

void hw_info_init(HwInfo *info);
void hw_stamp_now(HwInfo *info);

int hw_detect_cpu(HwInfo *info);
int hw_detect_gpu(HwInfo *info);
int hw_list_encoders(HwInfo *info);
int hw_get_ffmpeg_version(wchar_t *out, size_t cap);

int hw_needs_retest(const HwInfo *stored);
HwTestResult hw_run_test(HwInfo *out, HwProgressCallback cb, void *user,
                         volatile int *cancel);

HwProfile hw_choose_profile_raw(const HwInfo *info);
HwProfile hw_choose_profile_hysteresis(const HwInfo *info, int stored_profile);
double hw_best_gpu_speed8(const HwInfo *info);
const wchar_t *hw_profile_name(int profile);
const wchar_t *hw_encoder_name(int encoder);
int hw_encoder_is_gpu(int encoder);
int hw_fallback_encoder(int failed_encoder);

int hw_load(HwInfo *info);
int hw_save(const HwInfo *info);
void hw_log_result(const HwInfo *info);
void hw_test_settings_text(wchar_t *out, size_t cap);

int hw_get_override(void);
void hw_set_override(int profile);

#endif /* HW_H */
