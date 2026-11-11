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

#define ENCODE_CMD_MAX 24576
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
    /* Keep the original audio unless the user asks for a conversion. A 36 kbps
       stereo default on a 5.1 film saves almost nothing and degrades the
       result, so Copy is the safe default in both containers. */
    opt->audio_codec = ENC_ACODEC_COPY;
    opt->bitrate = ENC_ABITRATE_HIGH;
    opt->custom_kbps = 128;
    opt->downmix_stereo = 0;
    opt->audio_mask = 0; /* every audio track */
    opt->audio_none = 0;
    opt->video_track = (src && src->video_primary >= 0)
        ? src->video_primary : 0;
    opt->subtitle_mask = 0; /* every subtitle track */
    opt->subtitles_none = 0;
    opt->encoder_override = -1;
    opt->crf_override = 0;
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

int encode_audio_selected(const SourceInfo *src, const EncodeOptions *opt,
                          int track_index)
{
    if (!src || track_index < 0 || track_index >= src->audio_tracks_stored) return 0;
    if (opt && opt->audio_none) return 0;
    if (!opt || opt->audio_mask == 0) return 1;
    if (track_index >= 32) return 0;
    return (opt->audio_mask & (1u << track_index)) != 0;
}

int encode_selected_audio_count(const SourceInfo *src, const EncodeOptions *opt)
{
    int i, n = 0;
    if (!src || src->audio_track_count <= 0) return 0;
    if (opt && opt->audio_none) return 0;
    if (!opt || opt->audio_mask == 0) {
        /* If ffprobe reported more tracks than we could store, the command
           maps all of them through the 0:a? shorthand. */
        return (src->audio_track_count > src->audio_tracks_stored)
            ? src->audio_track_count : src->audio_tracks_stored;
    }
    for (i = 0; i < src->audio_tracks_stored; i++) {
        if (encode_audio_selected(src, opt, i)) n++;
    }
    return n;
}

int encode_selected_video_track(const SourceInfo *src, const EncodeOptions *opt)
{
    int index;
    if (!src || src->video_tracks_stored <= 0) return 0;
    index = opt ? opt->video_track : src->video_primary;
    if (index < 0 || index >= src->video_tracks_stored) {
        index = (src->video_primary >= 0 && src->video_primary < src->video_tracks_stored)
            ? src->video_primary : 0;
    }
    return index;
}

long long encode_audio_total_bitrate(const SourceInfo *src)
{
    long long total = 0;
    int i;
    if (!src || src->audio_track_count <= 0) return 0;
    for (i = 0; i < src->audio_tracks_stored; i++) {
        if (src->audio_tracks[i].bitrate > 0) total += src->audio_tracks[i].bitrate;
        else total += 128000; /* unknown bitrate: use a neutral estimate */
    }
    if (total <= 0) {
        total = (src->audio_bitrate > 0) ? src->audio_bitrate
                                         : (long long)src->audio_track_count * 128000;
    }
    return total;
}

long long encode_selected_audio_bitrate(const SourceInfo *src,
                                        const EncodeOptions *opt)
{
    long long total = 0;
    int i;
    if (!src || src->audio_track_count <= 0) return 0;
    if (opt && opt->audio_none) return 0;
    if (!opt || opt->audio_mask == 0) return encode_audio_total_bitrate(src);
    for (i = 0; i < src->audio_tracks_stored; i++) {
        if (!encode_audio_selected(src, opt, i)) continue;
        total += (src->audio_tracks[i].bitrate > 0)
            ? src->audio_tracks[i].bitrate : 128000;
    }
    return total;
}

int encode_audio_copy_available(const SourceInfo *src, int container)
{
    int i;
    if (!src) return 0;
    if (container != ENC_CONT_MP4) return 1;
    if (src->audio_track_count <= 0) return 1;
    if (src->audio_tracks_stored < src->audio_track_count) return 0;
    for (i = 0; i < src->audio_tracks_stored; i++) {
        const wchar_t *c;
        /* The UI calls this before an option struct exists. Verify every
           track in that case; encode_build_command verifies the selection. */
        c = src->audio_tracks[i].codec;
        if (_wcsicmp(c, L"aac") != 0 && _wcsicmp(c, L"mp3") != 0 &&
            _wcsicmp(c, L"ac3") != 0 && _wcsicmp(c, L"eac3") != 0 &&
            _wcsicmp(c, L"alac") != 0) {
            return 0;
        }
    }
    return 1;
}

int encode_audio_copy_available_sel(const SourceInfo *src,
                                    const EncodeOptions *opt,
                                    int container)
{
    int i;
    if (!src || !opt) return 0;
    if (container != ENC_CONT_MP4) return 1;
    if (encode_selected_audio_count(src, opt) <= 0) return 1;
    if (src->audio_tracks_stored < src->audio_track_count && opt->audio_mask == 0) return 0;
    for (i = 0; i < src->audio_tracks_stored; i++) {
        const wchar_t *c;
        if (!encode_audio_selected(src, opt, i)) continue;
        c = src->audio_tracks[i].codec;
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
    if (codec == ENC_ACODEC_VORBIS) {
        return ENC_BITRATE_VORBIS_MIN_KBPS;
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

static const wchar_t *track_layout(const SourceInfo *src, int index)
{
    if (src && index >= 0 && index < src->audio_tracks_stored) {
        return src->audio_tracks[index].layout;
    }
    return L"";
}

/* Opus only accepts its canonical layouts. Side/wide variants such as
   5.1(side) or 7.1(wide) make libopus abort the whole encode with -22. */
static const wchar_t *opus_safe_layout(int channels)
{
    switch (channels) {
        case 1:  return L"mono";
        case 2:  return L"stereo";
        case 3:  return L"3.0";
        case 4:  return L"quad";
        case 5:  return L"5.0";
        case 6:  return L"5.1";
        case 7:  return L"6.1";
        case 8:  return L"7.1";
        default: return NULL;
    }
}

static int opus_layout_is_safe(const wchar_t *layout)
{
    static const wchar_t *safe[] = {
        L"mono", L"stereo", L"3.0", L"4.0", L"quad",
        L"5.0", L"5.1", L"6.1", L"7.1", NULL
    };
    int i;
    if (!layout || !*layout) return 0;
    for (i = 0; safe[i]; i++) {
        if (_wcsicmp(layout, safe[i]) == 0) return 1;
    }
    return 0;
}

static int layout_is_known(const wchar_t *layout)
{
    return layout && *layout && _wcsicmp(layout, L"unknown") != 0;
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
    int count;
    if (!src || !opt) return 0;
    if (!encode_audio_has_bitrate(opt->audio_codec)) return 0;
    count = src->audio_tracks_stored;
    if (count <= 0 && src->audio_track_count > 0) {
        return src->audio_channels > 2;
    }
    for (i = 0; i < count; i++) {
        if (!encode_audio_selected(src, opt, i)) continue;
        if (!encode_audio_track_copies(src, opt, i) &&
            encode_audio_downmix_track(src, opt, i)) {
            return 1;
        }
    }
    return 0;
}

static const wchar_t *audio_codec_name(int codec)
{
    switch (codec) {
        case ENC_ACODEC_OPUS:   return L"opus";
        case ENC_ACODEC_AAC:    return L"aac";
        case ENC_ACODEC_MP3:    return L"mp3";
        case ENC_ACODEC_FLAC:   return L"flac";
        case ENC_ACODEC_AC3:    return L"ac3";
        case ENC_ACODEC_EAC3:   return L"eac3";
        case ENC_ACODEC_VORBIS: return L"vorbis";
        default:                return NULL;
    }
}

static int container_accepts_copy_codec(int container, const wchar_t *codec)
{
    if (!codec) return 0;
    if (container != ENC_CONT_MP4) return 1;
    return _wcsicmp(codec, L"aac") == 0 || _wcsicmp(codec, L"mp3") == 0 ||
           _wcsicmp(codec, L"ac3") == 0 || _wcsicmp(codec, L"eac3") == 0 ||
           _wcsicmp(codec, L"alac") == 0;
}

/* A track may only be copied as-is when the source codec already matches the
   codec the user asked for and the target container accepts it.  Otherwise
   the requested conversion must actually happen. */
int encode_audio_track_copies(const SourceInfo *src, const EncodeOptions *opt,
                              int track_index)
{
    const wchar_t *source_codec;
    const wchar_t *wanted_codec;
    long long source_bps;
    long long target_bps;

    if (!src || !opt) return 0;
    if (track_index < 0 || track_index >= src->audio_tracks_stored) return 0;
    source_codec = src->audio_tracks[track_index].codec;

    if (opt->audio_codec == ENC_ACODEC_COPY) {
        return container_accepts_copy_codec(opt->container, source_codec);
    }
    if (!encode_audio_has_bitrate(opt->audio_codec)) return 0;

    wanted_codec = audio_codec_name(opt->audio_codec);
    if (!wanted_codec || _wcsicmp(source_codec, wanted_codec) != 0) return 0;
    if (!container_accepts_copy_codec(opt->container, source_codec)) return 0;
    /* Do not bypass an explicitly requested stereo downmix or a codec whose
       target encoder is stereo-only. */
    if (track_channels(src, track_index) > 2 &&
        (opt->downmix_stereo || encode_audio_force_stereo(opt->audio_codec))) {
        return 0;
    }

    source_bps = track_bitrate(src, track_index);
    target_bps = (long long)encode_bitrate_kbps(opt) * 1000;
    return target_bps > 0 && source_bps > 0 && source_bps < target_bps;
}

long long encode_estimated_audio_bitrate(const SourceInfo *src,
                                         const EncodeOptions *opt)
{
    long long total = 0;
    int base_kbps, i;

    if (!src || !opt || src->audio_track_count <= 0) return 0;
    if (opt->audio_codec == ENC_ACODEC_COPY || opt->audio_codec == ENC_ACODEC_FLAC) {
        return encode_selected_audio_bitrate(src, opt);
    }

    base_kbps = encode_bitrate_kbps(opt);
    if (base_kbps <= 0) return encode_selected_audio_bitrate(src, opt);

    for (i = 0; i < src->audio_tracks_stored; i++) {
        int channels, kbps;
        if (!encode_audio_selected(src, opt, i)) continue;
        if (encode_audio_track_copies(src, opt, i)) {
            long long source_bps = track_bitrate(src, i);
            total += (source_bps > 0) ? source_bps : 128000;
            continue;
        }
        channels = track_channels(src, i);
        kbps = base_kbps;
        if (!encode_audio_downmix_track(src, opt, i) && channels > 2) {
            kbps = kbps * channels / 2;
            if (kbps > ENC_BITRATE_MAX_KBPS) kbps = ENC_BITRATE_MAX_KBPS;
        }
        total += (long long)kbps * 1000;
    }
    return total;
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

int encode_subtitle_selected(const SourceInfo *src, const EncodeOptions *opt,
                             int track_index)
{
    if (!src || track_index < 0 || track_index >= src->subtitle_tracks_stored) return 0;
    if (opt && opt->subtitles_none) return 0;
    if (!opt || opt->subtitle_mask == 0) return 1;
    if (track_index >= 64) return 0;
    return (opt->subtitle_mask & (1ull << track_index)) != 0;
}

int encode_selected_text_subtitle_count(const SourceInfo *src,
                                        const EncodeOptions *opt)
{
    int i, n = 0;
    if (!src) return 0;
    for (i = 0; i < src->subtitle_tracks_stored; i++) {
        if (!encode_subtitle_selected(src, opt, i)) continue;
        if (encode_subtitle_is_text(src->subtitle_tracks[i].codec)) n++;
    }
    return n;
}

int encode_selected_bitmap_subtitle_count(const SourceInfo *src,
                                          const EncodeOptions *opt)
{
    int i, n = 0;
    if (!src) return 0;
    for (i = 0; i < src->subtitle_tracks_stored; i++) {
        if (!encode_subtitle_selected(src, opt, i)) continue;
        if (!encode_subtitle_is_text(src->subtitle_tracks[i].codec)) n++;
    }
    return n;
}

int encode_selected_subtitle_count(const SourceInfo *src,
                                   const EncodeOptions *opt, int container)
{
    int i, n = 0;
    if (!src || src->subtitle_track_count <= 0) return 0;
    if (opt && opt->subtitles_none) return 0;
    if (!opt || opt->subtitle_mask == 0) {
        if (src->subtitle_track_count > src->subtitle_tracks_stored) {
            return (container == ENC_CONT_MP4)
                ? encode_text_subtitle_count(src)
                : src->subtitle_track_count;
        }
        if (container == ENC_CONT_MP4) return encode_text_subtitle_count(src);
        return src->subtitle_tracks_stored;
    }
    for (i = 0; i < src->subtitle_tracks_stored; i++) {
        if (!encode_subtitle_selected(src, opt, i)) continue;
        if (container == ENC_CONT_MP4 &&
            !encode_subtitle_is_text(src->subtitle_tracks[i].codec)) continue;
        n++;
    }
    return n;
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
    return wcslen(out) < cap - 24;
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
    int i, out_index = 0;
    int fix_layout = (opt->audio_codec == ENC_ACODEC_OPUS ||
                      opt->audio_codec == ENC_ACODEC_VORBIS);

    args[0] = L'\0';
    if (src->audio_track_count <= 0) return;
    if (encode_selected_audio_count(src, opt) <= 0) return;

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
        if (!encode_audio_selected(src, opt, i)) continue;

        if (encode_audio_track_copies(src, opt, i)) {
            catf(args, cap, L" -c:a:%d copy", out_index);
        } else {
            int channels = track_channels(src, i);
            int downmix = encode_audio_downmix_track(src, opt, i);
            const wchar_t *layout = track_layout(src, i);
            int kbps = encode_bitrate_kbps(opt);

            /* A surround track needs a larger budget than a stereo one.
               Keep the UI value as the stereo baseline and scale it when the
               user deliberately keeps the multichannel layout. */
            if (!downmix && channels > 2) {
                kbps = kbps * channels / 2;
                if (kbps > ENC_BITRATE_MAX_KBPS) kbps = ENC_BITRATE_MAX_KBPS;
            }

            catf(args, cap, L" -c:a:%d %ls -b:a:%d %dk",
                 out_index, audio_encoder_name(opt->audio_codec),
                 out_index, kbps);

            if (downmix) {
                if (fix_layout && channels > 2 && layout_is_known(layout) &&
                    !opus_layout_is_safe(layout)) {
                    /* Explicit remap avoids relying on -ac negotiating a
                       non-canonical source layout in older FFmpeg builds. */
                    catf(args, cap,
                         L" -af:a:%d \"aformat=channel_layouts=stereo\"",
                         out_index);
                } else {
                    catf(args, cap, L" -ac:a:%d 2", out_index);
                }
            } else if (fix_layout) {
                const wchar_t *safe = opus_safe_layout(channels);
                if (channels <= 0 || channels > 8) {
                    /* libopus cannot represent unknown or >8 channel audio. */
                    catf(args, cap, L" -ac:a:%d 2", out_index);
                } else if (channels <= 2) {
                    catf(args, cap, L" -ac:a:%d %d", out_index, channels);
                } else if (!opus_layout_is_safe(layout)) {
                    catf(args, cap, L" -af:a:%d \"aformat=channel_layouts=%ls\"",
                         out_index, safe ? safe : L"5.1");
                }
            }
            if (opt->audio_codec == ENC_ACODEC_OPUS ||
                opt->audio_codec == ENC_ACODEC_VORBIS) {
                catf(args, cap, L" -ar:a:%d 48000", out_index);
            }
        }
        out_index++;
    }
}

/* Matroska accepts SRT/ASS/SSA/WebVTT text and the common bitmap formats.
   Other text codecs, most notably mov_text, must be converted: copying them
   into MKV makes FFmpeg abort the whole encode. */
static int mkv_subtitle_copy_ok(const wchar_t *codec)
{
    if (!codec || !*codec) return 1;
    if (_wcsicmp(codec, L"subrip") == 0 || _wcsicmp(codec, L"srt") == 0 ||
        _wcsicmp(codec, L"ass") == 0 || _wcsicmp(codec, L"ssa") == 0 ||
        _wcsicmp(codec, L"webvtt") == 0) {
        return 1;
    }
    if (!encode_subtitle_is_text(codec)) return 1;
    return 0;
}

static void build_subtitle_args(const SourceInfo *src, const EncodeOptions *opt,
                                wchar_t *args, size_t cap)
{
    int i, mapped = 0;

    args[0] = L'\0';
    if (src->subtitle_track_count <= 0) return;
    if (opt->subtitles_none) return;

    if (opt->container == ENC_CONT_MKV) {
        if (opt->subtitle_mask == 0 &&
            src->subtitle_track_count > src->subtitle_tracks_stored) {
            catf(args, cap, L" -map 0:s? -c:s copy");
            return;
        }
        for (i = 0; i < src->subtitle_tracks_stored; i++) {
            if (!encode_subtitle_selected(src, opt, i)) continue;
            if (src->subtitle_tracks[i].has_index) {
                catf(args, cap, L" -map 0:%d", src->subtitle_tracks[i].index);
            } else {
                catf(args, cap, L" -map 0:s:%d", i);
            }
            mapped++;
        }
        if (mapped > 0) {
            int out_sub = 0;
            catf(args, cap, L" -c:s copy");
            for (i = 0; i < src->subtitle_tracks_stored; i++) {
                if (!encode_subtitle_selected(src, opt, i)) continue;
                if (!mkv_subtitle_copy_ok(src->subtitle_tracks[i].codec)) {
                    catf(args, cap, L" -c:s:%d srt", out_sub);
                }
                out_sub++;
            }
        }
        return;
    }

    for (i = 0; i < src->subtitle_tracks_stored; i++) {
        if (!encode_subtitle_selected(src, opt, i)) continue;
        if (!encode_subtitle_is_text(src->subtitle_tracks[i].codec)) continue;
        if (src->subtitle_tracks[i].has_index) {
            catf(args, cap, L" -map 0:%d", src->subtitle_tracks[i].index);
        } else {
            catf(args, cap, L" -map 0:s:%d", i);
        }
        mapped++;
    }
    if (mapped > 0) catf(args, cap, L" -c:s mov_text");
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
    wchar_t maps[4096];
    wchar_t filter[1024];
    wchar_t color[512];
    int vt, i, selected_audio, use_encoder, use_quality;

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
    if (encode_selected_audio_count(src, opt) > 0 &&
        (!encode_audio_available(opt->container, opt->audio_codec) ||
         (opt->audio_codec == ENC_ACODEC_COPY &&
          !encode_audio_copy_available_sel(src, opt, opt->container)))) {
        if (error) *error = ENCODE_ERR_AUDIO;
        return 0;
    }
    if (opt->container == ENC_CONT_MP4 &&
        opt->subtitle_mask == 0 && !opt->subtitles_none &&
        src->subtitle_tracks_stored < src->subtitle_track_count) {
        if (error) *error = ENCODE_ERR_SUBS;
        return 0;
    }

    use_encoder = (opt->encoder_override >= 0) ? opt->encoder_override : encoder;
    use_quality = opt->crf_override;
    if (use_quality > 0) {
        if (!presets_video_args_crf(src, use_encoder, opt->video_codec,
                                    use_quality, video, ENCODE_ARGS_MAX)) {
            if (error) *error = ENCODE_ERR_CODEC;
            return 0;
        }
    } else if (!presets_video_args_ex(src, use_encoder, level, opt->video_codec,
                                      video, ENCODE_ARGS_MAX)) {
        if (error) *error = ENCODE_ERR_CODEC;
        return 0;
    }

    /* Map streams by absolute index. Relying on 0:v:0 / 0:a:N is unsafe for
       files with cover art or several video angles, and it makes per-track
       selection impossible. */
    maps[0] = L'\0';
    vt = encode_selected_video_track(src, opt);
    if (src->video_tracks_stored > 0 && vt >= 0 &&
        vt < src->video_tracks_stored && src->video_tracks[vt].has_index) {
        catf(maps, 4096, L" -map 0:%d", src->video_tracks[vt].index);
    } else {
        catf(maps, 4096, L" -map 0:v:0");
    }

    selected_audio = encode_selected_audio_count(src, opt);
    if (src->audio_track_count <= 0 || selected_audio <= 0) {
        catf(maps, 4096, L" -an");
    } else if (opt->audio_mask == 0 &&
               src->audio_track_count > src->audio_tracks_stored) {
        catf(maps, 4096, L" -map 0:a?");
    } else {
        for (i = 0; i < src->audio_tracks_stored; i++) {
            if (!encode_audio_selected(src, opt, i)) continue;
            if (src->audio_tracks[i].has_index) {
                catf(maps, 4096, L" -map 0:%d", src->audio_tracks[i].index);
            } else {
                catf(maps, 4096, L" -map 0:a:%d", i);
            }
        }
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
        L"-i %ls%ls %ls%ls %ls %ls -dn "
        L"-fps_mode passthrough -map_metadata 0 -map_chapters 0 "
        L"-max_muxing_queue_size 4096%ls%ls -y %ls",
        q_tool, q_in, maps,
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

static int verify_audio_codecs(const SourceInfo *src, const EncodeOptions *opt,
                               const SourceInfo *out)
{
    int i, out_index = 0;

    if (out->audio_track_count != encode_selected_audio_count(src, opt)) return 0;
    if (out->audio_tracks_stored < out->audio_track_count) return 1;
    for (i = 0; i < src->audio_tracks_stored; i++) {
        const wchar_t *wanted;
        if (!encode_audio_selected(src, opt, i)) continue;
        if (out_index >= out->audio_tracks_stored) return 0;
        if (encode_audio_track_copies(src, opt, i)) {
            out_index++;
            continue;
        }
        wanted = audio_codec_name(opt->audio_codec);
        if (wanted && _wcsicmp(out->audio_tracks[out_index].codec, wanted) != 0) {
            return 0;
        }
        out_index++;
    }
    return 1;
}

static int verify_subtitle_codecs(const SourceInfo *src, const EncodeOptions *opt,
                                  const SourceInfo *out)
{
    int i;

    if (out->subtitle_track_count !=
        encode_selected_subtitle_count(src, opt, opt->container)) {
        return 0;
    }
    if (out->subtitle_tracks_stored < out->subtitle_track_count) return 1;
    for (i = 0; i < out->subtitle_tracks_stored; i++) {
        if (opt->container == ENC_CONT_MP4) {
            if (_wcsicmp(out->subtitle_tracks[i].codec, L"mov_text") != 0) return 0;
        } else if (_wcsicmp(out->subtitle_tracks[i].codec, L"mov_text") == 0) {
            return 0;
        }
    }
    return 1;
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

    if (out.audio_track_count != encode_selected_audio_count(src, opt)) return 0;
    expected_subs = encode_selected_subtitle_count(src, opt, opt->container);
    if (out.subtitle_track_count != expected_subs) return 0;
    if (!verify_audio_codecs(src, opt, &out)) return 0;
    if (!verify_subtitle_codecs(src, opt, &out)) return 0;
    /* FFmpeg's default autorotate normalizes display orientation, so compare
       the display size rather than the stored coded size. When the user picks
       a secondary video stream, verify against that stream instead. */
    {
        int vt = encode_selected_video_track(src, opt);
        if (src->video_tracks_stored > 0 && vt >= 0 &&
            vt < src->video_tracks_stored &&
            src->video_tracks[vt].display_width > 0 &&
            src->video_tracks[vt].display_height > 0) {
            if (out.display_width != src->video_tracks[vt].display_width ||
                out.display_height != src->video_tracks[vt].display_height) return 0;
        } else if (out.display_width != src->display_width ||
                   out.display_height != src->display_height) {
            return 0;
        }
    }

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
        if (run == UTIL_RUN_START) {
            if (error) *error = ENCODE_ERR_START;
        } else if (opt->audio_codec == ENC_ACODEC_OPUS && buf &&
                   ((strstr(buf, "Invalid channel layout") != NULL &&
                     strstr(buf, "libopus") != NULL) ||
                    (strstr(buf, "libopus") != NULL &&
                     strstr(buf, "Invalid argument") != NULL))) {
            /* Older FFmpeg builds abort libopus on 5.1(side)/7.1(wide).
               Report a useful hint instead of a generic muxer error. */
            if (error) *error = ENCODE_ERR_AUDIO_LAYOUT;
        } else {
            if (error) *error = ENCODE_ERR_FFMPEG;
        }
        free(buf);
        DeleteFileW(partial);
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
