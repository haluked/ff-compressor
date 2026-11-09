/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UTIL_H
#define UTIL_H

#include <wchar.h>
#include <windows.h>

#define UTIL_PATH_MAX 4096

typedef enum {
    UTIL_RUN_OK = 0,
    UTIL_RUN_START,
    UTIL_RUN_TIMEOUT,
    UTIL_RUN_CANCELLED,
    UTIL_RUN_OOM
} UtilRunResult;

typedef void (*UtilLineCallback)(const char *line, size_t len, void *user);

/* Run a hidden child process, capture merged stdout/stderr (bounded), and
   return its exit code. `cancel` is polled every 20 ms; `timeout_ms` 0 means
   no timeout. The caller owns the returned buffer. */
UtilRunResult util_run_capture(const wchar_t *cmd, DWORD timeout_ms,
                               volatile int *cancel,
                               char **out, size_t *out_len,
                               DWORD *exit_code);

/* Same as util_run_capture, but also calls cb for every complete line of
   merged output while the process runs. Used for FFmpeg progress. */
UtilRunResult util_run_lines(const wchar_t *cmd, DWORD timeout_ms,
                             volatile int *cancel,
                             UtilLineCallback cb, void *user,
                             char **out, size_t *out_len,
                             DWORD *exit_code);

int  util_file_exists(const wchar_t *path);
int  util_dir_exists(const wchar_t *path);
void util_app_dir(wchar_t *out, size_t cap);
int  util_find_tool(const wchar_t *name, wchar_t *out, size_t cap);
int  util_get_file_size(const wchar_t *path, long long *out);

const wchar_t *util_basename(const wchar_t *path);
void util_dirname(const wchar_t *path, wchar_t *out, size_t cap);
int  util_join_path(wchar_t *out, size_t cap, const wchar_t *dir, const wchar_t *name);
int  util_quote_arg(wchar_t *out, size_t cap, const wchar_t *arg);
void util_trim(wchar_t *s);
void util_get_temp_dir(wchar_t *out, size_t cap);

void util_format_bytes(long long bytes, wchar_t *out, size_t cap);
void util_format_duration(double seconds, wchar_t *out, size_t cap);
void util_format_bitrate(long long bps, wchar_t *out, size_t cap);
void util_format_fps(double fps, wchar_t *out, size_t cap);

#endif /* UTIL_H */
