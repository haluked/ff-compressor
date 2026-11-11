/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Estimate verification harness: sample-based estimate vs a real full encode.
   Run from the project root:
     tests\build\estimate_check.exe tests\media\real\<clip>.mp4 ... */
#include "estimate.h"
#include "hw.h"
#include "presets.h"
#include "probe.h"
#include "strings.h"
#include "util.h"

#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>

static void quiet_progress(const EstimateProgress *p, void *user)
{
    (void)user;
    if (p) {
        wprintf(L"SAMPLE level=%ls clip=%d/%d speed=%.3fx\n",
                presets_level_name(p->level), p->clip_index, p->clip_total, p->speed);
    }
}

int wmain(int argc, wchar_t **argv)
{
    HwInfo hw;
    int i;
    wchar_t tempdir[UTIL_PATH_MAX];

    if (!hw_load(&hw)) {
        wprintf(L"No stored hardware profile. Run the computer test first.\n");
        return 1;
    }
    util_get_temp_dir(tempdir, UTIL_PATH_MAX);
    {
        wchar_t dir[UTIL_PATH_MAX];
        _snwprintf(dir, UTIL_PATH_MAX, L"%lsff-compressor\\verify-%lu",
                   tempdir, (unsigned long)GetCurrentProcessId());
        dir[UTIL_PATH_MAX - 1] = L'\0';
        SHCreateDirectoryExW(NULL, dir, NULL);
        wcsncpy(tempdir, dir, UTIL_PATH_MAX - 1);
        tempdir[UTIL_PATH_MAX - 1] = L'\0';
    }

    for (i = 1; i < argc; i++) {
        SourceInfo src;
        EstimateSet est;
        ProbeError pe;
        EstimateResult er;
        int li;

        pe = probe_run(argv[i], &src);
        if (pe != PROBE_OK) {
            wprintf(L"PROBE_FAIL file=%ls error=%d\n", argv[i], (int)pe);
            continue;
        }
        wprintf(L"CLIP file=%ls duration=%.3f size=%lld encoder=%ls profile=%ls\n",
                src.file_name, src.duration, src.file_size,
                hw_encoder_name(hw.recommended), hw_profile_name(hw.profile));
        wprintf(L"PREDICT seconds=%d fps=%.3f display=%dx%d speed8=%.2f\n",
                estimate_demo_seconds(&src, &hw, HW_PROFILE_AUTO),
                src.fps, src.display_width, src.display_height,
                hw.speed8_median[hw.recommended]);

        er = estimate_run(&src, &hw, HW_PROFILE_AUTO, 0, &est, quiet_progress, NULL, NULL);
        if (er != ESTIMATE_OK) {
            wprintf(L"ESTIMATE_FAIL file=%ls result=%d\n", src.file_name, (int)er);
            continue;
        }
        wprintf(L"DEMO file=%ls encoder=%ls seconds=%.3f fallback=%d\n",
                src.file_name, hw_encoder_name(est.encoder),
                est.total_seconds, est.used_fallback);

        for (li = 0; li < est.level_count; li++) {
            EstimateLevel *e = &est.levels[li];
            wchar_t out_path[UTIL_PATH_MAX];
            wchar_t cmd[UTIL_PATH_MAX * 2 + 1024];
            char *buf = NULL;
            size_t len = 0;
            DWORD code = 0;
            ULONGLONG t0, t1;
            long long actual_bytes = 0;
            double actual_sec;
            double size_delta, time_delta;
            int pass;

            if (!e->available) continue;
            _snwprintf(out_path, UTIL_PATH_MAX, L"%ls\\full_%d_%d.mkv",
                       tempdir, i, li);
            out_path[UTIL_PATH_MAX - 1] = L'\0';
            DeleteFileW(out_path);
            if (!presets_build_full_cmd(&src, est.encoder, e->level, out_path, 1,
                                        cmd, sizeof(cmd) / sizeof(wchar_t))) {
                wprintf(L"CMD_FAIL file=%ls level=%ls\n",
                        src.file_name, presets_level_name(e->level));
                continue;
            }
            t0 = GetTickCount64();
            if (util_run_capture(cmd, 900000, NULL, &buf, &len, &code) != UTIL_RUN_OK ||
                code != 0) {
                free(buf);
                DeleteFileW(out_path);
                wprintf(L"ENCODE_FAIL file=%ls level=%ls exit=%lu\n",
                        src.file_name, presets_level_name(e->level),
                        (unsigned long)code);
                continue;
            }
            t1 = GetTickCount64();
            free(buf);
            actual_sec = (double)(t1 - t0) / 1000.0;
            if (!util_get_file_size(out_path, &actual_bytes)) actual_bytes = 0;
            DeleteFileW(out_path);

            size_delta = (e->est_size_bytes > 0.0)
                ? ((double)actual_bytes - e->est_size_bytes) / e->est_size_bytes * 100.0
                : 0.0;
            time_delta = (e->est_seconds > 0.0)
                ? (actual_sec - e->est_seconds) / e->est_seconds * 100.0
                : 0.0;
            pass = (fabs(size_delta) <= 20.0 && fabs(time_delta) <= 30.0);

            wprintf(L"RESULT clip=%ls level=%ls encoder=%ls "
                    L"est_bytes=%.0f actual_bytes=%lld size_delta=%.1f%% "
                    L"est_sec=%.2f actual_sec=%.2f time_delta=%.1f%% "
                    L"ssim=%.4f %ls\n",
                    src.file_name, presets_level_name(e->level),
                    hw_encoder_name(est.encoder),
                    e->est_size_bytes, actual_bytes, size_delta,
                    e->est_seconds, actual_sec, time_delta,
                    e->ssim, pass ? L"PASS" : L"FAIL");
        }
        estimate_cleanup_preview_dir(&est);
    }
    RemoveDirectoryW(tempdir);
    return 0;
}
