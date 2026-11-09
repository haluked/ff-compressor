/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "presets.h"
#include "strings.h"
#include "util.h"

#include <stdio.h>
#include <wchar.h>

static int clamp_level(int level)
{
    if (level < 0) return 0;
    if (level >= QUALITY_LEVEL_COUNT) return QUALITY_LEVEL_COUNT - 1;
    return level;
}

static const wchar_t *color_or(const wchar_t *value, const wchar_t *fallback)
{
    return (value && value[0]) ? value : fallback;
}

int presets_available_levels(int profile, int *levels)
{
    if (!levels) return 0;
    if (profile == HW_PROFILE_LIGHT) {
        levels[0] = QUALITY_SMALL;
        levels[1] = QUALITY_BALANCED;
        levels[2] = QUALITY_HIGH;
        return 3;
    }
    levels[0] = QUALITY_TINY;
    levels[1] = QUALITY_SMALL;
    levels[2] = QUALITY_BALANCED;
    levels[3] = QUALITY_HIGH;
    return 4;
}

const wchar_t *presets_level_name(int level)
{
    switch (level) {
        case QUALITY_TINY:     return text(STR_QUALITY_TINY);
        case QUALITY_SMALL:    return text(STR_QUALITY_SMALL);
        case QUALITY_BALANCED: return text(STR_QUALITY_BALANCED);
        case QUALITY_HIGH:     return text(STR_QUALITY_HIGH);
        default:               return text(STR_QUALITY_BALANCED);
    }
}

const wchar_t *presets_level_note(int level)
{
    switch (level) {
        case QUALITY_TINY:     return text(STR_QUALITY_TINY_NOTE);
        case QUALITY_SMALL:    return text(STR_QUALITY_SMALL_NOTE);
        case QUALITY_BALANCED: return text(STR_QUALITY_BALANCED_NOTE);
        case QUALITY_HIGH:     return text(STR_QUALITY_HIGH_NOTE);
        default:               return text(STR_QUALITY_BALANCED_NOTE);
    }
}

int presets_video_args(const SourceInfo *src, int encoder, int level,
                       wchar_t *out, size_t cap)
{
    static const int q_x265[4] = { 30, 27, 24, 21 };
    static const int q_nvenc[4] = { 36, 33, 30, 27 };
    static const int q_qsv[4] = { 34, 31, 28, 25 };
    static const int q_amf[4] = { 36, 33, 30, 27 };
    wchar_t color[384] = L"";
    const wchar_t *pix = L"yuv420p";
    int ten, q;

    if (!src || !out || cap == 0) return 0;
    out[0] = L'\0';
    level = clamp_level(level);
    ten = src->bit_depth >= 10;

    if (src->color_primaries[0] || src->color_transfer[0] || src->is_hdr) {
        _snwprintf(color, 384,
            L" -color_primaries %ls -color_trc %ls -colorspace %ls -color_range %ls",
            color_or(src->color_primaries, src->is_hdr ? L"bt2020" : L"bt709"),
            color_or(src->color_transfer, src->is_hdr ? L"smpte2084" : L"bt709"),
            color_or(src->color_space, src->is_hdr ? L"bt2020nc" : L"bt709"),
            color_or(src->color_range, L"tv"));
        color[383] = L'\0';
    }

    if (encoder == HW_ENC_X265) {
        q = q_x265[level];
        if (ten) pix = L"yuv420p10le";
        if (src->is_hdr && ten) {
            _snwprintf(out, cap,
                L"-c:v libx265 -preset medium -crf %d -pix_fmt %ls -profile:v main10 "
                L"-x265-params \"log-level=error:colorprim=%ls:transfer=%ls:colormatrix=%ls%s\"%ls",
                q, pix,
                color_or(src->color_primaries, L"bt2020"),
                color_or(src->color_transfer, L"smpte2084"),
                color_or(src->color_space, L"bt2020nc"),
                (_wcsicmp(src->color_transfer, L"smpte2084") == 0) ? L":hdr10=1" : L"",
                color);
        } else {
            _snwprintf(out, cap,
                L"-c:v libx265 -preset medium -crf %d -pix_fmt %ls%s%s",
                q, pix, ten ? L" -profile:v main10" : L"", color);
        }
    } else if (encoder == HW_ENC_NVENC) {
        q = q_nvenc[level];
        if (ten) pix = L"p010le";
        _snwprintf(out, cap,
            L"-c:v hevc_nvenc -preset p5 -tune hq -rc vbr -cq %d -b:v 0 -pix_fmt %ls%s%s",
            q, pix, ten ? L" -profile:v main10" : L"", color);
    } else if (encoder == HW_ENC_QSV) {
        q = q_qsv[level];
        pix = ten ? L"p010le" : L"nv12";
        _snwprintf(out, cap,
            L"-c:v hevc_qsv -preset medium -global_quality %d -pix_fmt %ls%s%s",
            q, pix, ten ? L" -profile:v main10" : L"", color);
    } else if (encoder == HW_ENC_AMF) {
        int qp;
        q = q_amf[level];
        qp = q + 2;
        if (qp > 51) qp = 51;
        if (ten) pix = L"p010le";
        _snwprintf(out, cap,
            L"-c:v hevc_amf -quality balanced -rc cqp -qp_i %d -qp_p %d -qp_b %d "
            L"-pix_fmt %ls%s%s",
            q, qp, qp, pix, ten ? L" -profile:v main10" : L"", color);
    } else {
        return 0;
    }

    out[cap - 1] = L'\0';
    return out[0] != L'\0';
}

int presets_build_sample_cmd(const SourceInfo *src, int encoder, int level,
                             double start_sec, double duration_sec,
                             const wchar_t *out_path,
                             wchar_t *cmd, size_t cap)
{
    wchar_t ffmpeg[UTIL_PATH_MAX];
    wchar_t q_tool[UTIL_PATH_MAX + 4];
    wchar_t q_in[UTIL_PATH_MAX + 4];
    wchar_t q_out[UTIL_PATH_MAX + 4];
    wchar_t args[1024];

    if (!src || !cmd || !out_path) return 0;
    if (!util_find_tool(L"ffmpeg", ffmpeg, UTIL_PATH_MAX)) return 0;
    if (!util_quote_arg(q_tool, sizeof(q_tool) / sizeof(wchar_t), ffmpeg)) return 0;
    if (!util_quote_arg(q_in, sizeof(q_in) / sizeof(wchar_t), src->path)) return 0;
    if (!util_quote_arg(q_out, sizeof(q_out) / sizeof(wchar_t), out_path)) return 0;
    if (!presets_video_args(src, encoder, level, args, 1024)) return 0;

    _snwprintf(cmd, cap,
        L"%ls -hide_banner -loglevel error -nostats -progress pipe:1 "
        L"-ss %.3f -i %ls -t %.3f -map 0:v:0 -an -sn -dn -fps_mode passthrough "
        L"%ls -f matroska -y %ls",
        q_tool, start_sec, q_in, duration_sec, args, q_out);
    cmd[cap - 1] = L'\0';
    return 1;
}

int presets_build_full_cmd(const SourceInfo *src, int encoder, int level,
                           const wchar_t *out_path, int include_audio,
                           wchar_t *cmd, size_t cap)
{
    wchar_t ffmpeg[UTIL_PATH_MAX];
    wchar_t q_tool[UTIL_PATH_MAX + 4];
    wchar_t q_in[UTIL_PATH_MAX + 4];
    wchar_t q_out[UTIL_PATH_MAX + 4];
    wchar_t args[1024];

    if (!src || !cmd || !out_path) return 0;
    if (!util_find_tool(L"ffmpeg", ffmpeg, UTIL_PATH_MAX)) return 0;
    if (!util_quote_arg(q_tool, sizeof(q_tool) / sizeof(wchar_t), ffmpeg)) return 0;
    if (!util_quote_arg(q_in, sizeof(q_in) / sizeof(wchar_t), src->path)) return 0;
    if (!util_quote_arg(q_out, sizeof(q_out) / sizeof(wchar_t), out_path)) return 0;
    if (!presets_video_args(src, encoder, level, args, 1024)) return 0;

    if (include_audio) {
        _snwprintf(cmd, cap,
            L"%ls -hide_banner -loglevel error -nostats -progress pipe:1 "
            L"-i %ls -map 0:v:0 -map 0:a? %ls -c:a copy "
            L"-fps_mode passthrough -map_metadata 0 -map_chapters 0 "
            L"-f matroska -y %ls",
            q_tool, q_in, args, q_out);
    } else {
        _snwprintf(cmd, cap,
            L"%ls -hide_banner -loglevel error -nostats -progress pipe:1 "
            L"-i %ls -map 0:v:0 -an -sn -dn -fps_mode passthrough "
            L"-map_metadata 0 -map_chapters 0 -f matroska -y %ls",
            q_tool, q_in, args, q_out);
    }
    cmd[cap - 1] = L'\0';
    return 1;
}
