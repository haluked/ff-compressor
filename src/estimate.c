/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "estimate.h"
#include "presets.h"
#include "util.h"

#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

/* Harmonic mean is deliberately conservative when the sampled scenes have
   very different complexity. A plain median overweights the easy scenes and
   underestimated the full-length time on high-motion 60 fps clips. */
static double harmonic_mean(const double *values, int n)
{
    double sum = 0.0;
    int i;
    if (n <= 0) return 0.0;
    for (i = 0; i < n; i++) {
        if (values[i] > 0.0) sum += 1.0 / values[i];
    }
    return (sum > 0.0) ? (double)n / sum : 0.0;
}

static double robust_speed(const double *values, int n)
{
    double minv, h;
    int i;
    if (n <= 0) return 0.0;
    minv = values[0];
    for (i = 1; i < n; i++) {
        if (values[i] < minv) minv = values[i];
    }
    h = harmonic_mean(values, n);
    /* Blend the slowest sample with the harmonic mean. The slowest sample
       catches a scene the short demo may under-represent; the harmonic mean
       avoids over-penalizing one unusually slow scene. */
    if (minv > 0.0 && h > 0.0) return sqrt(minv * h);
    return h;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double*)a;
    double y = *(const double*)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

static double median_value(double *values, int n)
{
    if (n <= 0) return 0.0;
    qsort(values, (size_t)n, sizeof(double), cmp_double);
    if (n & 1) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
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

static void cleanup_temp_dir(const wchar_t *dir);

static void make_temp_dir(wchar_t *out, size_t cap)
{
    wchar_t base[UTIL_PATH_MAX];
    util_get_temp_dir(base, UTIL_PATH_MAX);
    _snwprintf(out, cap, L"%lsff-compressor\\demo-%lu", base,
               (unsigned long)GetCurrentProcessId());
    out[cap - 1] = L'\0';
    cleanup_temp_dir(out);
    SHCreateDirectoryExW(NULL, out, NULL);
}

static void cleanup_temp_dir(const wchar_t *dir)
{
    wchar_t pattern[UTIL_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    if (!dir || !dir[0]) return;
    _snwprintf(pattern, UTIL_PATH_MAX, L"%ls\\*", dir);
    pattern[UTIL_PATH_MAX - 1] = L'\0';
    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            {
                wchar_t path[UTIL_PATH_MAX];
                _snwprintf(path, UTIL_PATH_MAX, L"%ls\\%ls", dir, fd.cFileName);
                path[UTIL_PATH_MAX - 1] = L'\0';
                DeleteFileW(path);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

int estimate_plan(const SourceInfo *src, int profile,
                  int *levels, int *level_count,
                  int *clips, double *clip_seconds)
{
    int count, clip_count;
    double clip_len;

    if (!src || !levels) return 0;
    if (profile != HW_PROFILE_LIGHT && profile != HW_PROFILE_THOROUGH) {
        profile = HW_PROFILE_BALANCED;
    }
    if (profile == HW_PROFILE_LIGHT) {
        clip_count = 2;
        clip_len = 3.0;
    } else if (profile == HW_PROFILE_THOROUGH) {
        clip_count = 4;
        clip_len = 4.0;
    } else {
        clip_count = 3;
        clip_len = 4.0;
    }
    count = presets_available_levels(profile, levels);

    if (src->duration <= 0.0) {
        clip_count = 1;
        clip_len = 1.0;
    } else if (src->duration < 4.0) {
        clip_count = 1;
        clip_len = src->duration;
    } else if (src->duration < 8.0) {
        clip_len = 2.0;
        if (clip_count > 2) clip_count = 2;
    } else if (src->duration < 12.0) {
        clip_len = 3.0;
    }
    if (clip_len > src->duration) clip_len = src->duration;
    if (clip_len < 0.5) clip_len = 0.5;

    if (level_count) *level_count = count;
    if (clips) *clips = clip_count;
    if (clip_seconds) *clip_seconds = clip_len;
    return count;
}

int estimate_choose_encoder(const SourceInfo *src, const HwInfo *hw)
{
    if (!hw) return HW_ENC_X265;
    if (src && src->is_hdr && hw->hdr_encoder >= 0 &&
        hw->enc_ok10[hw->hdr_encoder]) {
        return hw->hdr_encoder;
    }
    if (hw->recommended >= 0 && hw->enc_ok8[hw->recommended]) {
        return hw->recommended;
    }
    if (hw->best8 >= 0 && hw->enc_ok8[hw->best8]) return hw->best8;
    return HW_ENC_X265;
}

int estimate_demo_seconds(const SourceInfo *src, const HwInfo *hw, int profile_override)
{
    int levels[ESTIMATE_MAX_LEVELS];
    int level_count = 0, clips = 0;
    double clip_sec = 0.0;
    int enc;
    double speed;
    double source_seconds;
    double seconds;

    if (!src) return 5;
    estimate_plan(src, profile_override, levels, &level_count, &clips, &clip_sec);
    enc = estimate_choose_encoder(src, hw);
    speed = 0.0;
    if (hw) {
        if (src->is_hdr && hw->speed10_median[enc] > 0.0) speed = hw->speed10_median[enc];
        else speed = hw->speed8_median[enc];
    }
    if (speed <= 0.0) speed = 1.5;
    {
        double w = (double)(src->display_width > 0 ? src->display_width : 1920);
        double h = (double)(src->display_height > 0 ? src->display_height : 1080);
        double fps = src->fps > 0.0 ? src->fps : 30.0;
        double rate = (1920.0 * 1080.0 * 30.0) / (w * h * fps);
        if (rate < 0.10) rate = 0.10;
        if (rate > 1.0) rate = 1.0;
        speed *= rate;
        if (speed < 0.25) speed = 0.25;
    }
    source_seconds = (double)level_count * (double)clips * clip_sec;
    seconds = source_seconds / speed + (double)level_count * 0.4 + 1.0;
    if (seconds < 5.0) seconds = 5.0;
    return (int)(seconds + 0.5);
}

int estimate_next_encoder(int current, int failed, int *fell_back)
{
    if (!failed) return current;
    if (current == HW_ENC_X265) return -1;
    if (fell_back) *fell_back = 1;
    return HW_ENC_X265;
}

static double run_ssim(const SourceInfo *src, const wchar_t *sample_path,
                       double start, double clip_sec, volatile int *cancel)
{
    wchar_t ffmpeg[UTIL_PATH_MAX];
    wchar_t q_tool[UTIL_PATH_MAX + 4];
    wchar_t q_src[UTIL_PATH_MAX + 4];
    wchar_t q_sample[UTIL_PATH_MAX + 4];
    wchar_t cmd[UTIL_PATH_MAX * 2 + 1024];
    wchar_t pix[32];
    char *buf = NULL;
    size_t len = 0;
    DWORD code = 0;
    double result = 0.0;

    if (!util_find_tool(L"ffmpeg", ffmpeg, UTIL_PATH_MAX)) return 0.0;
    if (!util_quote_arg(q_tool, sizeof(q_tool) / sizeof(wchar_t), ffmpeg)) return 0.0;
    if (!util_quote_arg(q_src, sizeof(q_src) / sizeof(wchar_t), src->path)) return 0.0;
    if (!util_quote_arg(q_sample, sizeof(q_sample) / sizeof(wchar_t), sample_path)) return 0.0;
    wcscpy(pix, src->bit_depth >= 10 ? L"yuv420p10le" : L"yuv420p");

    _snwprintf(cmd, sizeof(cmd) / sizeof(wchar_t),
        L"%ls -hide_banner -loglevel info -nostats -ss %.3f -i %ls -i %ls -t %.3f "
        L"-lavfi \"[0:v]setpts=PTS-STARTPTS,format=%ls[a];"
        L"[1:v]setpts=PTS-STARTPTS,format=%ls[b];[a][b]ssim\" -f null NUL",
        q_tool, start, q_src, q_sample, clip_sec, pix, pix);
    cmd[(sizeof(cmd) / sizeof(wchar_t)) - 1] = L'\0';

    if (util_run_capture(cmd, 180000, cancel, &buf, &len, &code) == UTIL_RUN_OK && code == 0) {
        char *p = strstr(buf, "All:");
        if (p) {
            double v = strtod(p + 4, NULL);
            if (v > 0.0 && v <= 1.0) result = v;
        }
    }
    free(buf);
    return result;
}

static int extract_preview_frame(const SourceInfo *src, const wchar_t *input_path,
                                 double seek, const wchar_t *bmp_path,
                                 volatile int *cancel)
{
    wchar_t ffmpeg[UTIL_PATH_MAX];
    wchar_t q_tool[UTIL_PATH_MAX + 4];
    wchar_t q_in[UTIL_PATH_MAX + 4];
    wchar_t q_out[UTIL_PATH_MAX + 4];
    wchar_t cmd[UTIL_PATH_MAX * 2 + 1024];
    wchar_t filter[768];
    char *buf = NULL;
    size_t len = 0;
    DWORD code = 0;
    UtilRunResult rr;

    if (!util_find_tool(L"ffmpeg", ffmpeg, UTIL_PATH_MAX)) return 0;
    if (!util_quote_arg(q_tool, sizeof(q_tool) / sizeof(wchar_t), ffmpeg)) return 0;
    if (!util_quote_arg(q_in, sizeof(q_in) / sizeof(wchar_t), input_path)) return 0;
    if (!util_quote_arg(q_out, sizeof(q_out) / sizeof(wchar_t), bmp_path)) return 0;

    if (src->is_hdr) {
        wcscpy(filter,
            L"zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,"
            L"tonemap=tonemap=hable:desat=0,zscale=t=bt709:m=bt709:r=tv,"
            L"format=bgr24,scale=w=640:h=-2:flags=lanczos");
    } else {
        wcscpy(filter, L"scale=w=640:h=-2:flags=lanczos,format=bgr24");
    }

    DeleteFileW(bmp_path);
    _snwprintf(cmd, sizeof(cmd) / sizeof(wchar_t),
        L"%ls -hide_banner -loglevel error -nostats -ss %.3f -i %ls "
        L"-frames:v 1 -vf \"%ls\" -f image2 -c:v bmp -y %ls",
        q_tool, seek, q_in, filter, q_out);
    cmd[(sizeof(cmd) / sizeof(wchar_t)) - 1] = L'\0';

    rr = util_run_capture(cmd, 30000, cancel, &buf, &len, &code);
    free(buf);
    if (rr == UTIL_RUN_CANCELLED) return -2;
    if (rr == UTIL_RUN_OK && code == 0 && util_file_exists(bmp_path)) return 1;

    if (src->is_hdr) {
        /* Fallback for a build without zscale/tonemap: keep a usable preview
           even if the colours are approximate. */
        wcscpy(filter, L"scale=w=640:h=-2:flags=lanczos,format=bgr24");
        DeleteFileW(bmp_path);
        _snwprintf(cmd, sizeof(cmd) / sizeof(wchar_t),
            L"%ls -hide_banner -loglevel error -nostats -ss %.3f -i %ls "
            L"-frames:v 1 -vf \"%ls\" -f image2 -c:v bmp -y %ls",
            q_tool, seek, q_in, filter, q_out);
        cmd[(sizeof(cmd) / sizeof(wchar_t)) - 1] = L'\0';
        rr = util_run_capture(cmd, 30000, cancel, &buf, &len, &code);
        free(buf);
        if (rr == UTIL_RUN_CANCELLED) return -2;
        if (rr == UTIL_RUN_OK && code == 0 && util_file_exists(bmp_path)) return 1;
    }
    return 0;
}

static int run_and_measure(const SourceInfo *src, int encoder, int level,
                           double start, double clip_sec, const wchar_t *path,
                           int keep_file, volatile int *cancel, double *speed_out,
                           double *bytes_out, double *wall_out)
{
    wchar_t cmd[UTIL_PATH_MAX * 2 + 1024];
    char *buf = NULL;
    size_t len = 0;
    DWORD code = 0;
    UtilRunResult rr;
    ULONGLONG t0, t1;
    long long size = 0;
    double speed, wall;

    if (!presets_build_sample_cmd(src, encoder, level, start, clip_sec, path,
                                  cmd, sizeof(cmd) / sizeof(wchar_t))) {
        return -1;
    }
    DeleteFileW(path);
    t0 = GetTickCount64();
    rr = util_run_capture(cmd, 180000, cancel, &buf, &len, &code);
    t1 = GetTickCount64();
    wall = (double)(t1 - t0) / 1000.0;

    if (rr == UTIL_RUN_CANCELLED) {
        free(buf);
        DeleteFileW(path);
        return -2;
    }
    if (rr != UTIL_RUN_OK || code != 0) {
        free(buf);
        DeleteFileW(path);
        return -1;
    }
    speed = parse_speed(buf);
    free(buf);

    if (!util_get_file_size(path, &size) || size <= 0) {
        DeleteFileW(path);
        return -1;
    }
    if (speed <= 0.0 && wall > 0.0) speed = clip_sec / wall;

    if (!keep_file) DeleteFileW(path);
    if (speed_out) *speed_out = speed;
    if (bytes_out) *bytes_out = (double)size;
    if (wall_out) *wall_out = wall;
    return 0;
}

static void sample_positions(double duration, int clips, double clip_sec, double *pos)
{
    static const double f4[4] = { 0.10, 0.35, 0.60, 0.85 };
    static const double f3[3] = { 0.10, 0.45, 0.85 };
    static const double f2[2] = { 0.15, 0.80 };
    const double *f = f3;
    double margin = duration * 0.05;
    double avail;
    int i;

    if (margin < 0.5) margin = 0.5;
    avail = duration - clip_sec - 2.0 * margin;
    if (avail < 0.0) {
        margin = 0.0;
        avail = duration - clip_sec;
    }
    if (avail < 0.0) {
        avail = 0.0;
        clip_sec = duration;
    }
    if (clips >= 4) f = f4;
    else if (clips == 3) f = f3;
    else if (clips == 2) f = f2;

    for (i = 0; i < clips; i++) {
        double frac = (clips == 1) ? 0.0 : f[i];
        pos[i] = margin + avail * frac;
        if (pos[i] < 0.0) pos[i] = 0.0;
        if (pos[i] + clip_sec > duration) pos[i] = duration - clip_sec;
        if (pos[i] < 0.0) pos[i] = 0.0;
    }
}

EstimateResult estimate_run(const SourceInfo *src, const HwInfo *hw,
                            int profile_override, EstimateSet *out,
                            EstimateProgressCallback cb, void *user,
                            volatile int *cancel)
{
    int levels[ESTIMATE_MAX_LEVELS];
    int level_count = 0, clips = 0;
    double clip_sec = 0.0;
    double positions[4] = { 0, 0, 0, 0 };
    wchar_t tempdir[UTIL_PATH_MAX];
    wchar_t sample_path[UTIL_PATH_MAX];
    ULONGLONG t0;
    int encoder;
    int li, ci;
    int profile;
    double startup_wall = 0.0;

    if (!src || !out) return ESTIMATE_ERROR;
    memset(out, 0, sizeof(*out));
    profile = (profile_override != HW_PROFILE_AUTO)
        ? profile_override
        : (hw ? hw->profile : HW_PROFILE_BALANCED);
    out->profile = profile;
    estimate_plan(src, profile, levels, &level_count, &clips, &clip_sec);
    out->level_count = level_count;
    out->clips = clips;
    out->clip_seconds = clip_sec;
    for (li = 0; li < level_count; li++) out->levels_used[li] = levels[li];

    encoder = estimate_choose_encoder(src, hw);
    out->encoder = encoder;
    make_temp_dir(tempdir, UTIL_PATH_MAX);
    sample_positions(src->duration, clips, clip_sec, positions);
    out->preview_time = positions[0] + clip_sec / 2.0;
    t0 = GetTickCount64();

    /* Measure process/encoder startup with a very short encode. Subtracting
       this from each sample's wall time gives a sustained-speed estimate that
       is much closer to a full-length encode. */
    {
        wchar_t startup_path[UTIL_PATH_MAX];
        double s_speed = 0.0, s_bytes = 0.0, s_wall = 0.0;
        int s_rc;
        _snwprintf(startup_path, UTIL_PATH_MAX, L"%ls\\startup.mkv", tempdir);
        startup_path[UTIL_PATH_MAX - 1] = L'\0';
        s_rc = run_and_measure(src, encoder, levels[0], positions[0], 0.1,
                               startup_path, 0, cancel, &s_speed, &s_bytes, &s_wall);
        if (s_rc == -2) {
            cleanup_temp_dir(tempdir);
            return ESTIMATE_CANCELLED;
        }
        if (s_rc == 0) startup_wall = s_wall;
        else startup_wall = 0.4;
    }

    for (li = 0; li < level_count; li++) {
        double bytes_sum = 0.0;
        double speeds[4];
        double ssims[4];
        int speed_count = 0;
        int ssim_count = 0;
        double videos_sec = 0.0;
        int level_id = levels[li];
        int ok = 1;

        for (ci = 0; ci < clips; ci++) {
            double speed = 0.0, bytes = 0.0, wall = 0.0;
            int rc;
            EstimateProgress p;

            _snwprintf(sample_path, UTIL_PATH_MAX, L"%ls\\demo_%d_%d.mkv",
                       tempdir, li, ci);
            sample_path[UTIL_PATH_MAX - 1] = L'\0';

            rc = run_and_measure(src, encoder, level_id, positions[ci], clip_sec,
                                 sample_path, 1, cancel, &speed, &bytes, &wall);
            if (rc == -2) {
                cleanup_temp_dir(tempdir);
                return ESTIMATE_CANCELLED;
            }
            if (rc != 0) {
                int next = estimate_next_encoder(encoder, 1, &out->used_fallback);
                if (next < 0) {
                    cleanup_temp_dir(tempdir);
                    return ESTIMATE_ERROR;
                }
                encoder = next;
                out->encoder = encoder;
                rc = run_and_measure(src, encoder, level_id, positions[ci], clip_sec,
                                     sample_path, 1, cancel, &speed, &bytes, &wall);
                if (rc == -2) {
                    cleanup_temp_dir(tempdir);
                    return ESTIMATE_CANCELLED;
                }
                if (rc != 0) {
                    cleanup_temp_dir(tempdir);
                    return ESTIMATE_ERROR;
                }
            }

            {
                double ssim = run_ssim(src, sample_path, positions[ci], clip_sec, cancel);
                if (cancel && *cancel) {
                    DeleteFileW(sample_path);
                    cleanup_temp_dir(tempdir);
                    return ESTIMATE_CANCELLED;
                }
                if (ssim > 0.0 && ssim_count < 4) ssims[ssim_count++] = ssim;
            }

            if (ci == 0) {
                wchar_t orig_bmp[UTIL_PATH_MAX];
                wchar_t enc_bmp[UTIL_PATH_MAX];
                int r1, r2;
                _snwprintf(orig_bmp, UTIL_PATH_MAX, L"%ls\\prev_%d_orig.bmp", tempdir, li);
                _snwprintf(enc_bmp, UTIL_PATH_MAX, L"%ls\\prev_%d_enc.bmp", tempdir, li);
                orig_bmp[UTIL_PATH_MAX - 1] = L'\0';
                enc_bmp[UTIL_PATH_MAX - 1] = L'\0';
                r1 = extract_preview_frame(src, src->path,
                                           positions[0] + clip_sec / 2.0,
                                           orig_bmp, cancel);
                r2 = extract_preview_frame(src, sample_path, clip_sec / 2.0,
                                           enc_bmp, cancel);
                if (r1 == -2 || r2 == -2) {
                    DeleteFileW(sample_path);
                    cleanup_temp_dir(tempdir);
                    return ESTIMATE_CANCELLED;
                }
                if (r1 == 1 && r2 == 1) out->preview_available[li] = 1;
            }

            DeleteFileW(sample_path);
            bytes_sum += bytes;
            videos_sec += clip_sec;
            {
                double effective = 0.0;
                if (wall > startup_wall + 0.02) {
                    effective = clip_sec / (wall - startup_wall);
                }
                if (effective <= 0.0 || effective > 1000.0) effective = speed;
                if (effective > 0.0 && speed_count < 4) speeds[speed_count++] = effective;
                speed = effective;
            }

            memset(&p, 0, sizeof(p));
            p.level = level_id;
            p.level_index = li;
            p.level_total = level_count;
            p.clip_index = ci + 1;
            p.clip_total = clips;
            p.speed = speed;
            p.elapsed_seconds = (double)(GetTickCount64() - t0) / 1000.0;
            if (cb) cb(&p, user);
        }

        if (!ok || videos_sec <= 0.0) {
            cleanup_temp_dir(tempdir);
            return ESTIMATE_ERROR;
        }
        {
            double video_bps = (bytes_sum * 8.0) / videos_sec;
            double audio_bps = 0.0;
            double speed;
            double est_size, est_time;
            EstimateLevel *e = &out->levels[li];

            if (src->audio_track_count > 0) {
                audio_bps = (src->audio_bitrate > 0) ? (double)src->audio_bitrate : 128000.0;
            }
            speed = (speed_count > 0) ? robust_speed(speeds, speed_count) : 0.0;
            if (speed <= 0.0 && videos_sec > 0.0) {
                speed = videos_sec / ((double)(GetTickCount64() - t0) / 1000.0);
            }
            if (speed <= 0.0) speed = 1.0;
            est_size = (video_bps + audio_bps) * src->duration / 8.0 * 1.01;
            est_time = src->duration / speed + 0.9;
            if (est_time < 0.9) est_time = 0.9;

            e->level = level_id;
            e->available = 1;
            e->est_size_bytes = est_size;
            e->est_size_min = est_size * 0.85;
            e->est_size_max = est_size * 1.15;
            e->est_seconds = est_time;
            e->est_seconds_min = est_time * 0.70;
            e->est_seconds_max = est_time * 1.30;
            e->sample_video_bps = video_bps;
            e->sample_speed = speed;
            e->ssim = (ssim_count > 0) ? median_value(ssims, ssim_count) : 0.0;
            e->ssim_available = (ssim_count > 0);
            e->clips = clips;
            e->clip_seconds = clip_sec;
        }
    }

    out->complete = 1;
    out->total_seconds = (double)(GetTickCount64() - t0) / 1000.0;
    {
        int any = 0;
        for (li = 0; li < level_count; li++) {
            if (out->preview_available[li]) { any = 1; break; }
        }
        if (any) {
            wcsncpy(out->preview_dir, tempdir, UTIL_PATH_MAX - 1);
            out->preview_dir[UTIL_PATH_MAX - 1] = L'\0';
        } else {
            cleanup_temp_dir(tempdir);
        }
    }
    return ESTIMATE_OK;
}

void estimate_cleanup_preview_dir(EstimateSet *set)
{
    if (!set) return;
    if (set->preview_dir[0]) {
        cleanup_temp_dir(set->preview_dir);
        set->preview_dir[0] = L'\0';
    }
    memset(set->preview_available, 0, sizeof(set->preview_available));
}
