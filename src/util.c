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

    /* Development builds live in tests\build; the portable tools stay in the
       project's bin\ folder. This candidate is harmless for release builds. */
    _snwprintf(candidate, UTIL_PATH_MAX, L"%ls\\..\\..\\bin\\%ls.exe", app, name);
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

#define UTIL_CAPTURE_LIMIT (32u * 1024u * 1024u)

static int capture_append(char **buf, size_t *cap, size_t *len, const char *src, DWORD n)
{
    if (*len + (size_t)n + 1 > *cap) {
        size_t new_cap = *cap;
        char *p;
        while (new_cap < *len + (size_t)n + 1) {
            if (new_cap >= UTIL_CAPTURE_LIMIT) return 0;
            new_cap *= 2;
        }
        if (new_cap > UTIL_CAPTURE_LIMIT) new_cap = UTIL_CAPTURE_LIMIT;
        if (new_cap < *len + (size_t)n + 1) return 0;
        p = (char*)realloc(*buf, new_cap);
        if (!p) return 0;
        *buf = p;
        *cap = new_cap;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 1;
}

static void line_feed(char *line, size_t *line_len, const char *src, DWORD n,
                      UtilLineCallback cb, void *user)
{
    DWORD i;
    for (i = 0; i < n; i++) {
        char c = src[i];
        if (c == '\n') {
            line[*line_len] = '\0';
            if (cb) cb(line, *line_len, user);
            *line_len = 0;
        } else if (c != '\r') {
            if (*line_len < 4095) {
                line[(*line_len)++] = c;
            } else {
                line[*line_len] = '\0';
                if (cb) cb(line, *line_len, user);
                *line_len = 0;
            }
        }
    }
}

static void capture_drain(HANDLE pipe, char **buf, size_t *cap, size_t *len, int *oom,
                          char *line, size_t *line_len,
                          UtilLineCallback cb, void *user)
{
    char chunk[8192];
    DWORD avail = 0;
    while (PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
        DWORD want = avail > (DWORD)sizeof(chunk) ? (DWORD)sizeof(chunk) : avail;
        DWORD got = 0;
        if (!ReadFile(pipe, chunk, want, &got, NULL) || got == 0) break;
        if (!capture_append(buf, cap, len, chunk, got)) { *oom = 1; return; }
        line_feed(line, line_len, chunk, got, cb, user);
    }
}

UtilRunResult util_run_lines(const wchar_t *cmd, DWORD timeout_ms,
                             volatile int *cancel,
                             UtilLineCallback cb, void *user,
                             char **out, size_t *out_len,
                             DWORD *exit_code)
{
    HANDLE rd = NULL, wr = NULL, job = NULL;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t *cmd_copy = NULL;
    char *buf = NULL;
    char line[4096];
    size_t cap = 65536, len = 0, line_len = 0;
    int oom = 0, timed_out = 0, was_cancelled = 0;
    DWORD code = 0;
    DWORD started;
    UtilRunResult result = UTIL_RUN_START;

    memset(&pi, 0, sizeof(pi));
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (exit_code) *exit_code = 0;
    if (!cmd || !out || !out_len) return UTIL_RUN_START;
    if (cancel && *cancel) return UTIL_RUN_CANCELLED;

    cmd_copy = _wcsdup(cmd);
    if (!cmd_copy) return UTIL_RUN_OOM;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) goto cleanup;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(NULL, cmd_copy, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        NULL, NULL, &si, &pi)) {
        goto cleanup;
    }
    CloseHandle(wr);
    wr = NULL;

    job = CreateJobObjectW(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
        memset(&info, 0, sizeof(info));
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info)) ||
            !AssignProcessToJobObject(job, pi.hProcess)) {
            CloseHandle(job);
            job = NULL;
        }
    }
    ResumeThread(pi.hThread);

    buf = (char*)malloc(cap);
    if (!buf) {
        result = UTIL_RUN_OOM;
        goto stop_process;
    }
    buf[0] = '\0';
    line[0] = '\0';

    started = GetTickCount();
    for (;;) {
        DWORD avail = 0;
        if (cancel && *cancel) {
            was_cancelled = 1;
            break;
        }
        if (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            capture_drain(rd, &buf, &cap, &len, &oom, line, &line_len, cb, user);
            if (oom) break;
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) {
            capture_drain(rd, &buf, &cap, &len, &oom, line, &line_len, cb, user);
            break;
        }
        if (timeout_ms != 0 && GetTickCount() - started > timeout_ms) {
            timed_out = 1;
            break;
        }
    }
    if (line_len > 0 && cb) {
        line[line_len] = '\0';
        cb(line, line_len, user);
        line_len = 0;
    }

stop_process:
    if (was_cancelled || timed_out || result == UTIL_RUN_OOM) {
        if (job) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
    }
    WaitForSingleObject(pi.hProcess, 3000);
    GetExitCodeProcess(pi.hProcess, &code);

cleanup:
    if (wr) CloseHandle(wr);
    if (rd) CloseHandle(rd);
    if (job) CloseHandle(job);
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    free(cmd_copy);

    if (result == UTIL_RUN_OOM) {
        free(buf);
        return UTIL_RUN_OOM;
    }
    if (was_cancelled) { free(buf); return UTIL_RUN_CANCELLED; }
    if (timed_out) { free(buf); return UTIL_RUN_TIMEOUT; }
    if (!buf) return UTIL_RUN_START;

    *out = buf;
    *out_len = len;
    if (exit_code) *exit_code = code;
    return UTIL_RUN_OK;
}

UtilRunResult util_run_capture(const wchar_t *cmd, DWORD timeout_ms,
                               volatile int *cancel,
                               char **out, size_t *out_len,
                               DWORD *exit_code)
{
    return util_run_lines(cmd, timeout_ms, cancel, NULL, NULL,
                          out, out_len, exit_code);
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
