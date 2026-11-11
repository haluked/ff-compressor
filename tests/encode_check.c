/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "probe.h"
#include "hw.h"
#include "presets.h"
#include "estimate.h"
#include "encode.h"
#include "util.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static volatile int g_cancel = 0;

typedef struct {
    volatile int *cancel;
    DWORD delay_ms;
} CancelJob;

static DWORD WINAPI cancel_thread(LPVOID param)
{
    CancelJob *job = (CancelJob*)param;
    Sleep(job->delay_ms);
    *job->cancel = 1;
    return 0;
}

static void progress_cb(const EncodeProgress *p, void *user)
{
    int *last = (int*)user;
    int percent = (int)(p->fraction * 100.0);
    if (percent >= *last + 10 || p->verifying) {
        printf("PROGRESS percent=%d out=%.2f speed=%.2f verify=%d\n",
               percent, p->out_seconds, p->speed, p->verifying);
        *last = percent;
    }
}

static int parse_container(const wchar_t *s)
{
    if (_wcsicmp(s, L"mp4") == 0) return ENC_CONT_MP4;
    return ENC_CONT_MKV;
}

static int parse_vcodec(const wchar_t *s)
{
    if (_wcsicmp(s, L"h264") == 0) return ENC_VCODEC_H264;
    return ENC_VCODEC_HEVC;
}

static int parse_acodec(const wchar_t *s)
{
    if (_wcsicmp(s, L"copy") == 0) return ENC_ACODEC_COPY;
    if (_wcsicmp(s, L"opus") == 0) return ENC_ACODEC_OPUS;
    if (_wcsicmp(s, L"aac") == 0) return ENC_ACODEC_AAC;
    if (_wcsicmp(s, L"mp3") == 0) return ENC_ACODEC_MP3;
    if (_wcsicmp(s, L"flac") == 0) return ENC_ACODEC_FLAC;
    if (_wcsicmp(s, L"ac3") == 0) return ENC_ACODEC_AC3;
    if (_wcsicmp(s, L"eac3") == 0) return ENC_ACODEC_EAC3;
    if (_wcsicmp(s, L"vorbis") == 0) return ENC_ACODEC_VORBIS;
    return ENC_ACODEC_OPUS;
}

static int parse_level(const wchar_t *s)
{
    if (_wcsicmp(s, L"tiny") == 0) return QUALITY_TINY;
    if (_wcsicmp(s, L"small") == 0) return QUALITY_SMALL;
    if (_wcsicmp(s, L"high") == 0) return QUALITY_HIGH;
    return QUALITY_BALANCED;
}

int wmain(int argc, wchar_t **argv)
{
    SourceInfo src;
    SourceInfo outinfo;
    HwInfo hw;
    EncodeOptions opt;
    EncodeError err = ENCODE_ERR_NONE;
    EncodeResult result;
    wchar_t cmd[16384];
    wchar_t preview[UTIL_PATH_MAX];
    wchar_t out_path[UTIL_PATH_MAX];
    wchar_t detail[512];
    long long out_size = 0;
    int kbps = 36, runs = 1, i, encoder;
    int last_progress = -10;
    ProbeError pe;

    if (argc < 7) {
        wprintf(L"usage: encode_check <input> <mkv|mp4> <h265|h264> "
                L"<copy|opus|aac|mp3|flac|ac3|eac3|vorbis> <kbps> <level> "
                L"[runs] [cancel_ms] [track_1based] [downmix 0|1]\n");
        return 2;
    }

    kbps = _wtoi(argv[5]);
    if (kbps <= 0) kbps = 36;
    runs = (argc > 7) ? _wtoi(argv[7]) : 1;
    if (runs < 1) runs = 1;

    memset(&src, 0, sizeof(src));
    pe = probe_run(argv[1], &src);
    if (pe != PROBE_OK) {
        wprintf(L"PROBE_FAIL error=%d\n", (int)pe);
        return 1;
    }
    wprintf(L"SOURCE file=%ls duration=%.3f size=%lld audio=%d subs=%d hdr=%d\n",
            src.file_name, src.duration, src.file_size,
            src.audio_track_count, src.subtitle_track_count, src.is_hdr);

    hw_info_init(&hw);
    if (!hw_load(&hw) || hw.profile < 0) {
        hw.profile = HW_PROFILE_BALANCED;
        hw.recommended = HW_ENC_X265;
        hw.best8 = HW_ENC_X265;
        hw.enc_ok8[HW_ENC_X265] = 1;
        hw.speed8_median[HW_ENC_X265] = 1.0;
    }

    encode_options_defaults(&opt, &src);
    opt.container = parse_container(argv[2]);
    opt.video_codec = parse_vcodec(argv[3]);
    opt.audio_codec = parse_acodec(argv[4]);
    if (!encode_audio_available(opt.container, opt.audio_codec)) {
        wprintf(L"INVALID audio=%d container=%d\n", opt.audio_codec, opt.container);
        return 1;
    }
    if (kbps == 24) opt.bitrate = ENC_ABITRATE_VOICE;
    else if (kbps == 36) opt.bitrate = ENC_ABITRATE_MUSIC;
    else if (kbps == 64) opt.bitrate = ENC_ABITRATE_HIGH;
    else {
        opt.bitrate = ENC_ABITRATE_CUSTOM;
        opt.custom_kbps = kbps;
    }
    opt.downmix_stereo = 1;
    if (argc > 9) {
        int track = _wtoi(argv[9]);
        if (track > 0 && track <= PROBE_MAX_AUDIO_TRACKS) {
            opt.audio_mask = 1u << (track - 1);
        }
    }
    if (argc > 10) opt.downmix_stereo = _wtoi(argv[10]) ? 1 : 0;
    encoder = estimate_choose_encoder(&src, &hw);

    if (encode_preview_output_path(&src, opt.container, preview, UTIL_PATH_MAX)) {
        wprintf(L"PREVIEW %ls\n", preview);
    }

    for (i = 0; i < runs; i++) {
        if (encode_preview_output_path(&src, opt.container, preview, UTIL_PATH_MAX) &&
            encode_build_command(&src, encoder, parse_level(argv[6]),
                                 &opt, preview, cmd, 16384, &err)) {
            wprintf(L"CMD %ls\n", cmd);
        }

        last_progress = -10;
        if (argc > 8 && _wtoi(argv[8]) > 0) {
            CancelJob job;
            HANDLE th;
            g_cancel = 0;
            job.cancel = &g_cancel;
            job.delay_ms = (DWORD)_wtoi(argv[8]);
            th = CreateThread(NULL, 0, cancel_thread, &job, 0, NULL);
            if (th) CloseHandle(th);
        }

        result = encode_run(&src, encoder, parse_level(argv[6]),
                            &opt, progress_cb, &last_progress, &g_cancel,
                            out_path, UTIL_PATH_MAX, &out_size,
                            &err, detail, 512);

        wprintf(L"RESULT run=%d result=%d error=%d size=%lld path=%ls\n",
                i + 1, (int)result, (int)err, out_size, out_path);
        if (detail[0]) wprintf(L"DETAIL %ls\n", detail);
        if (result == ENCODE_OK) {
            memset(&outinfo, 0, sizeof(outinfo));
            if (probe_run(out_path, &outinfo) == PROBE_OK) {
                wprintf(L"OUTPUT duration=%.3f video=%ls %dx%d audio=%d subs=%d "
                        L"size=%lld\n",
                        outinfo.duration, outinfo.video_codec,
                        outinfo.coded_width, outinfo.coded_height,
                        outinfo.audio_track_count, outinfo.subtitle_track_count,
                        outinfo.file_size);
            } else {
                wprintf(L"OUTPUT_PROBE_FAIL\n");
            }
        }
    }
    return 0;
}
