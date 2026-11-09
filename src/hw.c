/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "hw.h"
#include "strings.h"

#include <windows.h>
#include <shlobj.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <math.h>

#define HW_REG_KEY      L"Software\\ff-compressor"
#define HW_TEST_WIDTH   1920
#define HW_TEST_HEIGHT  1080
#define HW_TEST_FPS     30
#define HW_TEST_SECONDS 3
#define HW_WARMUPS      1
#define HW_RUNS         3
#define HW_RUN_TIMEOUT  30000u
#define HW_HYSTERESIS   0.20

/* ------------------------------------------------------------------ utils */
static int cmp_double(const void *a, const void *b)
{
    double x = *(const double*)a;
    double y = *(const double*)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

static double median_of(double *values, int n)
{
    if (n <= 0) return 0.0;
    qsort(values, (size_t)n, sizeof(double), cmp_double);
    if (n & 1) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

static void copy_utf8(const char *src, wchar_t *dst, size_t cap)
{
    int n;
    if (!dst || cap == 0) return;
    dst[0] = L'\0';
    if (!src || !*src) return;
    n = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)cap - 1);
    if (n <= 0) n = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)cap - 1);
    if (n > 0) dst[n] = L'\0';
}

static double parse_speed(const char *buf)
{
    const char *p = buf;
    double last = 0.0;
    if (!buf) return 0.0;
    while ((p = strstr(p, "speed=")) != NULL) {
        char *end = NULL;
        double v;
        p += 6;
        if (strncmp(p, "N/A", 3) == 0) { p += 3; continue; }
        v = strtod(p, &end);
        if (end != p) last = v;
        p = end ? end : p;
    }
    return last;
}

static void local_time_string(wchar_t *out, size_t cap)
{
    SYSTEMTIME st;
    if (!out || cap == 0) return;
    GetLocalTime(&st);
    _snwprintf(out, cap, L"%04d-%02d-%02d %02d:%02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    out[cap - 1] = L'\0';
}

/* --------------------------------------------------------------- registry */
static int reg_set_dword(HKEY key, const wchar_t *name, DWORD value)
{
    return RegSetValueExW(key, name, 0, REG_DWORD,
                          (const BYTE*)&value, sizeof(value)) == ERROR_SUCCESS;
}

static int reg_set_sz(HKEY key, const wchar_t *name, const wchar_t *value)
{
    return RegSetValueExW(key, name, 0, REG_SZ,
                          (const BYTE*)value,
                          (DWORD)((wcslen(value) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

static DWORD reg_get_dword(HKEY key, const wchar_t *name, DWORD fallback)
{
    DWORD value = fallback, type = 0, len = sizeof(value);
    if (RegQueryValueExW(key, name, NULL, &type, (LPBYTE)&value, &len) == ERROR_SUCCESS &&
        type == REG_DWORD && len == sizeof(value)) {
        return value;
    }
    return fallback;
}

static void reg_get_sz(HKEY key, const wchar_t *name, wchar_t *out, size_t cap)
{
    DWORD type = 0, len = (DWORD)(cap * sizeof(wchar_t));
    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (RegQueryValueExW(key, name, NULL, &type, (LPBYTE)out, &len) == ERROR_SUCCESS &&
        type == REG_SZ) {
        out[cap - 1] = L'\0';
    } else {
        out[0] = L'\0';
    }
}

void hw_info_init(HwInfo *info)
{
    if (!info) return;
    memset(info, 0, sizeof(*info));
    info->test_version = HW_TEST_VERSION;
    info->best8 = -1;
    info->best10 = -1;
    info->recommended = -1;
    info->hdr_encoder = -1;
    info->profile = -1;
    info->profile_raw = -1;
}

int hw_load(HwInfo *info)
{
    HKEY key;
    DWORD profile;
    if (!info) return 0;
    hw_info_init(info);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, HW_REG_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) return 0;

    info->test_version = (int)reg_get_dword(key, L"HwTestVersion", 0);
    profile = reg_get_dword(key, L"HwProfile", 0xFFFFFFFFu);
    info->profile = (profile <= 2) ? (int)profile : -1;
    info->profile_raw = (int)reg_get_dword(key, L"HwProfileRaw", 0xFFFFFFFFu);
    info->skipped = (int)reg_get_dword(key, L"HwSkipped", 0);
    info->logical_cores = (int)reg_get_dword(key, L"HwCores", 0);
    info->physical_cores = (int)reg_get_dword(key, L"HwPhysicalCores", 0);
    info->gpu_vendor = reg_get_dword(key, L"HwGpuVendor", 0);
    info->gpu_memory_mb = reg_get_dword(key, L"HwGpuMemoryMB", 0);
    info->total_seconds = (double)reg_get_dword(key, L"HwTotalSeconds", 0);
    info->best8 = (int)reg_get_dword(key, L"HwBest8", 0xFFFFFFFFu);
    info->best10 = (int)reg_get_dword(key, L"HwBest10", 0xFFFFFFFFu);
    info->recommended = (int)reg_get_dword(key, L"HwRecommended", 0xFFFFFFFFu);
    info->hdr_encoder = (int)reg_get_dword(key, L"HwHdrEncoder", 0xFFFFFFFFu);
    reg_get_sz(key, L"HwGpuName", info->gpu_name, 128);
    reg_get_sz(key, L"HwGpuDriver", info->gpu_driver, 64);
    reg_get_sz(key, L"HwCpuName", info->cpu_name, 128);
    reg_get_sz(key, L"HwFfmpeg", info->ffmpeg_version, 128);
    reg_get_sz(key, L"HwTestedAt", info->tested_at, 32);
    {
        int enc;
        for (enc = 0; enc < HW_ENC_COUNT; enc++) {
            wchar_t name[64];
            _snwprintf(name, 64, L"HwListed_%d", enc);
            info->enc_listed[enc] = (int)reg_get_dword(key, name, 0);
            _snwprintf(name, 64, L"HwOk8_%d", enc);
            info->enc_ok8[enc] = (int)reg_get_dword(key, name, 0);
            _snwprintf(name, 64, L"HwOk10_%d", enc);
            info->enc_ok10[enc] = (int)reg_get_dword(key, name, 0);
            _snwprintf(name, 64, L"HwVerified10_%d", enc);
            info->enc_10bit_verified[enc] = (int)reg_get_dword(key, name, 0);
            _snwprintf(name, 64, L"HwSpeed8_%d", enc);
            info->speed8_median[enc] = (double)reg_get_dword(key, name, 0) / 100.0;
            _snwprintf(name, 64, L"HwSpeed10_%d", enc);
            info->speed10_median[enc] = (double)reg_get_dword(key, name, 0) / 100.0;
            _snwprintf(name, 64, L"HwPix10_%d", enc);
            reg_get_sz(key, name, info->pix_fmt10[enc], 32);
            _snwprintf(name, 64, L"HwProfile10_%d", enc);
            reg_get_sz(key, name, info->profile10[enc], 64);
        }
    }
    RegCloseKey(key);

    if (info->test_version != HW_TEST_VERSION) return 0;
    if (info->profile < 0 || info->profile > 2) return 0;
    return 1;
}

void hw_stamp_now(HwInfo *info)
{
    if (info) local_time_string(info->tested_at, 32);
}

int hw_save(const HwInfo *info)
{
    HKEY key;
    if (!info) return 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, HW_REG_KEY, 0, NULL, 0,
                        KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS) {
        return 0;
    }
    reg_set_dword(key, L"HwTestVersion", (DWORD)HW_TEST_VERSION);
    reg_set_dword(key, L"HwProfile", (DWORD)info->profile);
    reg_set_dword(key, L"HwProfileRaw", (DWORD)info->profile_raw);
    reg_set_dword(key, L"HwSkipped", (DWORD)info->skipped);
    reg_set_dword(key, L"HwCores", (DWORD)info->logical_cores);
    reg_set_dword(key, L"HwPhysicalCores", (DWORD)info->physical_cores);
    reg_set_dword(key, L"HwGpuVendor", (DWORD)info->gpu_vendor);
    reg_set_dword(key, L"HwGpuMemoryMB", (DWORD)info->gpu_memory_mb);
    reg_set_dword(key, L"HwBest8", (DWORD)info->best8);
    reg_set_dword(key, L"HwBest10", (DWORD)info->best10);
    reg_set_dword(key, L"HwRecommended", (DWORD)info->recommended);
    reg_set_dword(key, L"HwHdrEncoder", (DWORD)info->hdr_encoder);
    reg_set_sz(key, L"HwGpuName", info->gpu_name);
    reg_set_sz(key, L"HwGpuDriver", info->gpu_driver);
    reg_set_sz(key, L"HwCpuName", info->cpu_name);
    reg_set_sz(key, L"HwFfmpeg", info->ffmpeg_version);
    reg_set_sz(key, L"HwTestedAt", info->tested_at);
    if (!info->skipped) {
        int enc;
        for (enc = 0; enc < HW_ENC_COUNT; enc++) {
            wchar_t name[64];
            _snwprintf(name, 64, L"HwListed_%d", enc);
            reg_set_dword(key, name, (DWORD)info->enc_listed[enc]);
            _snwprintf(name, 64, L"HwOk8_%d", enc);
            reg_set_dword(key, name, (DWORD)info->enc_ok8[enc]);
            _snwprintf(name, 64, L"HwOk10_%d", enc);
            reg_set_dword(key, name, (DWORD)info->enc_ok10[enc]);
            _snwprintf(name, 64, L"HwVerified10_%d", enc);
            reg_set_dword(key, name, (DWORD)info->enc_10bit_verified[enc]);
            _snwprintf(name, 64, L"HwSpeed8_%d", enc);
            reg_set_dword(key, name, (DWORD)(info->speed8_median[enc] * 100.0 + 0.5));
            _snwprintf(name, 64, L"HwSpeed10_%d", enc);
            reg_set_dword(key, name, (DWORD)(info->speed10_median[enc] * 100.0 + 0.5));
            _snwprintf(name, 64, L"HwPix10_%d", enc);
            reg_set_sz(key, name, info->pix_fmt10[enc]);
            _snwprintf(name, 64, L"HwProfile10_%d", enc);
            reg_set_sz(key, name, info->profile10[enc]);
        }
    }
    if (!info->skipped && info->total_seconds > 0.0) {
        reg_set_dword(key, L"HwTotalSeconds", (DWORD)(info->total_seconds + 0.5));
    }
    RegCloseKey(key);
    return 1;
}

int hw_get_override(void)
{
    HKEY key;
    DWORD v;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, HW_REG_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return HW_PROFILE_AUTO;
    }
    v = reg_get_dword(key, L"HwOverride", HW_PROFILE_AUTO);
    RegCloseKey(key);
    return (v <= 2) ? (int)v : HW_PROFILE_AUTO;
}

void hw_set_override(int profile)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, HW_REG_KEY, 0, NULL, 0,
                        KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS) {
        return;
    }
    reg_set_dword(key, L"HwOverride", (DWORD)profile);
    RegCloseKey(key);
}

/* -------------------------------------------------------------- CPU / GPU */
int hw_detect_cpu(HwInfo *info)
{
    HKEY key;
    SYSTEM_INFO si;
    wchar_t name[128] = L"";
    DWORD len = sizeof(name);
    DWORD type = 0;
    int physical = 0;

    if (!info) return 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"ProcessorNameString", NULL, &type,
                             (LPBYTE)name, &len) == ERROR_SUCCESS && type == REG_SZ) {
            name[127] = L'\0';
        }
        RegCloseKey(key);
    }
    if (!name[0]) wcscpy(name, L"Unknown CPU");
    wcsncpy(info->cpu_name, name, 127);
    info->cpu_name[127] = L'\0';
    util_trim(info->cpu_name);

    GetNativeSystemInfo(&si);
    info->logical_cores = (int)si.dwNumberOfProcessors;

    {
        DWORD bytes = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &bytes);
        if (bytes > 0) {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buf =
                (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(bytes);
            if (buf) {
                if (GetLogicalProcessorInformationEx(RelationProcessorCore, buf, &bytes)) {
                    DWORD off = 0;
                    while (off < bytes) {
                        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX e =
                            (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((BYTE*)buf + off);
                        if (e->Size == 0) break;
                        physical++;
                        off += e->Size;
                    }
                }
                free(buf);
            }
        }
    }
    info->physical_cores = physical > 0 ? physical : (info->logical_cores / 2);
    if (info->physical_cores < 1) info->physical_cores = 1;
    return 1;
}

static void detect_driver_version(const wchar_t *gpu_name, wchar_t *out, size_t cap)
{
    HKEY class_key;
    DWORD i;
    wchar_t first[64] = L"";

    if (!out || cap == 0) return;
    out[0] = L'\0';

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
                      0, KEY_READ, &class_key) != ERROR_SUCCESS) {
        return;
    }

    for (i = 0; i < 64; i++) {
        wchar_t sub[128];
        DWORD sub_len = 128;
        HKEY key;
        wchar_t desc[256] = L"";
        wchar_t ver[64] = L"";
        DWORD type, len;

        if (RegEnumKeyExW(class_key, i, sub, &sub_len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        if (RegOpenKeyExW(class_key, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;

        type = 0; len = sizeof(desc);
        RegQueryValueExW(key, L"DriverDesc", NULL, &type, (LPBYTE)desc, &len);
        desc[255] = L'\0';
        type = 0; len = sizeof(ver);
        RegQueryValueExW(key, L"DriverVersion", NULL, &type, (LPBYTE)ver, &len);
        ver[63] = L'\0';

        if (ver[0] && !first[0]) {
            wcsncpy(first, ver, 63);
            first[63] = L'\0';
        }
        if (ver[0] && gpu_name && desc[0] &&
            (_wcsicmp(desc, gpu_name) == 0 || wcsstr(desc, gpu_name) != NULL)) {
            wcsncpy(out, ver, cap - 1);
            out[cap - 1] = L'\0';
            RegCloseKey(key);
            RegCloseKey(class_key);
            return;
        }
        RegCloseKey(key);
    }

    if (first[0]) {
        wcsncpy(out, first, cap - 1);
        out[cap - 1] = L'\0';
    } else {
        wcsncpy(out, L"unknown", cap - 1);
        out[cap - 1] = L'\0';
    }
    RegCloseKey(class_key);
}

int hw_detect_gpu(HwInfo *info)
{
    HMODULE dxgi;
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *best = NULL;
    DXGI_ADAPTER_DESC1 best_desc;
    int found = 0;

    if (!info) return 0;
    memset(&best_desc, 0, sizeof(best_desc));

    dxgi = LoadLibraryW(L"dxgi.dll");
    if (dxgi) {
        typedef HRESULT (WINAPI *CreateFactoryFn)(REFIID, void**);
        CreateFactoryFn create = (CreateFactoryFn)(void*)GetProcAddress(dxgi, "CreateDXGIFactory1");
        if (create && SUCCEEDED(create(&IID_IDXGIFactory1, (void**)&factory)) && factory) {
            UINT i;
            for (i = 0; ; i++) {
                IDXGIAdapter1 *adapter = NULL;
                DXGI_ADAPTER_DESC1 desc;
                if (factory->lpVtbl->EnumAdapters1(factory, i, &adapter) != S_OK) break;
                if (adapter->lpVtbl->GetDesc1(adapter, &desc) == S_OK &&
                    !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                    if (!found || desc.DedicatedVideoMemory > best_desc.DedicatedVideoMemory) {
                        if (best) best->lpVtbl->Release(best);
                        best = adapter;
                        best_desc = desc;
                        found = 1;
                        continue;
                    }
                }
                adapter->lpVtbl->Release(adapter);
            }
            factory->lpVtbl->Release(factory);
        }
    }

    if (found) {
        wcsncpy(info->gpu_name, best_desc.Description, 127);
        info->gpu_name[127] = L'\0';
        info->gpu_vendor = best_desc.VendorId;
        info->gpu_memory_mb = (unsigned long long)(best_desc.DedicatedVideoMemory / (1024ull * 1024ull));
        best->lpVtbl->Release(best);
    } else {
        DISPLAY_DEVICEW dd;
        memset(&dd, 0, sizeof(dd));
        dd.cb = sizeof(dd);
        if (EnumDisplayDevicesW(NULL, 0, &dd, 0)) {
            wcsncpy(info->gpu_name, dd.DeviceString, 127);
            info->gpu_name[127] = L'\0';
            found = 1;
        } else {
            wcsncpy(info->gpu_name, L"Unknown GPU", 127);
            info->gpu_name[127] = L'\0';
        }
    }

    if (dxgi) FreeLibrary(dxgi);
    detect_driver_version(info->gpu_name, info->gpu_driver, 64);
    return found;
}

/* --------------------------------------------------------------- ffmpeg */
int hw_get_ffmpeg_version(wchar_t *out, size_t cap)
{
    wchar_t ffmpeg[UTIL_PATH_MAX];
    wchar_t q[UTIL_PATH_MAX + 4];
    wchar_t cmd[UTIL_PATH_MAX + 64];
    char *buf = NULL;
    size_t len = 0;
    DWORD code = 0;

    if (!out || cap == 0) return 0;
    out[0] = L'\0';
    if (!util_find_tool(L"ffmpeg", ffmpeg, UTIL_PATH_MAX)) return 0;
    if (!util_quote_arg(q, sizeof(q) / sizeof(wchar_t), ffmpeg)) return 0;
    _snwprintf(cmd, sizeof(cmd) / sizeof(wchar_t), L"%ls -hide_banner -version", q);
    cmd[(sizeof(cmd) / sizeof(wchar_t)) - 1] = L'\0';

    if (util_run_capture(cmd, 15000, NULL, &buf, &len, &code) != UTIL_RUN_OK || code != 0) {
        free(buf);
        return 0;
    }
    {
        char *nl = buf ? strchr(buf, '\n') : NULL;
        if (nl) *nl = '\0';
        copy_utf8(buf, out, cap);
    }
    free(buf);
    return out[0] ? 1 : 0;
}

int hw_list_encoders(HwInfo *info)
{
    wchar_t ffmpeg[UTIL_PATH_MAX];
    wchar_t q[UTIL_PATH_MAX + 4];
    wchar_t cmd[UTIL_PATH_MAX + 64];
    char *buf = NULL;
    size_t len = 0;
    DWORD code = 0;

    if (!info) return 0;
    if (!util_find_tool(L"ffmpeg", ffmpeg, UTIL_PATH_MAX)) return 0;
    if (!util_quote_arg(q, sizeof(q) / sizeof(wchar_t), ffmpeg)) return 0;
    _snwprintf(cmd, sizeof(cmd) / sizeof(wchar_t), L"%ls -hide_banner -encoders", q);
    cmd[(sizeof(cmd) / sizeof(wchar_t)) - 1] = L'\0';

    if (util_run_capture(cmd, 15000, NULL, &buf, &len, &code) != UTIL_RUN_OK || code != 0) {
        free(buf);
        return 0;
    }
    info->enc_listed[HW_ENC_NVENC] = strstr(buf, "hevc_nvenc") != NULL;
    info->enc_listed[HW_ENC_QSV]   = strstr(buf, "hevc_qsv") != NULL;
    info->enc_listed[HW_ENC_AMF]   = strstr(buf, "hevc_amf") != NULL;
    info->enc_listed[HW_ENC_X265]  = strstr(buf, "libx265") != NULL;
    free(buf);
    hw_get_ffmpeg_version(info->ffmpeg_version, 128);
    return 1;
}

int hw_needs_retest(const HwInfo *stored)
{
    HwInfo current;
    wchar_t version[128];

    if (!stored) return 1;
    if (stored->test_version != HW_TEST_VERSION) return 1;
    if (stored->profile < 0 || stored->profile > 2) return 1;

    hw_info_init(&current);
    hw_detect_cpu(&current);
    hw_detect_gpu(&current);

    if (_wcsicmp(stored->cpu_name, current.cpu_name) != 0) return 1;
    if (stored->logical_cores != current.logical_cores) return 1;
    if (_wcsicmp(stored->gpu_name, current.gpu_name) != 0) return 1;
    if (_wcsicmp(stored->gpu_driver, current.gpu_driver) != 0) return 1;

    if (!hw_get_ffmpeg_version(version, 128)) return 1;
    if (_wcsicmp(stored->ffmpeg_version, version) != 0) return 1;
    return 0;
}

/* ---------------------------------------------------------- profile logic */
int hw_encoder_is_gpu(int encoder)
{
    return encoder >= HW_ENC_NVENC && encoder <= HW_ENC_AMF;
}

int hw_fallback_encoder(int failed_encoder)
{
    if (failed_encoder == HW_ENC_X265) return -1;
    return HW_ENC_X265;
}

double hw_best_gpu_speed8(const HwInfo *info)
{
    double best = 0.0;
    int i;
    if (!info) return 0.0;
    for (i = HW_ENC_NVENC; i <= HW_ENC_AMF; i++) {
        if (info->enc_ok8[i] && info->speed8_median[i] > best) {
            best = info->speed8_median[i];
        }
    }
    return best;
}

HwProfile hw_choose_profile_raw(const HwInfo *info)
{
    double gpu = hw_best_gpu_speed8(info);
    double x265 = info ? info->speed8_median[HW_ENC_X265] : 0.0;
    int threads = info ? info->logical_cores : 0;

    if ((gpu >= 25.0 && threads >= 8) || (x265 >= 3.0 && threads >= 12)) {
        return HW_PROFILE_THOROUGH;
    }
    if ((gpu < 3.0 && x265 < 1.0) || (threads <= 4 && x265 < 1.5)) {
        return HW_PROFILE_LIGHT;
    }
    return HW_PROFILE_BALANCED;
}

HwProfile hw_choose_profile_hysteresis(const HwInfo *info, int stored_profile)
{
    HwProfile raw = hw_choose_profile_raw(info);
    double gpu = hw_best_gpu_speed8(info);
    double x265 = info ? info->speed8_median[HW_ENC_X265] : 0.0;
    int threads = info ? info->logical_cores : 0;
    double up = 1.0 + HW_HYSTERESIS;
    double down = 1.0 / up;
    int light_speed, light_cores;

    if (stored_profile < 0 || stored_profile > 2 || stored_profile == (int)raw) return raw;

    light_speed = (gpu < 3.0 * down) && (x265 < 1.0 * down);
    light_cores = (threads <= 4) && (x265 < 1.5 * down);

    if (stored_profile == HW_PROFILE_LIGHT && raw == HW_PROFILE_BALANCED) {
        if (gpu >= 3.0 * up || x265 >= 1.0 * up || (threads > 4 && x265 >= 1.5 * up)) {
            return HW_PROFILE_BALANCED;
        }
        return HW_PROFILE_LIGHT;
    }
    if (stored_profile == HW_PROFILE_BALANCED && raw == HW_PROFILE_THOROUGH) {
        if ((gpu >= 25.0 * up && threads >= 8) || (x265 >= 3.0 * up && threads >= 12)) {
            return HW_PROFILE_THOROUGH;
        }
        return HW_PROFILE_BALANCED;
    }
    if (stored_profile == HW_PROFILE_BALANCED && raw == HW_PROFILE_LIGHT) {
        if (light_speed || light_cores) return HW_PROFILE_LIGHT;
        return HW_PROFILE_BALANCED;
    }
    if (stored_profile == HW_PROFILE_THOROUGH && raw == HW_PROFILE_BALANCED) {
        if (gpu < 25.0 * down && x265 < 3.0 * down) return HW_PROFILE_BALANCED;
        return HW_PROFILE_THOROUGH;
    }
    if (stored_profile == HW_PROFILE_LIGHT && raw == HW_PROFILE_THOROUGH) {
        if ((gpu >= 25.0 * up && threads >= 8) || (x265 >= 3.0 * up && threads >= 12)) {
            return HW_PROFILE_THOROUGH;
        }
        if (gpu >= 3.0 * up || x265 >= 1.0 * up || (threads > 4 && x265 >= 1.5 * up)) {
            return HW_PROFILE_BALANCED;
        }
        return HW_PROFILE_LIGHT;
    }
    if (stored_profile == HW_PROFILE_THOROUGH && raw == HW_PROFILE_LIGHT) {
        if (light_speed || light_cores) return HW_PROFILE_LIGHT;
        if (gpu < 25.0 * down && x265 < 3.0 * down) return HW_PROFILE_BALANCED;
        return HW_PROFILE_THOROUGH;
    }
    return raw;
}

const wchar_t *hw_profile_name(int profile)
{
    switch (profile) {
        case HW_PROFILE_LIGHT:    return text(STR_PROFILE_LIGHT);
        case HW_PROFILE_BALANCED: return text(STR_PROFILE_BALANCED);
        case HW_PROFILE_THOROUGH: return text(STR_PROFILE_THOROUGH);
        case HW_PROFILE_AUTO:
        default:                  return text(STR_PROFILE_AUTO);
    }
}

const wchar_t *hw_encoder_name(int encoder)
{
    switch (encoder) {
        case HW_ENC_NVENC: return text(STR_ENC_NVENC);
        case HW_ENC_QSV:   return text(STR_ENC_QSV);
        case HW_ENC_AMF:   return text(STR_ENC_AMF);
        case HW_ENC_X265:  return text(STR_ENC_X265);
        default:           return text(STR_ENC_NONE);
    }
}

/* ------------------------------------------------------------ test driver */
static int contains_ci(const char *haystack, const char *needle)
{
    char h[256];
    char n[64];
    size_t i;
    if (!haystack || !needle) return 0;
    for (i = 0; i + 1 < sizeof(h) && haystack[i]; i++) h[i] = (char)tolower((unsigned char)haystack[i]);
    h[i] = '\0';
    for (i = 0; i + 1 < sizeof(n) && needle[i]; i++) n[i] = (char)tolower((unsigned char)needle[i]);
    n[i] = '\0';
    return strstr(h, n) != NULL;
}

static void make_10bit_temp(int enc, wchar_t *out, size_t cap)
{
    wchar_t base[UTIL_PATH_MAX];
    wchar_t dir[UTIL_PATH_MAX];
    if (!out || cap == 0) return;
    out[0] = L'\0';
    util_get_temp_dir(base, UTIL_PATH_MAX);
    _snwprintf(dir, UTIL_PATH_MAX, L"%lsff-compressor\\hwtest", base);
    dir[UTIL_PATH_MAX - 1] = L'\0';
    SHCreateDirectoryExW(NULL, dir, NULL);
    _snwprintf(out, cap, L"%ls\\hw10_%d.mp4", dir, enc);
    out[cap - 1] = L'\0';
    DeleteFileW(out);
}

static int verify_10bit_file(const wchar_t *path, wchar_t *pix_out, size_t pix_cap,
                             wchar_t *profile_out, size_t profile_cap)
{
    wchar_t ffprobe[UTIL_PATH_MAX];
    wchar_t q_tool[UTIL_PATH_MAX + 4];
    wchar_t q_path[UTIL_PATH_MAX + 4];
    wchar_t cmd[UTIL_PATH_MAX * 2 + 128];
    char *buf = NULL;
    size_t len = 0;
    DWORD code = 0;
    char pix[64] = "";
    char profile[64] = "";
    char *line;

    if (pix_out && pix_cap) pix_out[0] = L'\0';
    if (profile_out && profile_cap) profile_out[0] = L'\0';
    if (!util_find_tool(L"ffprobe", ffprobe, UTIL_PATH_MAX)) return 0;
    if (!util_quote_arg(q_tool, sizeof(q_tool) / sizeof(wchar_t), ffprobe)) return 0;
    if (!util_quote_arg(q_path, sizeof(q_path) / sizeof(wchar_t), path)) return 0;

    _snwprintf(cmd, sizeof(cmd) / sizeof(wchar_t),
        L"%ls -v error -select_streams v:0 -show_entries stream=pix_fmt,profile "
        L"-of default=nw=1 %ls", q_tool, q_path);
    cmd[(sizeof(cmd) / sizeof(wchar_t)) - 1] = L'\0';

    if (util_run_capture(cmd, 15000, NULL, &buf, &len, &code) != UTIL_RUN_OK ||
        code != 0 || len == 0) {
        free(buf);
        return 0;
    }

    line = strtok(buf, "\r\n");
    while (line) {
        if (strncmp(line, "pix_fmt=", 8) == 0) {
            strncpy(pix, line + 8, sizeof(pix) - 1);
            pix[sizeof(pix) - 1] = '\0';
        } else if (strncmp(line, "profile=", 8) == 0) {
            strncpy(profile, line + 8, sizeof(profile) - 1);
            profile[sizeof(profile) - 1] = '\0';
        }
        line = strtok(NULL, "\r\n");
    }
    free(buf);

    if (pix_out) copy_utf8(pix, pix_out, pix_cap);
    if (profile_out) copy_utf8(profile, profile_out, profile_cap);

    if (!(strstr(pix, "10") || strstr(pix, "p010") || strstr(pix, "p016"))) return 0;
    if (!(contains_ci(profile, "main 10") || contains_ci(profile, "main10"))) return 0;
    return 1;
}

static int build_test_cmd(const wchar_t *ffmpeg, int enc, int bit_depth,
                          const wchar_t *out_path, wchar_t *cmd, size_t cap)
{
    wchar_t q[UTIL_PATH_MAX + 4];
    wchar_t q_out[UTIL_PATH_MAX + 4];
    const wchar_t *args = NULL;

    if (!util_quote_arg(q, sizeof(q) / sizeof(wchar_t), ffmpeg)) return 0;
    if (bit_depth == 10) {
        if (!out_path || !util_quote_arg(q_out, sizeof(q_out) / sizeof(wchar_t), out_path)) return 0;
    }

    if (enc == HW_ENC_X265) {
        if (bit_depth != 8) return 0;
        args = L"-c:v libx265 -preset medium -crf 26 -x265-params log-level=error -pix_fmt yuv420p";
    } else if (enc == HW_ENC_NVENC) {
        args = (bit_depth == 10)
            ? L"-c:v hevc_nvenc -preset p5 -tune hq -rc vbr -cq 28 -b:v 0 -pix_fmt p010le -profile:v main10"
            : L"-c:v hevc_nvenc -preset p5 -tune hq -rc vbr -cq 28 -b:v 0 -pix_fmt yuv420p";
    } else if (enc == HW_ENC_QSV) {
        args = (bit_depth == 10)
            ? L"-c:v hevc_qsv -preset medium -global_quality 26 -pix_fmt p010le -profile:v main10"
            : L"-c:v hevc_qsv -preset medium -global_quality 26 -pix_fmt nv12";
    } else if (enc == HW_ENC_AMF) {
        args = (bit_depth == 10)
            ? L"-c:v hevc_amf -quality balanced -rc cqp -qp_i 26 -qp_p 28 -qp_b 28 -pix_fmt p010le -profile:v main10"
            : L"-c:v hevc_amf -quality balanced -rc cqp -qp_i 26 -qp_p 28 -qp_b 28 -pix_fmt yuv420p";
    } else {
        return 0;
    }

    if (bit_depth == 10) {
        _snwprintf(cmd, cap,
            L"%ls -hide_banner -loglevel error -nostats -progress pipe:1 "
            L"-f lavfi -i \"testsrc2=size=%dx%d:rate=%d:duration=%d\" "
            L"%ls -an -sn -dn -t %d -f mp4 -y %ls",
            q, HW_TEST_WIDTH, HW_TEST_HEIGHT, HW_TEST_FPS, HW_TEST_SECONDS,
            args, HW_TEST_SECONDS, q_out);
    } else {
        _snwprintf(cmd, cap,
            L"%ls -hide_banner -loglevel error -nostats -progress pipe:1 "
            L"-f lavfi -i \"testsrc2=size=%dx%d:rate=%d:duration=%d\" "
            L"%ls -an -sn -dn -t %d -f null NUL",
            q, HW_TEST_WIDTH, HW_TEST_HEIGHT, HW_TEST_FPS, HW_TEST_SECONDS,
            args, HW_TEST_SECONDS);
    }
    cmd[cap - 1] = L'\0';
    return 1;
}

static int run_one(const wchar_t *ffmpeg, int enc, int bit_depth, int warmup,
                   const wchar_t *out_path,
                   int *run_done, int run_total, ULONGLONG test_start,
                   HwProgressCallback cb, void *user, volatile int *cancel,
                   double *speed_out, double *wall_ms_out)
{
    wchar_t cmd[UTIL_PATH_MAX * 2 + 512];
    char *buf = NULL;
    size_t len = 0;
    DWORD code = 0;
    ULONGLONG t0, t1;
    UtilRunResult rr;
    double speed, wall_ms;
    HwProgress p;

    if (!build_test_cmd(ffmpeg, enc, bit_depth, out_path, cmd, sizeof(cmd) / sizeof(wchar_t))) {
        return -1;
    }

    t0 = GetTickCount64();
    rr = util_run_capture(cmd, HW_RUN_TIMEOUT, cancel, &buf, &len, &code);
    t1 = GetTickCount64();
    wall_ms = (double)(t1 - t0);

    if (rr == UTIL_RUN_CANCELLED) {
        free(buf);
        return -2;
    }
    if (rr != UTIL_RUN_OK || code != 0) {
        free(buf);
        return -1;
    }

    speed = parse_speed(buf);
    free(buf);
    if (speed <= 0.0 && wall_ms > 0.0) speed = 1000.0 / wall_ms;

    (*run_done)++;

    memset(&p, 0, sizeof(p));
    p.event = HW_EVT_RUN;
    p.run_done = *run_done;
    p.run_total = run_total;
    p.encoder = enc;
    p.bit_depth = bit_depth;
    p.warmup = warmup;
    p.speed = speed;
    p.elapsed_seconds = (double)(GetTickCount64() - test_start) / 1000.0;
    if (cb) cb(&p, user);

    if (speed_out) *speed_out = speed;
    if (wall_ms_out) *wall_ms_out = wall_ms;
    return 0;
}

static int test_encoder_depth(const wchar_t *ffmpeg, HwInfo *info, int enc, int bit_depth,
                              int *run_done, int run_total, ULONGLONG test_start,
                              HwProgressCallback cb, void *user, volatile int *cancel)
{
    int r;
    double speeds[HW_RUNS];
    double walls[HW_RUNS];
    double one_speed, one_wall;
    int ok;
    wchar_t temp[UTIL_PATH_MAX];
    const wchar_t *out_path = NULL;

    temp[0] = L'\0';
    if (bit_depth == 10) {
        make_10bit_temp(enc, temp, UTIL_PATH_MAX);
        if (!temp[0]) return 0;
        out_path = temp;
    }

    /* Warm-up, not measured. */
    r = run_one(ffmpeg, enc, bit_depth, 1, out_path, run_done, run_total, test_start,
                cb, user, cancel, NULL, NULL);
    if (r == -2) {
        if (temp[0]) DeleteFileW(temp);
        return -2;
    }
    if (r != 0) {
        if (temp[0]) DeleteFileW(temp);
        return 0;
    }

    if (bit_depth == 10) {
        wchar_t pix[32];
        wchar_t profile[64];
        if (!verify_10bit_file(temp, pix, 32, profile, 64)) {
            DeleteFileW(temp);
            return 0;
        }
        wcsncpy(info->pix_fmt10[enc], pix, 31);
        info->pix_fmt10[enc][31] = L'\0';
        wcsncpy(info->profile10[enc], profile, 63);
        info->profile10[enc][63] = L'\0';
        info->enc_10bit_verified[enc] = 1;
    }

    ok = 1;
    for (r = 0; r < HW_RUNS; r++) {
        if (run_one(ffmpeg, enc, bit_depth, 0, out_path, run_done, run_total, test_start,
                    cb, user, cancel, &one_speed, &one_wall) != 0) {
            ok = 0;
            break;
        }
        speeds[r] = one_speed;
        walls[r] = one_wall;
    }
    if (!ok) {
        if (temp[0]) DeleteFileW(temp);
        return 0;
    }

    {
        double med = median_of(speeds, HW_RUNS);
        double wmed = median_of(walls, HW_RUNS);
        double minv = speeds[0], maxv = speeds[0];
        int i;
        for (i = 1; i < HW_RUNS; i++) {
            if (speeds[i] < minv) minv = speeds[i];
            if (speeds[i] > maxv) maxv = speeds[i];
        }
        if (bit_depth == 8) {
            info->enc_ok8[enc] = 1;
            info->speed8_median[enc] = med;
            info->speed8_min[enc] = minv;
            info->speed8_max[enc] = maxv;
            info->wall8_median_ms[enc] = wmed;
            info->speed8_run_count[enc] = HW_RUNS;
            for (i = 0; i < HW_RUNS; i++) info->speed8_runs[enc][i] = speeds[i];
        } else {
            info->enc_ok10[enc] = 1;
            info->speed10_median[enc] = med;
            info->speed10_min[enc] = minv;
            info->speed10_max[enc] = maxv;
            info->wall10_median_ms[enc] = wmed;
            info->speed10_run_count[enc] = HW_RUNS;
            for (i = 0; i < HW_RUNS; i++) info->speed10_runs[enc][i] = speeds[i];
        }
    }
    if (temp[0]) DeleteFileW(temp);
    return 1;
}

HwTestResult hw_run_test(HwInfo *out, HwProgressCallback cb, void *user,
                         volatile int *cancel)
{
    wchar_t ffmpeg[UTIL_PATH_MAX];
    HwInfo info;
    HwInfo stored;
    ULONGLONG t0;
    int total = 0, done = 0;
    int enc;
    HwProgress evt;

    if (!out) return HW_TEST_ERROR;
    hw_info_init(&info);
    t0 = GetTickCount64();

    hw_detect_cpu(&info);
    hw_detect_gpu(&info);
    if (!hw_list_encoders(&info)) {
        memset(&evt, 0, sizeof(evt));
        evt.event = HW_EVT_ERROR;
        evt.error = HW_ERR_FFMPEG;
        if (cb) cb(&evt, user);
        *out = info;
        return HW_TEST_ERROR;
    }
    if (!util_find_tool(L"ffmpeg", ffmpeg, UTIL_PATH_MAX)) {
        memset(&evt, 0, sizeof(evt));
        evt.event = HW_EVT_ERROR;
        evt.error = HW_ERR_FFMPEG;
        if (cb) cb(&evt, user);
        *out = info;
        return HW_TEST_ERROR;
    }

    for (enc = 0; enc < HW_ENC_COUNT; enc++) {
        if (info.enc_listed[enc]) {
            total += HW_WARMUPS + HW_RUNS;
            if (hw_encoder_is_gpu(enc)) total += HW_WARMUPS + HW_RUNS;
        }
    }
    info.best8 = -1;
    info.best10 = -1;

    memset(&evt, 0, sizeof(evt));
    evt.event = HW_EVT_START;
    evt.run_total = total;
    evt.elapsed_seconds = (double)(GetTickCount64() - t0) / 1000.0;
    if (cb) cb(&evt, user);

    for (enc = 0; enc < HW_ENC_COUNT; enc++) {
        int r;
        if (!info.enc_listed[enc]) continue;
        if (cancel && *cancel) goto cancelled;

        r = test_encoder_depth(ffmpeg, &info, enc, 8, &done, total, t0, cb, user, cancel);
        if (r == -2) goto cancelled;

        if (r > 0 && hw_encoder_is_gpu(enc)) {
            r = test_encoder_depth(ffmpeg, &info, enc, 10, &done, total, t0, cb, user, cancel);
            if (r == -2) goto cancelled;
        }
    }

    /* The planned total can exceed the runs actually attempted after a failed
       warm-up. Clamp the final progress so the UI ends at 100%. */
    done = total;
    memset(&evt, 0, sizeof(evt));
    evt.event = HW_EVT_DONE;
    evt.run_done = done;
    evt.run_total = total;
    evt.elapsed_seconds = (double)(GetTickCount64() - t0) / 1000.0;
    if (cb) cb(&evt, user);

    {
        double best = -1.0;
        for (enc = 0; enc < HW_ENC_COUNT; enc++) {
            if (info.enc_ok8[enc] && info.speed8_median[enc] > best) {
                best = info.speed8_median[enc];
                info.best8 = enc;
            }
        }
    }
    {
        double best = -1.0;
        for (enc = HW_ENC_NVENC; enc <= HW_ENC_AMF; enc++) {
            if (info.enc_ok10[enc] && info.speed10_median[enc] > best) {
                best = info.speed10_median[enc];
                info.best10 = enc;
            }
        }
    }
    if (info.best8 < 0) {
        memset(&evt, 0, sizeof(evt));
        evt.event = HW_EVT_ERROR;
        evt.error = HW_ERR_NO_ENCODER;
        if (cb) cb(&evt, user);
        *out = info;
        return HW_TEST_ERROR;
    }

    info.recommended = info.best8;
    info.hdr_encoder = (info.best10 >= 0) ? info.best10 : HW_ENC_X265;

    info.profile_raw = (int)hw_choose_profile_raw(&info);
    info.profile = info.profile_raw;
    if (hw_load(&stored) &&
        _wcsicmp(stored.gpu_name, info.gpu_name) == 0 &&
        _wcsicmp(stored.gpu_driver, info.gpu_driver) == 0) {
        info.profile = (int)hw_choose_profile_hysteresis(&info, stored.profile);
    }
    info.total_seconds = (double)(GetTickCount64() - t0) / 1000.0;
    info.test_version = HW_TEST_VERSION;
    local_time_string(info.tested_at, 32);
    *out = info;
    return HW_TEST_OK;

cancelled:
    memset(&evt, 0, sizeof(evt));
    evt.event = HW_EVT_ERROR;
    evt.error = HW_ERR_CANCELLED;
    evt.elapsed_seconds = (double)(GetTickCount64() - t0) / 1000.0;
    if (cb) cb(&evt, user);
    *out = info;
    return HW_TEST_CANCELLED;
}

/* ------------------------------------------------------------------- log */
void hw_test_settings_text(wchar_t *out, size_t cap)
{
    static const wchar_t *text_value =
        L"source=testsrc2 " L"1920x1080@30, 3 s, 8-bit yuv420p / 10-bit p010le; "
        L"warmup=1; measured=3; median; 8-bit output=-f null NUL; "
        L"10-bit output=temp mp4, ffprobe-verified pix_fmt/profile\n"
        L"  x265 8-bit : libx265 -preset medium -crf 26 -x265-params log-level=error -pix_fmt yuv420p\n"
        L"  NVENC 8-bit: hevc_nvenc -preset p5 -tune hq -rc vbr -cq 28 -b:v 0 -pix_fmt yuv420p\n"
        L"  NVENC 10-bit: hevc_nvenc ... -pix_fmt p010le -profile:v main10\n"
        L"  QSV 8-bit  : hevc_qsv -preset medium -global_quality 26 -pix_fmt nv12\n"
        L"  QSV 10-bit : hevc_qsv ... -pix_fmt p010le -profile:v main10\n"
        L"  AMF 8-bit  : hevc_amf -quality balanced -rc cqp -qp_i 26 -qp_p 28 -qp_b 28 -pix_fmt yuv420p\n"
        L"  AMF 10-bit : hevc_amf ... -pix_fmt p010le -profile:v main10";
    if (!out || cap == 0) return;
    wcsncpy(out, text_value, cap - 1);
    out[cap - 1] = L'\0';
}

static const wchar_t *encoder_hardware_note(const HwInfo *info, int enc)
{
    if (!info) return L"unknown";
    if (info->enc_ok8[enc]) return L"tested_on_hardware";
    if (enc == HW_ENC_NVENC) {
        return (info->gpu_vendor == 0x10DE) ? L"tested_failed" : L"untested_on_hardware";
    }
    if (enc == HW_ENC_QSV) {
        return (info->gpu_vendor == 0x8086) ? L"tested_failed" : L"untested_on_hardware";
    }
    if (enc == HW_ENC_AMF) {
        return (info->gpu_vendor == 0x1002) ? L"tested_failed" : L"untested_on_hardware";
    }
    return L"cpu";
}

static void log_near_boundaries(FILE *f, const HwInfo *info)
{
    double gpu = hw_best_gpu_speed8(info);
    double x265 = info->speed8_median[HW_ENC_X265];
    int threads = info->logical_cores;

    if (fabs(x265 - 1.0) <= 0.15) {
        fwprintf(f, L"near_boundary=Light x265 1.0x, measured %.3fx (15%% window 0.85..1.15)\n", x265);
    }
    if (gpu > 0.0 && fabs(gpu - 3.0) <= 0.45) {
        fwprintf(f, L"near_boundary=Light GPU 3.0x, measured %.3fx (15%% window 2.55..3.45)\n", gpu);
    }
    if (gpu > 0.0 && fabs(gpu - 25.0) <= 3.75) {
        fwprintf(f, L"near_boundary=Thorough GPU 25.0x, measured %.3fx (15%% window 21.25..28.75)\n", gpu);
    }
    if (fabs(x265 - 3.0) <= 0.45) {
        fwprintf(f, L"near_boundary=Thorough x265 3.0x, measured %.3fx (15%% window 2.55..3.45)\n", x265);
    }
    if (threads >= 3 && threads <= 5) {
        fwprintf(f, L"near_boundary=Light threads 4, measured %d\n", threads);
    }
    if (threads >= 7 && threads <= 9) {
        fwprintf(f, L"near_boundary=Thorough threads 8, measured %d\n", threads);
    }
    if (threads >= 11 && threads <= 13) {
        fwprintf(f, L"near_boundary=Thorough threads 12, measured %d\n", threads);
    }
}

void hw_log_result(const HwInfo *info)
{
    wchar_t appdata[MAX_PATH];
    wchar_t dir[MAX_PATH];
    wchar_t path[MAX_PATH];
    wchar_t settings[2048];
    FILE *f;
    int enc;

    if (!info) return;
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appdata) != S_OK) return;
    _snwprintf(dir, MAX_PATH, L"%ls\\ff-compressor", appdata);
    dir[MAX_PATH - 1] = L'\0';
    SHCreateDirectoryExW(NULL, dir, NULL);
    _snwprintf(path, MAX_PATH, L"%ls\\hwtest.log", dir);
    path[MAX_PATH - 1] = L'\0';

    f = _wfopen(path, L"a, ccs=UTF-8");
    if (!f) return;

    hw_test_settings_text(settings, 2048);
    fwprintf(f, L"=== %ls ===\n", info->tested_at);
    fwprintf(f, L"test_version=%d\n", info->test_version);
    fwprintf(f, L"cpu=%ls logical=%d physical=%d\n",
             info->cpu_name, info->logical_cores, info->physical_cores);
    fwprintf(f, L"gpu=%ls vendor=0x%04X vram=%llu MB driver=%ls\n",
             info->gpu_name, info->gpu_vendor, info->gpu_memory_mb, info->gpu_driver);
    fwprintf(f, L"ffmpeg=%ls\n", info->ffmpeg_version);
    fwprintf(f, L"settings:\n%ls\n", settings);
    fwprintf(f, L"encoders:\n");
    for (enc = 0; enc < HW_ENC_COUNT; enc++) {
        int i;
        fwprintf(f, L"  %ls listed=%d ok8=%d ok10=%d hardware=%ls\n",
                 hw_encoder_name(enc), info->enc_listed[enc], info->enc_ok8[enc],
                 info->enc_ok10[enc], encoder_hardware_note(info, enc));
        if (info->enc_ok8[enc]) {
            fwprintf(f, L"    runs8=");
            for (i = 0; i < info->speed8_run_count[enc]; i++) {
                fwprintf(f, L"%ls%.3f", i ? L"," : L"", info->speed8_runs[enc][i]);
            }
            fwprintf(f, L" median=%.3f min=%.3f max=%.3f wall_ms=%.0f\n",
                     info->speed8_median[enc], info->speed8_min[enc], info->speed8_max[enc],
                     info->wall8_median_ms[enc]);
        }
        if (info->enc_ok10[enc]) {
            fwprintf(f, L"    runs10=");
            for (i = 0; i < info->speed10_run_count[enc]; i++) {
                fwprintf(f, L"%ls%.3f", i ? L"," : L"", info->speed10_runs[enc][i]);
            }
            fwprintf(f, L" median=%.3f min=%.3f max=%.3f wall_ms=%.0f\n",
                     info->speed10_median[enc], info->speed10_min[enc], info->speed10_max[enc],
                     info->wall10_median_ms[enc]);
        }
        if (info->enc_10bit_verified[enc]) {
            fwprintf(f, L"    10bit_verified=1 pix_fmt=%ls profile=%ls\n",
                     info->pix_fmt10[enc], info->profile10[enc]);
        }
    }
    log_near_boundaries(f, info);
    fwprintf(f, L"best8=%ls best10=%ls recommended=%ls hdr=%ls\n",
             hw_encoder_name(info->best8), hw_encoder_name(info->best10),
             hw_encoder_name(info->recommended), hw_encoder_name(info->hdr_encoder));
    fwprintf(f, L"profile_raw=%ls profile=%ls skipped=%d override=%ls\n",
             hw_profile_name(info->profile_raw), hw_profile_name(info->profile),
             info->skipped, hw_profile_name(hw_get_override()));
    fwprintf(f, L"total_seconds=%.3f\n\n", info->total_seconds);
    fclose(f);
}
