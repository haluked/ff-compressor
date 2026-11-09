/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Developer harness for the first-launch hardware test.
   It uses the same hw_run_test() as the UI, prints the measured speeds and
   appends the same log entry. Run: hwtest_cli.exe 3 */
#include "hw.h"

#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>

static void on_progress(const HwProgress *p, void *user)
{
    (void)user;
    if (!p) return;
    if (p->event == HW_EVT_RUN) {
        wprintf(L"[%2d/%2d] %ls %d-bit %ls speed=%.3fx\n",
                p->run_done, p->run_total,
                hw_encoder_name(p->encoder), p->bit_depth,
                p->warmup ? L"warmup" : L"run", p->speed);
    } else if (p->event == HW_EVT_START) {
        wprintf(L"start: %d runs\n", p->run_total);
    } else if (p->event == HW_EVT_ERROR) {
        wprintf(L"error event: %d\n", (int)p->error);
    }
}

static void print_result(const HwInfo *info, HwTestResult r)
{
    int enc;
    wprintf(L"\nresult=%d raw=%ls final=%ls total=%.2fs\n",
            (int)r, hw_profile_name(info->profile_raw),
            hw_profile_name(info->profile), info->total_seconds);
    wprintf(L"cpu=%ls threads=%d physical=%d\n",
            info->cpu_name, info->logical_cores, info->physical_cores);
    wprintf(L"gpu=%ls driver=%ls\n", info->gpu_name, info->gpu_driver);
    for (enc = 0; enc < HW_ENC_COUNT; enc++) {
        if (!info->enc_listed[enc]) continue;
        wprintf(L"%ls: ok8=%d ok10=%d", hw_encoder_name(enc),
                info->enc_ok8[enc], info->enc_ok10[enc]);
        if (info->enc_ok8[enc]) {
            wprintf(L" speed8=%.3f (%.3f..%.3f)", info->speed8_median[enc],
                    info->speed8_min[enc], info->speed8_max[enc]);
        }
        if (info->enc_ok10[enc]) {
            wprintf(L" speed10=%.3f (%.3f..%.3f)", info->speed10_median[enc],
                    info->speed10_min[enc], info->speed10_max[enc]);
        }
        wprintf(L"\n");
    }
    wprintf(L"best8=%ls best10=%ls recommended=%ls hdr=%ls\n\n",
            hw_encoder_name(info->best8), hw_encoder_name(info->best10),
            hw_encoder_name(info->recommended), hw_encoder_name(info->hdr_encoder));
}

int wmain(int argc, wchar_t **argv)
{
    int runs = 1;
    int i;
    if (argc > 1) runs = _wtoi(argv[1]);
    if (runs < 1) runs = 1;
    if (runs > 10) runs = 10;

    for (i = 0; i < runs; i++) {
        HwInfo info;
        HwTestResult r;
        wprintf(L"=== hardware test run %d/%d ===\n", i + 1, runs);
        r = hw_run_test(&info, on_progress, NULL, NULL);
        print_result(&info, r);
        if (r == HW_TEST_OK) {
            hw_save(&info);
            hw_log_result(&info);
        }
    }
    return 0;
}
