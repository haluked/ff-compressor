/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "util.h"
#include "strings.h"

#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>
#include <string.h>

int util_file_exists(const wchar_t *path)
{
    DWORD attr;
    if (!path || !*path) return 0;
    attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

int util_dir_exists(const wchar_t *path)
{
    DWORD attr;
    if (!path || !*path) return 0;
    attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

void util_app_dir(wchar_t *out, size_t cap)
{
    wchar_t *slash;
    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (GetModuleFileNameW(NULL, out, (DWORD)cap) == 0) return;
    out[cap - 1] = L'\0';
    slash = wcsrchr(out, L'\\');
    if (slash) *slash = L'\0';
}

int util_find_tool(const wchar_t *name, wchar_t *out, size_t cap)
{
    wchar_t app[UTIL_PATH_MAX];
    wchar_t candidate[UTIL_PATH_MAX];

    if (!name || !out || cap < 8) return 0;

    util_app_dir(app, UTIL_PATH_MAX);
    if (!app[0]) return 0;

    _snwprintf(candidate, UTIL_PATH_MAX, L"%ls\\bin\\%ls.exe", app, name);
    candidate[UTIL_PATH_MAX - 1] = L'\0';
    if (util_file_exists(candidate)) { wcsncpy(out, candidate, cap - 1); out[cap - 1] = L'\0'; return 1; }

    _snwprintf(candidate, UTIL_PATH_MAX, L"%ls\\%ls.exe", app, name);
    candidate[UTIL_PATH_MAX - 1] = L'\0';
    if (util_file_exists(candidate)) { wcsncpy(out, candidate, cap - 1); out[cap - 1] = L'\0'; return 1; }

    _snwprintf(candidate, UTIL_PATH_MAX, L"%ls\\..\\bin\\%ls.exe", app, name);
    candidate[UTIL_PATH_MAX - 1] = L'\0';
    if (util_file_exists(candidate)) { wcsncpy(out, candidate, cap - 1); out[cap - 1] = L'\0'; return 1; }

    if (SearchPathW(NULL, name, L".exe", (DWORD)cap, out, NULL) > 0) {
        out[cap - 1] = L'\0';
        return 1;
    }
    out[0] = L'\0';
    return 0;
}

int util_get_file_size(const wchar_t *path, long long *out)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (out) *out = 0;
    if (!path || !*path || !out) return 0;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) return 0;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
    *out = ((long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    return 1;
}

const wchar_t *util_basename(const wchar_t *path)
{
    const wchar_t *slash;
    if (!path) return L"";
    slash = wcsrchr(path, L'\\');
    return slash ? slash + 1 : path;
}

void util_dirname(const wchar_t *path, wchar_t *out, size_t cap)
{
    const wchar_t *slash;
    size_t len;
    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (!path || !*path) return;
    slash = wcsrchr(path, L'\\');
    if (!slash) { wcsncpy(out, L".", cap - 1); out[cap - 1] = L'\0'; return; }
    len = (size_t)(slash - path);
    if (len >= cap) len = cap - 1;
    memcpy(out, path, len * sizeof(wchar_t));
    out[len] = L'\0';
}

int util_join_path(wchar_t *out, size_t cap, const wchar_t *dir, const wchar_t *name)
{
    size_t len;
    if (!out || cap == 0) return 0;
    out[0] = L'\0';
    if (!dir || !name) return 0;
    len = wcslen(dir);
    if (len > 0 && (dir[len - 1] == L'\\' || dir[len - 1] == L'/')) {
        _snwprintf(out, cap, L"%ls%ls", dir, name);
    } else {
        _snwprintf(out, cap, L"%ls\\%ls", dir, name);
    }
    out[cap - 1] = L'\0';
    return 1;
}

int util_quote_arg(wchar_t *out, size_t cap, const wchar_t *arg)
{
    size_t need;
    if (!out || cap == 0) return 0;
    out[0] = L'\0';
    if (!arg) return 0;
    if (wcschr(arg, L'"')) return 0;
    need = wcslen(arg) + 3;
    if (need > cap) return 0;
    _snwprintf(out, cap, L"\"%ls\"", arg);
    out[cap - 1] = L'\0';
    return 1;
}

void util_trim(wchar_t *s)
{
    wchar_t *p;
    size_t len;
    if (!s) return;
    p = s;
    while (*p && iswspace(*p)) p++;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));
    len = wcslen(s);
    while (len > 0 && iswspace(s[len - 1])) s[--len] = L'\0';
}

void util_get_temp_dir(wchar_t *out, size_t cap)
{
    DWORD n;
    if (!out || cap == 0) return;
    out[0] = L'\0';
    n = GetTempPathW((DWORD)cap, out);
    if (n == 0 || n >= cap) {
        wcsncpy(out, L".", cap - 1);
        out[cap - 1] = L'\0';
    }
}

void util_format_bytes(long long bytes, wchar_t *out, size_t cap)
{
    double v;
    if (!out || cap == 0) return;
    if (bytes <= 0) { wcsncpy(out, text(STR_VALUE_UNKNOWN), cap - 1); out[cap - 1] = L'\0'; return; }
    v = (double)bytes;
    if (v >= 1024.0 * 1024.0 * 1024.0) _snwprintf(out, cap, L"%.2f GB", v / (1024.0 * 1024.0 * 1024.0));
    else if (v >= 1024.0 * 1024.0)       _snwprintf(out, cap, L"%.1f MB", v / (1024.0 * 1024.0));
    else if (v >= 1024.0)                _snwprintf(out, cap, L"%.0f KB", v / 1024.0);
    else                                 _snwprintf(out, cap, L"%lld B", bytes);
    out[cap - 1] = L'\0';
}

void util_format_duration(double seconds, wchar_t *out, size_t cap)
{
    long long total;
    int h, m, s;
    if (!out || cap == 0) return;
    if (seconds <= 0.0) { wcsncpy(out, text(STR_VALUE_UNKNOWN), cap - 1); out[cap - 1] = L'\0'; return; }
    total = (long long)(seconds + 0.5);
    h = (int)(total / 3600);
    m = (int)((total % 3600) / 60);
    s = (int)(total % 60);
    if (h > 0) _snwprintf(out, cap, L"%d:%02d:%02d", h, m, s);
    else       _snwprintf(out, cap, L"%d:%02d", m, s);
    out[cap - 1] = L'\0';
}

void util_format_bitrate(long long bps, wchar_t *out, size_t cap)
{
    if (!out || cap == 0) return;
    if (bps <= 0) { wcsncpy(out, text(STR_VALUE_UNKNOWN), cap - 1); out[cap - 1] = L'\0'; return; }
    if (bps >= 1000000)      _snwprintf(out, cap, L"%.2f Mbps", (double)bps / 1000000.0);
    else if (bps >= 1000)    _snwprintf(out, cap, L"%.0f kbps", (double)bps / 1000.0);
    else                     _snwprintf(out, cap, L"%lld bps", bps);
    out[cap - 1] = L'\0';
}

void util_format_fps(double fps, wchar_t *out, size_t cap)
{
    if (!out || cap == 0) return;
    if (fps <= 0.0) { wcsncpy(out, text(STR_VALUE_UNKNOWN), cap - 1); out[cap - 1] = L'\0'; return; }
    _snwprintf(out, cap, text(STR_FPS_FMT), fps);
    out[cap - 1] = L'\0';
}
