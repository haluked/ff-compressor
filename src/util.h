/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UTIL_H
#define UTIL_H

#include <wchar.h>

#define UTIL_PATH_MAX 4096

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
