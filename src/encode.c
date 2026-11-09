/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "encode.h"
#include "presets.h"
#include "util.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#define ENCODE_CMD_MAX 16384
#define ENCODE_ARGS_MAX 8192

static void catf(wchar_t *dst, size_t cap, const wchar_t *fmt, ...)
{
    wchar_t tmp[2048];
    va_list ap;
    size_t len;

    va_start(ap, fmt);
    _vsnwprintf(tmp, 2048, fmt, ap);
    va_end(ap);
    tmp[2047] = L'\0';

    len = wcslen(dst);
    if (len + wcslen(tmp) < cap) wcscat(dst, tmp);
}

void encode_options_defaults(EncodeOptions *opt, const SourceInfo *src)
{
    if (!opt) return;
    memset(opt, 0, sizeof(*opt));
    opt->container = ENC_CONT_MKV;
    opt->video_codec = ENC_VCODEC_HEVC;
    opt->audio_codec = (src && src->audio_track_count > 0)
        ? ENC_ACODEC_OPUS : ENC_ACODEC_COPY;
    opt->bitrate = ENC_ABITRATE_MUSIC;
    opt->custom_kbps = ENC_BITRATE_HIGH_KBPS;
    opt->downmix_stereo = 1;
}

int encode_audio_available(int container, int codec)
{
    if (codec < 0 || codec >= ENC_ACODEC_COUNT) return 0;
    if (container == ENC_CONT_MKV) return 1;
    if (container == ENC_CONT_MP4) {
        switch (codec) {
            case ENC_ACODEC_COPY:
            case ENC_ACODEC_AAC:
            case ENC_ACODEC_MP3:
            case ENC_ACODEC_AC3:
            case ENC_ACODEC_EAC3:
                return 1;
            default:
                return 0;
        }
    }
    return 0;
}

int encode_audio_copy_available(const SourceInfo *src, int container)
{
    int i;
    if (!src) return 0;
    if (container != ENC_CONT_MP4) return 1;
    if (src->audio_track_count <= 0) return 1;
    if (src->audio_tracks_stored < src->audio_track_count) return 0;
    for (i = 0; i < src->audio_tracks_stored; i++) {
        const wchar_t *c = src->audio_tracks[i].codec;
        if (_wcsicmp(c, L"aac") != 0 && _wcsicmp(c, L"mp3") != 0 &&
            _wcsicmp(c, L"ac3") != 0 && _wcsicmp(c, L"eac3") != 0 &&
            _wcsicmp(c, L"alac") != 0) {
            return 0;
        }
    }
    return 1;
}

int encode_default_audio(int container)
{
    return (container == ENC_CONT_MP4) ? ENC_ACODEC_AAC : ENC_ACODEC_OPUS;
}

int encode_audio_has_bitrate(int codec)
{
    return codec != ENC_ACODEC_COPY && codec != ENC_ACODEC_FLAC;
}

int encode_audio_min_kbps(int codec)
{
    if (codec == ENC_ACODEC_AC3 || codec == ENC_ACODEC_EAC3) {
        return ENC_BITRATE_HIGH_KBPS;
    }
    return ENC_BITRATE_MIN_KBPS;
}

int encode_audio_force_stereo(int codec)
{
    return codec == ENC_ACODEC_MP3 || codec == ENC_ACODEC_AC3 ||
           codec == ENC_ACODEC_EAC3;
}

void encode_fix_bitrate(EncodeOptions *opt)
{
    if (!opt) return;
    if (opt->audio_codec != ENC_ACODEC_AC3 &&
        opt->audio_codec != ENC_ACODEC_EAC3) {
        return;
    }
    if (opt->bitrate == ENC_ABITRATE_CUSTOM) {
        if (opt->custom_kbps < ENC_BITRATE_HIGH_KBPS) {
            opt->bitrate = ENC_ABITRATE_HIGH;
        }
    } else if (opt->bitrate < ENC_ABITRATE_HIGH) {
        opt->bitrate = ENC_ABITRATE_HIGH;
    }
}

int encode_bitrate_kbps(const EncodeOptions *opt)
{
    int kbps;

    if (!opt || !encode_audio_has_bitrate(opt->audio_codec)) return 0;
    switch (opt->bitrate) {
        case ENC_ABITRATE_VOICE: kbps = ENC_BITRATE_VOICE_KBPS; break;
        case ENC_ABITRATE_MUSIC: kbps = ENC_BITRATE_MUSIC_KBPS; break;
        case ENC_ABITRATE_HIGH:  kbps = ENC_BITRATE_HIGH_KBPS; break;
        case ENC_ABITRATE_CUSTOM:
        default:                 kbps = opt->custom_kbps; break;
    }
    if (kbps < encode_audio_min_kbps(opt->audio_codec)) {
        kbps = encode_audio_min_kbps(opt->audio_codec);
    }
    if (kbps > ENC_BITRATE_MAX_KBPS) kbps = ENC_BITRATE_MAX_KBPS;
    return kbps;
}

static int track_channels(const SourceInfo *src, int index)
{
    if (src && index >= 0 && index < src->audio_tracks_stored) {
        return src->audio_tracks[index].channels;
    }
    return src ? src->audio_channels : 0;
}

static long long track_bitrate(const SourceInfo *src, int index)
{
    if (src && index >= 0 && index < src->audio_tracks_stored) {
        long long br = src->audio_tracks[index].bitrate;
        if (br > 0) return br;
    }
    if (!src) return 0;
    if (src->audio_track_count == 1) return src->audio_bitrate;
    return 0;
}

int encode_has_multichannel(const SourceInfo *src)
{
    int i;
    if (!src) return 0;
    if (src->audio_tracks_stored <= 0) return src->audio_channels > 2;
    for (i = 0; i < src->audio_tracks_stored; i++) {
        if (src->audio_tracks[i].channels > 2) return 1;
    }
    return 0;
}

int encode_audio_downmix_track(const SourceInfo *src, const EncodeOptions *opt,
                               int track_index)
{
    if (!src || !opt) return 0;
    if (!encode_audio_has_bitrate(opt->audio_codec)) return 0;
    if (track_channels(src, track_index) <= 2) return 0;
    return encode_audio_force_stereo(opt->audio_codec) || opt->downmix_stereo;
}

int encode_any_downmix(const SourceInfo *src, const EncodeOptions *opt)
{
    int i;
    if (!src || !opt) return 0;
    if (!encode_audio_has_bitrate(opt->audio_codec)) return 0;
    for (i = 0; i < src->audio_track_count; i++) {
        if (!encode_audio_track_copies(src, opt, i) &&
            encode_audio_downmix_track(src, opt, i)) {
            return 1;
        }
    }
    return 0;
}

int encode_audio_track_copies(const SourceInfo *src, const EncodeOptions *opt,
                              int track_index)
{
    long long source_bps;
    long long target_bps;

    if (!src || !opt) return 0;
    if (opt->audio_codec == ENC_ACODEC_COPY) return 1;
    if (!encode_audio_has_bitrate(opt->audio_codec)) return 0;

    source_bps = track_bitrate(src, track_index);
    target_bps = (long long)encode_bitrate_kbps(opt) * 1000;
    return target_bps > 0 && source_bps > 0 && source_bps < target_bps;
}

int encode_subtitle_is_text(const wchar_t *codec)
{
    static const wchar_t *text_codecs[] = {
        L"subrip", L"srt", L"ass", L"ssa", L"mov_text", L"webvtt", L"text",
        L"microdvd", L"subviewer", L"subviewer1", L"jacosub", L"sami",
        L"realtext", L"stl", L"pjs", L"vplayer", L"eia_608", L"eia_708",
        NULL
    };
    int i;
    if (!codec || !*codec) return 0;
    for (i = 0; text_codecs[i]; i++) {
        if (_wcsicmp(codec, text_codecs[i]) == 0) return 1;
    }
    return 0;
}

int encode_text_subtitle_count(const SourceInfo *src)
{
    int i, n = 0;
    if (!src) return 0;
    for (i = 0; i < src->subtitle_tracks_stored; i++) {
        if (encode_subtitle_is_text(src->subtitle_tracks[i].codec)) n++;
    }
    return n;
}

int encode_bitmap_subtitle_count(const SourceInfo *src)
{
    int n;
    if (!src) return 0;
    n = src->subtitle_track_count - encode_text_subtitle_count(src);
    return n > 0 ? n : 0;
}

static void strip_extension(const wchar_t *name, wchar_t *out, size_t cap)
{
    const wchar_t *dot;
    size_t len;

    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (!name) return;
    dot = wcsrchr(name, L'.');
    len = (dot && dot != name) ? (size_t)(dot - name) : wcslen(name);
    if (len >= cap) len = cap - 1;
    memcpy(out, name, len * sizeof(wchar_t));
    out[len] = L'\0';
}

static int build_candidate(const wchar_t *folder, const wchar_t *base,
                           int number, const wchar_t *ext,
                           wchar_t *out, size_t cap)
{
    wchar_t name[1024];

    if (number <= 1) {
        _snwprintf(name, 1024, L"%ls (compressed)%ls", base, ext);
    } else {
        _snwprintf(name, 1024, L"%ls (compressed %d)%ls", base, number, ext);
    }
    name[1023] = L'\0';
    if (!util_join_path(out, cap, folder, name)) return 0;
    return wcslen(out) < cap - 8;
}

static int choose_output(const SourceInfo *src, int container,
                         const wchar_t *folder, int start_number,
                         wchar_t *out, size_t cap)
{
    const wchar_t *ext = (container == ENC_CONT_MP4) ? L".mp4" : L".mkv";
    wchar_t base[512];
    int i;

    strip_extension(src->file_name, base, 512);
    if (!base[0]) wcscpy(base, L"video");
    for (i = start_number; i < 1000; i++) {
        if (!build_candidate(folder, base, i, ext, out, cap)) return 0;
        if (GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES) return 1;
    }
    return 0;
}

static int output_folder(const SourceInfo *src, wchar_t *out, size_t cap)
{
    wchar_t dir[UTIL_PATH_MAX];
    wchar_t folder[UTIL_PATH_MAX];
    wchar_t compressed[16];

    wcsncpy(compressed, L"Compressed", 15);
    compressed[15] = L'\0';
    util_dirname(src->path, dir, UTIL_PATH_MAX);
    if (!dir[0]) return 0;
    if (!util_join_path(folder, UTIL_PATH_MAX, dir, compressed)) return 0;
    {
        wchar_t full[UTIL_PATH_MAX];
        DWORD n = GetFullPathNameW(folder, UTIL_PATH_MAX, full, NULL);
        if (n == 0 || n >= UTIL_PATH_MAX) return 0;
        if (wcslen(full) >= cap) return 0;
        wcscpy(out, full);
    }
    return 1;
}

int encode_preview_output_path(const SourceInfo *src, int container,
                               wchar_t *out, size_t cap)
{
    wchar_t folder[UTIL_PATH_MAX];
    if (!src || !out || cap == 0) return 0;
    if (!output_folder(src, folder, UTIL_PATH_MAX)) return 0;
    return choose_output(src, container, folder, 1, out, cap);
}

static const wchar_t *audio_encoder_name(int codec)
{
    switch (codec) {
        case ENC_ACODEC_OPUS:   return L"libopus";
        case ENC_ACODEC_AAC:    return L"aac";
        case ENC_ACODEC_MP3:    return L"libmp3lame";
        case ENC_ACODEC_FLAC:   return L"flac";
        case ENC_ACODEC_AC3:    return L"ac3";
        case ENC_ACODEC_EAC3:   return L"eac3";
        case ENC_ACODEC_VORBIS: return L"libvorbis";
        default:                return L"copy";
    }
}

static void build_audio_args(const SourceInfo *src, const EncodeOptions *opt,
                             wchar_t *args, size_t cap)
{
    int i;

    args[0] = L'\0';
    if (src->audio_track_count <= 0) return;

    if (opt->audio_codec == ENC_ACODEC_COPY) {
        catf(args, cap, L" -c:a copy");
        return;
    }
    if (opt->audio_codec == ENC_ACODEC_FLAC) {
        catf(args, cap, L" -c:a flac");
        return;
    }

    catf(args, cap, L" -c:a %ls -b:a %dk",
         audio_encoder_name(opt->audio_codec), encode_bitrate_kbps(opt));

    for (i = 0; i < src->audio_tracks_stored; i++) {
        if (encode_audio_track_copies(src, opt, i)) {
            catf(args, cap, L" -c:a:%d copy", i);
        } else {
            catf(args, cap, L" -c:a:%d %ls -b:a:%d %dk",
                 i, audio_encoder_name(opt->audio_codec),
                 i, encode_bitrate_kbps(opt));
            if (encode_audio_downmix_track(src, opt, i)) {
                catf(args, cap, L" -ac:a:%d 2", i);
            }
        }
    }
}

static void build_subtitle_args(const SourceInfo *src, const EncodeOptions *opt,
                                wchar_t *args, size_t cap)
{
    int i;

    args[0] = L'\0';
    if (src->subtitle_track_count <= 0) return;

    if (opt->container == ENC_CONT_MKV) {
        catf(args, cap, L" -map 0:s? -c:s copy");
        return;
    }

    for (i = 0; i < src->subtitle_tracks_stored; i++) {
        if (encode_subtitle_is_text(src->subtitle_tracks[i].codec)) {
            catf(args, cap, L" -map 0:s:%d", i);
        }
    }
    if (encode_text_subtitle_count(src) > 0) {
        catf(args, cap, L" -c:s mov_text");
    }
}

int encode_build_command(const SourceInfo *src, int encoder, int level,
                         const EncodeOptions *opt, const wchar_t *out_path,
                         wchar_t *cmd, size_t cap, EncodeError *error)
{
    wchar_t ffmpeg[UTIL_PATH_MAX];
    wchar_t q_tool[UTIL_PATH_MAX + 4];
    wchar_t q_in[UTIL_PATH_MAX + 4];
    wchar_t q_out[UTIL_PATH_MAX + 4];
    wchar_t video[ENCODE_ARGS_MAX];
    wchar_t audio[ENCODE_ARGS_MAX];
    wchar_t subs[ENCODE_ARGS_MAX];
    wchar_t filter[1024];
    wchar_t color[512];

    if (error) *error = ENCODE_ERR_NONE;
    if (!src || !opt || !out_path || !cmd || cap == 0) {
        if (error) *error = ENCODE_ERR_PATH;
        return 0;
    }
    cmd[0] = L'\0';

    if (!util_find_tool(L"ffmpeg", ffmpeg, UTIL_PATH_MAX)) {
        if (error) *error = ENCODE_ERR_TOOL;
        return 0;
    }
    if (!util_quote_arg(q_tool, sizeof(q_tool) / sizeof(wchar_t), ffmpeg) ||
        !util_quote_arg(q_in, sizeof(q_in) / sizeof(wchar_t), src->path) ||
        !util_quote_arg(q_out, sizeof(q_out) / sizeof(wchar_t), out_path)) {
        if (error) *error = ENCODE_ERR_PATH;
        return 0;
    }
    if (!encode_audio_available(opt->container, opt->audio_codec) ||
        (opt->audio_codec == ENC_ACODEC_COPY &&
         !encode_audio_copy_available(src, opt->container))) {
        if (error) *error = ENCODE_ERR_AUDIO;
        return 0;
    }
    if (opt->container == ENC_CONT_MP4 &&
        src->subtitle_tracks_stored < src->subtitle_track_count) {
        if (error) *error = ENCODE_ERR_SUBS;
        return 0;
    }

    if (!presets_video_args_ex(src, encoder, level, opt->video_codec,
                               video, ENCODE_ARGS_MAX)) {
        if (error) *error = ENCODE_ERR_CODEC;
        return 0;
    }

    build_audio_args(src, opt, audio, ENCODE_ARGS_MAX);
    build_subtitle_args(src, opt, subs, ENCODE_ARGS_MAX);

    filter[0] = L'\0';
    color[0] = L'\0';
    if (src->is_hdr && opt->video_codec == ENC_VCODEC_H264) {
        wcscpy(filter,
            L" -vf \"zscale=t=linear:npl=100,format=gbrpf32le,"
            L"zscale=p=bt709,tonemap=tonemap=hable:desat=0,"
            L"zscale=t=bt709:m=bt709:r=tv,format=yuv420p\"");
        wcscat(color,
            L" -color_primaries bt709 -color_trc bt709 "
            L"-colorspace bt709 -color_range tv");
    }

    _snwprintf(cmd, cap,
        L"%ls -hide_banner -loglevel error -nostats -progress pipe:1 "
        L"-i %ls -map 0:v:0 %ls %ls%ls %ls %ls -dn "
        L"-fps_mode passthrough -map_metadata 0 -map_chapters 0 "
        L"-max_muxing_queue_size 4096%ls%ls -y %ls",
        q_tool, q_in,
        src->audio_track_count > 0 ? L"-map 0:a?" : L"-an",
        video, filter, audio, subs, color,
        opt->container == ENC_CONT_MP4
            ? (opt->video_codec == ENC_VCODEC_HEVC
                ? L" -tag:v hvc1 -movflags +faststart -f mp4"
                : L" -movflags +faststart -f mp4")
            : L" -f matroska",
        q_out);
    cmd[cap - 1] = L'\0';

    if (error && wcslen(cmd) == 0) *error = ENCODE_ERR_PATH;
    return cmd[0] != L'\0';
}

typedef struct {
    const SourceInfo *src;
    double duration;
    EncodeProgressCallback cb;
    void *user;
    EncodeProgress progress;
} ProgressCtx;

static void report_progress(ProgressCtx *ctx)
{
    if (ctx->cb) ctx->cb(&ctx->progress, ctx->user);
}

static void progress_line(const char *line, size_t len, void *user)
{
    ProgressCtx *ctx = (ProgressCtx*)user;
    if (!ctx || !line) return;

    if (len > 12 && strncmp(line, "out_time_us=", 12) == 0) {
        long long us = strtoll(line + 12, NULL, 10);
        if (us >= 0) {
            ctx->progress.out_seconds = (double)us / 1000000.0;
            if (ctx->duration > 0.0) {
                double f = ctx->progress.out_seconds / ctx->duration;
                if (f < 0.0) f = 0.0;
                if (f > 0.999) f = 0.999;
                ctx->progress.fraction = f;
            }
            report_progress(ctx);
        }
    } else if (len > 6 && strncmp(line, "speed=", 6) == 0) {
        char *end = NULL;
        double sp = strtod(line + 6, &end);
        if (end != line + 6 && sp > 0.0) {
            ctx->progress.speed = sp;
            report_progress(ctx);
        }
    }
}

static void detail_tail(const char *buf, size_t len, wchar_t *out, size_t cap)
{
    char tmp[512];
    size_t start, n = 0, i;
    int wlen;

    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (!buf || len == 0) return;

    start = (len > 400) ? len - 400 : 0;
    for (i = start; i < len && n < sizeof(tmp) - 1; i++) {
        char c = buf[i];
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        tmp[n++] = c;
    }
    tmp[n] = '\0';
    wlen = MultiByteToWideChar(CP_UTF8, 0, tmp, -1, out, (int)cap - 1);
    if (wlen <= 0) {
        wlen = MultiByteToWideChar(CP_ACP, 0, tmp, -1, out, (int)cap - 1);
    }
    if (wlen > 0) {
        out[wlen] = L'\0';
        util_trim(out);
    } else {
        out[0] = L'\0';
    }
}

static int verify_output(const SourceInfo *src, const EncodeOptions *opt,
                         const wchar_t *path)
{
    wchar_t ffprobe[UTIL_PATH_MAX];
    wchar_t q_tool[UTIL_PATH_MAX + 4];
    wchar_t q_path[UTIL_PATH_MAX + 4];
    wchar_t cmd[UTIL_PATH_MAX * 2 + 128];
    char *buf = NULL;
    size_t len = 0;
    DWORD code = 0;
    SourceInfo out;
    int expected_subs;
    double tol;

    if (!util_find_tool(L"ffprobe", ffprobe, UTIL_PATH_MAX)) return 0;
    if (!util_quote_arg(q_tool, sizeof(q_tool) / sizeof(wchar_t), ffprobe)) return 0;
    if (!util_quote_arg(q_path, sizeof(q_path) / sizeof(wchar_t), path)) return 0;

    _snwprintf(cmd, sizeof(cmd) / sizeof(wchar_t),
        L"%ls -v error -print_format json -show_format -show_streams %ls",
        q_tool, q_path);
    cmd[(sizeof(cmd) / sizeof(wchar_t)) - 1] = L'\0';

    if (util_run_capture(cmd, 60000, NULL, &buf, &len, &code) != UTIL_RUN_OK ||
        code != 0 || len == 0) {
        free(buf);
        return 0;
    }
    if (probe_parse_json(buf, &out) != PROBE_OK) {
        free(buf);
        return 0;
    }
    free(buf);

    if (out.audio_track_count != src->audio_track_count) return 0;
    expected_subs = (opt->container == ENC_CONT_MKV)
        ? src->subtitle_track_count : encode_text_subtitle_count(src);
    if (out.subtitle_track_count != expected_subs) return 0;
    /* FFmpeg's default autorotate normalizes display orientation, so compare
       the display size rather than the stored coded size. */
    if (out.display_width != src->display_width ||
        out.display_height != src->display_height) return 0;

    if (src->duration > 0.0 && out.duration > 0.0) {
        tol = src->duration * 0.02;
        if (tol < 2.0) tol = 2.0;
        if (fabs(out.duration - src->duration) > tol) return 0;
    }
    return 1;
}

EncodeResult encode_run(const SourceInfo *src, int encoder, int level,
                        const EncodeOptions *opt,
                        EncodeProgressCallback cb, void *user,
                        volatile int *cancel,
                        wchar_t *out_path, size_t out_cap,
                        long long *out_size, EncodeError *error,
                        wchar_t *detail, size_t detail_cap)
{
    wchar_t folder[UTIL_PATH_MAX];
    wchar_t partial[UTIL_PATH_MAX];
    wchar_t candidate[UTIL_PATH_MAX];
    wchar_t cmd[ENCODE_CMD_MAX];
    char *buf = NULL;
    size_t len = 0;
    DWORD code = 0;
    UtilRunResult run;
    EncodeError err = ENCODE_ERR_NONE;
    ProgressCtx ctx;
    int rc;
    int n;
    int verified;
    long long size = 0;

    if (out_path && out_cap) out_path[0] = L'\0';
    if (out_size) *out_size = 0;
    if (error) *error = ENCODE_ERR_NONE;
    if (detail && detail_cap) detail[0] = L'\0';
    if (!src || !opt || !out_path || out_cap == 0) {
        if (error) *error = ENCODE_ERR_PATH;
        return ENCODE_ERROR;
    }

    if (!output_folder(src, folder, UTIL_PATH_MAX)) {
        if (error) *error = ENCODE_ERR_PATH;
        return ENCODE_ERROR;
    }
    rc = SHCreateDirectoryExW(NULL, folder, NULL);
    if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS) {
        if (error) *error = ENCODE_ERR_FOLDER;
        return ENCODE_ERROR;
    }
    if (!choose_output(src, opt->container, folder, 1, candidate, UTIL_PATH_MAX)) {
        if (error) *error = ENCODE_ERR_PATH;
        return ENCODE_ERROR;
    }

    _snwprintf(partial, UTIL_PATH_MAX, L"%ls.%lu.partial", candidate,
               (unsigned long)GetCurrentProcessId());
    partial[UTIL_PATH_MAX - 1] = L'\0';
    if (util_file_exists(partial)) DeleteFileW(partial);

    if (!encode_build_command(src, encoder, level, opt, partial, cmd,
                              ENCODE_CMD_MAX, &err)) {
        if (error) *error = err;
        return ENCODE_ERROR;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.src = src;
    ctx.duration = src->duration;
    ctx.cb = cb;
    ctx.user = user;
    ctx.progress.fraction = 0.0;
    ctx.progress.verifying = 0;
    report_progress(&ctx);

    run = util_run_lines(cmd, 0, cancel, progress_line, &ctx,
                         &buf, &len, &code);
    if (run == UTIL_RUN_CANCELLED) {
        free(buf);
        DeleteFileW(partial);
        if (error) *error = ENCODE_ERR_NONE;
        return ENCODE_CANCELLED;
    }
    if (run != UTIL_RUN_OK || code != 0) {
        detail_tail(buf, len, detail, detail_cap);
        free(buf);
        DeleteFileW(partial);
        if (error) *error = (run == UTIL_RUN_START) ? ENCODE_ERR_START
                                                    : ENCODE_ERR_FFMPEG;
        return ENCODE_ERROR;
    }
    free(buf);

    if (cancel && *cancel) {
        DeleteFileW(partial);
        return ENCODE_CANCELLED;
    }

    ctx.progress.verifying = 1;
    ctx.progress.fraction = 1.0;
    report_progress(&ctx);

    verified = verify_output(src, opt, partial);
    if (!verified) {
        DeleteFileW(partial);
        if (error) *error = ENCODE_ERR_VERIFY;
        return ENCODE_VERIFY_FAILED;
    }
    if (cancel && *cancel) {
        DeleteFileW(partial);
        return ENCODE_CANCELLED;
    }

    {
        const wchar_t *ext = (opt->container == ENC_CONT_MP4) ? L".mp4" : L".mkv";
        wchar_t base[512];
        strip_extension(src->file_name, base, 512);
        if (!base[0]) wcscpy(base, L"video");
        for (n = 1; n < 1000; n++) {
            if (!build_candidate(folder, base, n, ext, candidate, UTIL_PATH_MAX)) {
                err = ENCODE_ERR_RENAME;
                break;
            }
            if (MoveFileExW(partial, candidate, MOVEFILE_COPY_ALLOWED)) {
                wcsncpy(out_path, candidate, out_cap - 1);
                out_path[out_cap - 1] = L'\0';
                util_get_file_size(candidate, &size);
                if (out_size) *out_size = size;
                ctx.progress.verifying = 0;
                ctx.progress.fraction = 1.0;
                report_progress(&ctx);
                if (error) *error = ENCODE_ERR_NONE;
                return ENCODE_OK;
            }
            if (GetLastError() != ERROR_ALREADY_EXISTS) {
                err = ENCODE_ERR_RENAME;
                break;
            }
        }
    }

    DeleteFileW(partial);
    if (error) *error = err;
    return ENCODE_ERROR;
}

void encode_open_folder(const wchar_t *file_path)
{
    wchar_t folder[UTIL_PATH_MAX];
    if (!file_path || !*file_path) return;
    util_dirname(file_path, folder, UTIL_PATH_MAX);
    if (folder[0]) {
        ShellExecuteW(NULL, L"open", folder, NULL, NULL, SW_SHOWNORMAL);
    }
}
