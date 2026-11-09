/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "probe.h"
#include "cjson.h"
#include "strings.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define PROBE_TIMEOUT_MS 30000u

static void copy_utf8(const char *src, wchar_t *dst, size_t cap)
{
    int n;
    if (!dst || cap == 0) return;
    dst[0] = L'\0';
    if (!src || !*src) return;
    n = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)cap - 1);
    if (n <= 0) {
        n = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)cap - 1);
    }
    if (n > 0) dst[n] = L'\0';
    else dst[0] = L'\0';
}

static cJSON *obj(cJSON *parent, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(parent, key);
}

static void get_str(cJSON *parent, const char *key, wchar_t *dst, size_t cap)
{
    cJSON *it = obj(parent, key);
    if (it && cJSON_IsString(it) && it->valuestring) copy_utf8(it->valuestring, dst, cap);
}

static double get_num(cJSON *parent, const char *key)
{
    cJSON *it = obj(parent, key);
    if (!it) return 0.0;
    if (cJSON_IsNumber(it)) return it->valuedouble;
    if (cJSON_IsString(it) && it->valuestring) {
        char *end = NULL;
        double v = strtod(it->valuestring, &end);
        if (end != it->valuestring) return v;
    }
    return 0.0;
}

static long long get_ll(cJSON *parent, const char *key)
{
    double v = get_num(parent, key);
    if (v <= 0.0) return 0;
    return (long long)(v + 0.5);
}

static int get_int(cJSON *parent, const char *key)
{
    return (int)get_num(parent, key);
}

static double parse_ratio(const char *s)
{
    double a, b;
    char *end = NULL;
    if (!s || !*s) return 0.0;
    if (strcmp(s, "0/0") == 0 || _stricmp(s, "N/A") == 0) return 0.0;
    if (sscanf(s, "%lf/%lf", &a, &b) == 2 && b != 0.0) return a / b;
    a = strtod(s, &end);
    return (end != s) ? a : 0.0;
}

static int pix_fmt_bit_depth(const wchar_t *pf, int raw_bits)
{
    if (raw_bits > 8) return raw_bits;
    if (!pf) return 8;
    if (wcsstr(pf, L"p010") || wcsstr(pf, L"p016")) return 10;
    if (wcsstr(pf, L"p210") || wcsstr(pf, L"p216")) return 10;
    if (wcsstr(pf, L"10le") || wcsstr(pf, L"10be")) return 10;
    if (wcsstr(pf, L"12le") || wcsstr(pf, L"12be")) return 12;
    if (wcsstr(pf, L"14le") || wcsstr(pf, L"14be")) return 14;
    if (wcsstr(pf, L"16le") || wcsstr(pf, L"16be")) return 16;
    if (wcsstr(pf, L"9le") || wcsstr(pf, L"9be")) return 9;
    return 8;
}

static int is_sar_square(const wchar_t *sar)
{
    if (!sar || !*sar) return 1;
    if (wcscmp(sar, L"1:1") == 0) return 1;
    if (wcscmp(sar, L"0:1") == 0) return 1;
    if (wcscmp(sar, L"N/A") == 0) return 1;
    return 0;
}

static void recompute_derived(SourceInfo *out)
{
    if (out->format_bitrate <= 0 && out->duration > 0.0 && out->file_size > 0) {
        out->format_bitrate = (long long)(((double)out->file_size * 8.0) / out->duration);
    }
    if (out->video_bitrate <= 0) {
        out->video_bitrate = out->format_bitrate;
        if (out->audio_bitrate > 0 && out->video_bitrate > out->audio_bitrate) {
            out->video_bitrate -= out->audio_bitrate;
        }
    }
    if (out->coded_width > 0 && out->coded_height > 0 && out->fps > 0.0 && out->video_bitrate > 0) {
        out->bits_per_pixel = (double)out->video_bitrate /
            ((double)out->coded_width * (double)out->coded_height * out->fps);
    }
    if (out->display_width <= 0 || out->display_height <= 0) {
        out->display_width = out->coded_width;
        out->display_height = out->coded_height;
    }
}

ProbeError probe_parse_json(const char *json, SourceInfo *out)
{
    cJSON *root;
    cJSON *format;
    cJSON *streams;
    cJSON *video = NULL;
    cJSON *audio = NULL;
    int i, n;

    if (!out) return PROBE_ERR_BAD_JSON;
    memset(out, 0, sizeof(*out));
    if (!json || !*json) return PROBE_ERR_BAD_JSON;

    root = cJSON_Parse(json);
    if (!root) return PROBE_ERR_BAD_JSON;

    format = obj(root, "format");
    if (format) {
        get_str(format, "format_name", out->container, 128);
        get_str(format, "format_long_name", out->container_long, 256);
        out->duration = get_num(format, "duration");
        out->file_size = get_ll(format, "size");
        out->format_bitrate = get_ll(format, "bit_rate");
    }

    streams = obj(root, "streams");
    if (cJSON_IsArray(streams)) {
        n = cJSON_GetArraySize(streams);
        for (i = 0; i < n; i++) {
            cJSON *s = cJSON_GetArrayItem(streams, i);
            cJSON *type;
            const char *t;
            if (!cJSON_IsObject(s)) continue;
            type = obj(s, "codec_type");
            t = (type && cJSON_IsString(type)) ? type->valuestring : NULL;
            if (!t) continue;

            if (strcmp(t, "video") == 0) {
                cJSON *disp = obj(s, "disposition");
                int attached_pic = 0;
                if (disp && cJSON_IsObject(disp)) attached_pic = get_int(disp, "attached_pic");
                out->video_track_count++;
                if (!video && !attached_pic) video = s;
            } else if (strcmp(t, "audio") == 0) {
                out->audio_track_count++;
                if (!audio) audio = s;
            } else if (strcmp(t, "subtitle") == 0) {
                out->subtitle_track_count++;
            }
        }
    }

    if (!video) {
        cJSON_Delete(root);
        return PROBE_ERR_NO_VIDEO;
    }

    get_str(video, "codec_name", out->video_codec, 64);
    get_str(video, "codec_long_name", out->video_codec_long, 128);
    get_str(video, "profile", out->profile, 64);
    get_str(video, "pix_fmt", out->pix_fmt, 64);
    get_str(video, "color_primaries", out->color_primaries, 64);
    get_str(video, "color_transfer", out->color_transfer, 64);
    get_str(video, "color_space", out->color_space, 64);
    get_str(video, "color_range", out->color_range, 32);
    get_str(video, "field_order", out->field_order, 32);
    get_str(video, "sample_aspect_ratio", out->sar, 32);
    get_str(video, "display_aspect_ratio", out->dar, 32);

    out->coded_width = get_int(video, "width");
    out->coded_height = get_int(video, "height");
    out->display_width = out->coded_width;
    out->display_height = out->coded_height;
    out->video_bitrate = get_ll(video, "bit_rate");
    out->nb_frames = get_ll(video, "nb_frames");
    if (out->duration <= 0.0) out->duration = get_num(video, "duration");

    {
        cJSON *avg = obj(video, "avg_frame_rate");
        cJSON *r = obj(video, "r_frame_rate");
        out->fps = (avg && cJSON_IsString(avg)) ? parse_ratio(avg->valuestring) : 0.0;
        out->r_fps = (r && cJSON_IsString(r)) ? parse_ratio(r->valuestring) : 0.0;
        if (out->fps <= 0.0) out->fps = out->r_fps;
        if (out->fps > 0.0 && out->r_fps > 0.0) {
            double diff = out->fps - out->r_fps;
            if (diff < 0.0) diff = -diff;
            out->is_vfr = (diff > out->r_fps * 0.015);
        }
    }

    out->bit_depth = pix_fmt_bit_depth(out->pix_fmt, get_int(video, "bits_per_raw_sample"));

    /* Rotation can be a display-matrix side data entry or a legacy tag. */
    {
        cJSON *side = obj(video, "side_data_list");
        if (cJSON_IsArray(side)) {
            int sn = cJSON_GetArraySize(side);
            int k;
            for (k = 0; k < sn; k++) {
                cJSON *sd = cJSON_GetArrayItem(side, k);
                cJSON *rot = sd ? obj(sd, "rotation") : NULL;
                cJSON *sdt = sd ? obj(sd, "side_data_type") : NULL;
                if (rot && cJSON_IsNumber(rot)) out->rotation = (int)rot->valuedouble;
                if (sdt && cJSON_IsString(sdt) && sdt->valuestring) {
                    if (strstr(sdt->valuestring, "DOVI") || strstr(sdt->valuestring, "Dolby Vision") ||
                        strstr(sdt->valuestring, "Dolby")) {
                        out->is_dolby_vision = 1;
                    }
                }
                if (sd && obj(sd, "dv_profile")) out->is_dolby_vision = 1;
            }
        }
        if (out->rotation == 0) {
            cJSON *tags = obj(video, "tags");
            if (tags && cJSON_IsObject(tags)) {
                cJSON *rot = obj(tags, "rotate");
                if (rot && cJSON_IsString(rot) && rot->valuestring) out->rotation = atoi(rot->valuestring);
                else if (rot && cJSON_IsNumber(rot)) out->rotation = (int)rot->valuedouble;
            }
        }
    }
    if (out->rotation < 0) out->rotation = -out->rotation;
    out->rotation %= 360;
    if (out->rotation == 90 || out->rotation == 270) {
        out->display_width = out->coded_height;
        out->display_height = out->coded_width;
    }

    if (out->color_transfer[0] &&
        (wcscmp(out->color_transfer, L"smpte2084") == 0 ||
         wcscmp(out->color_transfer, L"smpte428") == 0 ||
         wcscmp(out->color_transfer, L"bt2020-10") == 0 ||
         wcscmp(out->color_transfer, L"bt2020-12") == 0)) {
        out->is_hdr = 1;
    }
    if (out->color_transfer[0] && wcscmp(out->color_transfer, L"arib-std-b67") == 0) {
        out->is_hlg = 1;
        out->is_hdr = 1;
    }
    if (out->is_dolby_vision) out->is_hdr = 1;

    if (out->field_order[0] &&
        wcscmp(out->field_order, L"progressive") != 0 &&
        wcscmp(out->field_order, L"unknown") != 0) {
        out->is_interlaced = 1;
    }
    out->is_anamorphic = !is_sar_square(out->sar);

    if (audio) {
        get_str(audio, "codec_name", out->audio_codec, 64);
        get_str(audio, "codec_long_name", out->audio_codec_long, 128);
        out->audio_channels = get_int(audio, "channels");
        out->audio_sample_rate = get_int(audio, "sample_rate");
        out->audio_bitrate = get_ll(audio, "bit_rate");
        {
            cJSON *tags = obj(audio, "tags");
            if (tags && cJSON_IsObject(tags)) get_str(tags, "language", out->audio_language, 32);
        }
    }

    recompute_derived(out);
    cJSON_Delete(root);
    return PROBE_OK;
}

const wchar_t *probe_error_text(ProbeError err)
{
    switch (err) {
        case PROBE_ERR_NOT_FOUND:      return text(STR_MSG_MISSING_TOOLS_TEXT);
        case PROBE_ERR_START:          return text(STR_MSG_START_FAILED);
        case PROBE_ERR_TIMEOUT:        return text(STR_MSG_TIMEOUT);
        case PROBE_ERR_BAD_JSON:       return text(STR_MSG_BAD_JSON);
        case PROBE_ERR_NO_VIDEO:       return text(STR_MSG_NO_VIDEO);
        case PROBE_ERR_OUT_OF_MEMORY:  return text(STR_MSG_OUT_OF_MEMORY);
        case PROBE_ERR_FAILED:
        default:                       return text(STR_MSG_PROBE_FAILED);
    }
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double*)a;
    double y = *(const double*)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

/* avg_frame_rate and r_frame_rate are identical for many MKV VFR files.
   Sample the first five seconds of packet timestamps and look for irregular
   spacing. This is metadata-only (no frame decoding) and stays fast. */
static int detect_vfr_extra(const wchar_t *ffprobe, const wchar_t *path)
{
    wchar_t q_tool[UTIL_PATH_MAX + 4];
    wchar_t q_path[UTIL_PATH_MAX + 4];
    wchar_t cmd[UTIL_PATH_MAX * 2 + 256];
    char *buf = NULL;
    size_t len = 0;
    DWORD exit_code = 0;
    double pts[2048];
    double deltas[2047];
    int n = 0, dn = 0, i;
    char *p;

    if (!util_quote_arg(q_tool, sizeof(q_tool) / sizeof(wchar_t), ffprobe)) return 0;
    if (!util_quote_arg(q_path, sizeof(q_path) / sizeof(wchar_t), path)) return 0;

    _snwprintf(cmd, sizeof(cmd) / sizeof(wchar_t),
        L"%ls -v error -select_streams v:0 -read_intervals %%+5 "
        L"-show_entries packet=pts_time -of csv=p=0 %ls", q_tool, q_path);
    cmd[(sizeof(cmd) / sizeof(wchar_t)) - 1] = L'\0';

    if (util_run_capture(cmd, PROBE_TIMEOUT_MS, NULL, &buf, &len, &exit_code) != UTIL_RUN_OK) return 0;
    if (exit_code != 0 || len == 0) { free(buf); return 0; }

    p = buf;
    while (*p && n < 2048) {
        char *end = NULL;
        double v = strtod(p, &end);
        if (end != p) pts[n++] = v;
        while (*p && *p != '\n' && *p != '\r') p++;
        while (*p == '\n' || *p == '\r') p++;
    }
    free(buf);

    if (n < 10) return 0;
    qsort(pts, (size_t)n, sizeof(double), cmp_double);

    for (i = 1; i < n; i++) {
        double d = pts[i] - pts[i - 1];
        if (d > 0.0005) deltas[dn++] = d;
    }
    if (dn < 8) return 0;
    qsort(deltas, (size_t)dn, sizeof(double), cmp_double);

    {
        double median = deltas[dn / 2];
        double max_dev = 0.0;
        for (i = 0; i < dn; i++) {
            double d = deltas[i] - median;
            if (d < 0.0) d = -d;
            if (d > max_dev) max_dev = d;
        }
        return max_dev > 0.006;
    }
}

ProbeError probe_run(const wchar_t *path, SourceInfo *out)
{
    wchar_t ffprobe[UTIL_PATH_MAX];
    wchar_t q_tool[UTIL_PATH_MAX + 4];
    wchar_t q_path[UTIL_PATH_MAX + 4];
    wchar_t cmd[UTIL_PATH_MAX * 2 + 128];
    char *buf = NULL;
    size_t len = 0;
    DWORD exit_code = 0;
    UtilRunResult run;
    ProbeError result;

    if (!out) return PROBE_ERR_BAD_JSON;
    memset(out, 0, sizeof(*out));
    if (!path || !*path) return PROBE_ERR_FAILED;

    if (!util_find_tool(L"ffprobe", ffprobe, UTIL_PATH_MAX)) return PROBE_ERR_NOT_FOUND;
    if (!util_quote_arg(q_tool, sizeof(q_tool) / sizeof(wchar_t), ffprobe)) return PROBE_ERR_START;
    if (!util_quote_arg(q_path, sizeof(q_path) / sizeof(wchar_t), path)) return PROBE_ERR_START;

    _snwprintf(cmd, sizeof(cmd) / sizeof(wchar_t),
        L"%ls -v error -print_format json -show_format -show_streams %ls", q_tool, q_path);
    cmd[(sizeof(cmd) / sizeof(wchar_t)) - 1] = L'\0';

    run = util_run_capture(cmd, PROBE_TIMEOUT_MS, NULL, &buf, &len, &exit_code);
    if (run == UTIL_RUN_START) return PROBE_ERR_START;
    if (run == UTIL_RUN_TIMEOUT) return PROBE_ERR_TIMEOUT;
    if (run == UTIL_RUN_OOM) return PROBE_ERR_OUT_OF_MEMORY;
    if (run != UTIL_RUN_OK || exit_code != 0 || len == 0) {
        free(buf);
        return PROBE_ERR_FAILED;
    }

    result = probe_parse_json(buf, out);
    free(buf);
    if (result != PROBE_OK) return result;

    wcsncpy(out->path, path, UTIL_PATH_MAX - 1);
    out->path[UTIL_PATH_MAX - 1] = L'\0';
    wcsncpy(out->file_name, util_basename(path), 255);
    out->file_name[255] = L'\0';
    if (out->file_size <= 0) util_get_file_size(path, &out->file_size);
    recompute_derived(out);

    if (!out->is_vfr) {
        out->is_vfr = detect_vfr_extra(ffprobe, path);
    }
    return PROBE_OK;
}
