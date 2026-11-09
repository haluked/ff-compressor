/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "hw.h"
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static void p(const char *label, const wchar_t *w)
{
    char buf[512];
    WideCharToMultiByte(CP_UTF8, 0, w ? w : L"", -1, buf, 512, NULL, NULL);
    printf("%s%s\n", label, buf);
    fflush(stdout);
}

int wmain(void)
{
    HwInfo i;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("start\n"); fflush(stdout);
    hw_info_init(&i);
    printf("cpu\n"); fflush(stdout);
    hw_detect_cpu(&i);
    p("cpu_name=", i.cpu_name);
    printf("logical=%d physical=%d\n", i.logical_cores, i.physical_cores); fflush(stdout);
    printf("gpu\n"); fflush(stdout);
    hw_detect_gpu(&i);
    p("gpu_name=", i.gpu_name);
    p("gpu_driver=", i.gpu_driver);
    printf("vendor=0x%04X vram=%llu\n", i.gpu_vendor, i.gpu_memory_mb); fflush(stdout);
    printf("encoders\n"); fflush(stdout);
    printf("listed=%d\n", hw_list_encoders(&i)); fflush(stdout);
    p("ffmpeg=", i.ffmpeg_version);
    printf("nvenc=%d qsv=%d amf=%d x265=%d\n",
           i.enc_listed[0], i.enc_listed[1], i.enc_listed[2], i.enc_listed[3]);
    fflush(stdout);
    return 0;
}
