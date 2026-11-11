/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Simulates a broken GPU encoder by advertising NVENC on a machine where it
   cannot initialize, then checks that the sample engine falls back to CPU
   x265 and completes with used_fallback=1. */
#include "estimate.h"
#include "hw.h"
#include "probe.h"
#include "presets.h"
#include "strings.h"

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static void progress(const EstimateProgress *p, void *user)
{
    (void)user;
    if (p && p->clip_index == 1) {
        wprintf(L"  level=%ls start\n", presets_level_name(p->level));
    }
}

int wmain(int argc, wchar_t **argv)
{
    SourceInfo src;
    HwInfo hw;
    EstimateSet est;
    EstimateResult er;
    ProbeError pe;
    const wchar_t *path = (argc > 1) ? argv[1] : L"tests\\media\\old_480p_24s.avi";

    pe = probe_run(path, &src);
    if (pe != PROBE_OK) {
        wprintf(L"PROBE_FAIL error=%d\n", (int)pe);
        return 1;
    }

    hw_info_init(&hw);
    hw.profile = HW_PROFILE_BALANCED;
    hw.recommended = HW_ENC_NVENC;
    hw.best8 = HW_ENC_NVENC;
    hw.enc_ok8[HW_ENC_NVENC] = 1;
    hw.speed8_median[HW_ENC_NVENC] = 10.0;
    hw.hdr_encoder = -1;

    er = estimate_run(&src, &hw, HW_PROFILE_AUTO, 0, -1, &est, progress, NULL, NULL);
    wprintf(L"result=%d encoder=%ls fallback=%d complete=%d\n",
            (int)er, hw_encoder_name(est.encoder), est.used_fallback, est.complete);
    if (er == ESTIMATE_OK && est.used_fallback && est.encoder == HW_ENC_X265) {
        wprintf(L"FALLBACK_TEST PASS\n");
        return 0;
    }
    wprintf(L"FALLBACK_TEST FAIL\n");
    return 1;
}
