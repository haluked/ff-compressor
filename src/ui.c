/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui.h"
#include "engine.h"
#include "hw.h"
#include "probe.h"
#include "presets.h"
#include "estimate.h"
#include "encode.h"
#include "strings.h"
#include "util.h"
#include "gb_ui.h"

#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#define IDC_BTN_CHOOSE   3001
#define IDC_BTN_CONTINUE 3002
#define IDC_BTN_DETAILS  3003
#define IDC_BTN_MIN      3004
#define IDC_BTN_CLOSE    3005
#define IDC_BTN_RETEST   3006
#define IDC_BTN_DEPTH    3007
#define IDC_BTN_SKIP     3008
#define IDC_BTN_BACK     3009
#define IDC_BTN_TESTAGAIN 3010
#define IDC_BTN_QCANCEL  3011
#define IDC_BTN_COMPRESS 3012
#define IDC_BTN_PREVIEW  3013
#define IDC_BTN_CONT_MKV 3014
#define IDC_BTN_CONT_MP4 3015
#define IDC_BTN_VC_HEVC 3016
#define IDC_BTN_VC_H264 3017
#define IDC_BTN_AUDIO   3018
#define IDC_BTN_ABITRATE 3019
#define IDC_BTN_DOWNMIX 3020
#define IDC_BTN_START   3021
#define IDC_BTN_ECANCEL 3022
#define IDC_BTN_SHOW    3023
#define IDC_BTN_INPUT_OK 3024
#define IDC_BTN_INPUT_CANCEL 3025
#define IDC_EDIT_CUSTOM 3026
#define IDC_BTN_VIDEO_TRACK 3027
#define IDC_BTN_AUDIO_TRACK 3028
#define IDC_BTN_ADVANCED 3029
#define IDC_BTN_ADV_ENC 3030
#define IDC_BTN_ADV_Q 3031
#define IDC_EDIT_CRF 3032
#define IDC_BTN_ADV_OK 3033
#define IDC_BTN_ADV_CANCEL 3034
#define IDC_BTN_SUBTITLE_TRACK 3035

#define BASE_CLIENT_W 646
#define BASE_CLIENT_H 485

#define APP_WINDOW_STYLE    (WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU)
#define APP_WINDOW_EX_STYLE (WS_EX_APPWINDOW | WS_EX_CONTROLPARENT)

#define GB_MAX_TRACKED 48
typedef struct {
    HWND hwnd;
    WNDPROC old_proc;
    int hover;
} TrackedButton;

static HWND g_hWnd = NULL;
static HWND g_hBtnChoose = NULL;
static HWND g_hBtnContinue = NULL;
static HWND g_hBtnDetails = NULL;
static HWND g_hBtnMin = NULL;
static HWND g_hBtnClose = NULL;
static HWND g_hBtnRetest = NULL;
static HWND g_hBtnDepth = NULL;
static HWND g_hBtnSkip = NULL;
static HWND g_hBtnBack = NULL;
static HWND g_hBtnTestAgain = NULL;
static HWND g_hBtnQCancel = NULL;
static HWND g_hBtnCompress = NULL;
static HWND g_hBtnPreview = NULL;
static HWND g_hBtnContMkv = NULL;
static HWND g_hBtnContMp4 = NULL;
static HWND g_hBtnVcHevc = NULL;
static HWND g_hBtnVcH264 = NULL;
static HWND g_hBtnAudio = NULL;
static HWND g_hBtnABitrate = NULL;
static HWND g_hBtnDownmix = NULL;
static HWND g_hBtnVideoTrack = NULL;
static HWND g_hBtnAudioTrack = NULL;
static HWND g_hBtnAdvanced = NULL;
static HWND g_hBtnAdvEnc = NULL;
static HWND g_hBtnAdvQ = NULL;
static HWND g_hEditCrf = NULL;
static HWND g_hBtnAdvOk = NULL;
static HWND g_hBtnAdvCancel = NULL;
static HWND g_hBtnSubtitleTrack = NULL;
static HWND g_hBtnStart = NULL;
static HWND g_hBtnECancel = NULL;
static HWND g_hBtnShow = NULL;
static HWND g_hBtnInputOk = NULL;
static HWND g_hBtnInputCancel = NULL;
static HWND g_hEditCustom = NULL;
static HBRUSH g_brushLightest = NULL;
static WNDPROC g_edit_old_proc = NULL;

static HFONT g_fontTitle = NULL;
static HFONT g_fontBold = NULL;
static HFONT g_fontNormal = NULL;
static HFONT g_fontSmall = NULL;
static HFONT g_fontSmallBold = NULL;

static UINT g_dpi = 96;
static int g_total_w = 0;
static int g_total_h = 0;

static TrackedButton g_tracked[GB_MAX_TRACKED];
static int g_tracked_count = 0;

static SourceInfo g_info;
static int g_has_info = 0;
static int g_probing = 0;
static int g_show_details = 0;
static int g_status_error = 0;
static wchar_t g_status[512];
static wchar_t g_pending_name[256];

static RECT g_rc_panel;
static RECT g_rc_drop;
static RECT g_rc_status;

static HwInfo g_hw_info;
static int g_hw_has_info = 0;
static int g_hw_testing = 0;
static int g_hw_skipped = 0;
static int g_hw_override = HW_PROFILE_AUTO;
static HwProgress g_hw_progress;
static HwError g_hw_error = HW_ERR_NONE;
static ULONGLONG g_hw_start_tick = 0;
static int g_hw_est_seconds = 30;
static volatile int g_hw_cancel = 0;
static wchar_t g_hw_pending_path[UTIL_PATH_MAX];
static wchar_t g_depth_items[4][32];

static int g_step = 0; /* 0 = video, 1 = quality */
static int g_quality_selected = QUALITY_BALANCED;
static int g_quality_testing = 0;
static int g_quality_has_result = 0;
static int g_quality_gen = 0;
static EstimateSet g_est;
static EstimateProgress g_est_progress;
static int g_est_seconds = 10;
static double g_quality_last_seconds = 0.0;
static ULONGLONG g_est_start_tick = 0;
static volatile int g_est_cancel = 0;
static RECT g_rc_cards[ESTIMATE_MAX_LEVELS];
static int g_card_count = 0;
static int g_quality_preview = 0;
static HBITMAP g_preview_orig_bmp = NULL;
static HBITMAP g_preview_enc_bmp = NULL;
static RECT g_rc_preview_chips[ESTIMATE_MAX_LEVELS];
static int g_preview_chip_count = 0;

static EncodeOptions g_enc_opt;
static int g_encode_state = 0; /* 0 settings, 1 running, 2 done, 3 error */
static int g_enc_options_ready = 0;
static int g_encode_gen = 0;
static EncodeProgress g_encode_progress;
static ULONGLONG g_encode_start_tick = 0;
static volatile int g_encode_cancel = 0;
static wchar_t g_encode_output[UTIL_PATH_MAX];
static wchar_t g_encode_detail[512];
static EncodeError g_encode_error_code = ENCODE_ERR_NONE;
static long long g_encode_size = 0;
static int g_input_active = 0;
static wchar_t g_audio_items[ENC_ACODEC_COUNT][48];
static wchar_t g_bitrate_items[4][48];
static wchar_t g_track_items[PROBE_MAX_AUDIO_TRACKS + 2][160];
static wchar_t g_video_items[PROBE_MAX_VIDEO_TRACKS][128];
static int g_video_item_map[PROBE_MAX_VIDEO_TRACKS];
static wchar_t g_sub_items[PROBE_MAX_SUBTITLE_TRACKS + 2][160];
static int g_advanced_active = 0;
static int g_adv_encoder = -1;
static int g_adv_quality = 0; /* 0 preset, 1..4 level, 5 archive, 6 custom */
static int g_adv_crf = 19;
static wchar_t g_adv_enc_items[HW_ENC_COUNT + 1][48];
static int g_adv_enc_map[HW_ENC_COUNT + 1];
static int g_adv_enc_count = 0;
static wchar_t g_adv_q_items[7][64];
static int g_quality_show_details = 0;
static int g_quality_deep = 0;
static int g_quality_encoder = -1;
static wchar_t g_quality_enc_items[HW_ENC_COUNT + 1][64];
static int g_quality_enc_map[HW_ENC_COUNT + 1];
static int g_quality_enc_count = 0;
static RECT g_rc_q_encoder;
static int g_custom_est_testing = 0;
static int g_custom_est_ready = 0;
static int g_custom_est_gen = 0;
static volatile int g_custom_est_cancel = 0;
static EstimateLevel g_custom_est;
static EstimateProgress g_custom_est_progress;
static RECT g_rc_q_details;
static RECT g_rc_q_custom;
static RECT g_rc_q_deep;

typedef struct {
    ProbeError error;
    SourceInfo info;
} ProbeDoneMsg;

typedef struct {
    HwTestResult result;
    HwInfo info;
} HwDoneMsg;

typedef struct {
    int gen;
    EstimateResult result;
    EstimateSet est;
} EstimateDoneMsg;

typedef struct {
    int gen;
    EstimateProgress progress;
} EstProgressMsg;

typedef struct {
    int gen;
    EncodeProgress progress;
} EncodeProgressMsg;

typedef struct {
    int gen;
    EstimateProgress progress;
} CustomEstProgressMsg;

typedef struct {
    int gen;
    EstimateResult result;
    EstimateLevel est;
    int encoder;
    int used_fallback;
} CustomEstDoneMsg;

typedef struct {
    int gen;
    EncodeResult result;
    wchar_t path[UTIL_PATH_MAX];
    long long size;
    EncodeError error_code;
    wchar_t detail[512];
} EncodeDoneMsg;

static void layout_controls(void);
static void update_controls_visibility(void);
static void start_probe(HWND hWnd, const wchar_t *path);
static void set_status_text(const wchar_t *s, int is_error);
static void hw_ui_start(HWND hWnd, int auto_test, const wchar_t *pending_path);
static void hw_ui_update_depth_label(void);
static void hw_ui_open_depth_dropdown(void);
static void quality_ui_start(HWND hWnd, int force);
static void quality_ui_start_deep(HWND hWnd);
static void quality_ui_update_selection(void);
static void quality_ui_reset(void);
static int quality_current_profile(void);
static EstimateLevel *quality_level_result(int level);
static int quality_level_index(int level);
static void quality_ui_open_preview(HWND hWnd);
static void quality_clear_preview_bitmaps(void);
static void quality_cleanup_previews(void);
static void quality_load_preview(void);
static const wchar_t *quality_ssim_label(double ssim);

static void encode_ui_enter(HWND hWnd);
static void encode_ui_reset(void);
static void encode_ui_update_controls(void);
static void encode_ui_start(HWND hWnd);
static void encode_ui_cancel(void);
static void encode_ui_back(HWND hWnd);
static void encode_ui_open_custom(HWND hWnd);
static void encode_ui_close_custom(void);
static void encode_ui_accept_custom(HWND hWnd);
static void encode_ui_open_audio_dropdown(HWND hWnd);
static void encode_ui_open_bitrate_dropdown(HWND hWnd);
static void encode_ui_open_audio_track_dropdown(HWND hWnd);
static void encode_ui_open_video_track_dropdown(HWND hWnd);
static void encode_ui_update_track_labels(void);
static void encode_ui_open_advanced(HWND hWnd);
static void encode_ui_close_advanced(void);
static void encode_ui_accept_advanced(HWND hWnd);
static void encode_ui_start_custom_test(HWND hWnd);
static void encode_ui_restart_custom_test(HWND hWnd);
static void encode_ui_open_subtitle_track_dropdown(HWND hWnd);
static void encode_ui_update_adv_labels(void);
static void encode_ui_refresh_subtitle_button(void);
static void encode_ui_on_custom_est_done(HWND hWnd, CustomEstDoneMsg *msg);
static void encode_ui_on_done(HWND hWnd, EncodeDoneMsg *msg);
static const wchar_t *encode_error_text(EncodeError err);
static const wchar_t *encode_video_encoder_name(void);

static int S(int value)
{
    return MulDiv(value, (int)g_dpi, 96);
}

/* ------------------------------------------------------------------ hover */
static TrackedButton *tracked_find(HWND hwnd)
{
    int i;
    for (i = 0; i < g_tracked_count; i++) {
        if (g_tracked[i].hwnd == hwnd) return &g_tracked[i];
    }
    return NULL;
}

static LRESULT CALLBACK tracked_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TrackedButton *t = tracked_find(hwnd);
    WNDPROC old = t ? t->old_proc : NULL;

    if (msg == WM_MOUSEMOVE && t) {
        if (!t->hover) {
            t->hover = 1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
    } else if (msg == WM_MOUSELEAVE && t) {
        if (t->hover) {
            t->hover = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }

    return old ? CallWindowProcW(old, hwnd, msg, wParam, lParam)
               : DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK custom_edit_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            SendMessageW(GetParent(hwnd), WM_COMMAND,
                         MAKEWPARAM(IDC_BTN_INPUT_OK, BN_CLICKED), 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            SendMessageW(GetParent(hwnd), WM_COMMAND,
                         MAKEWPARAM(IDC_BTN_INPUT_CANCEL, BN_CLICKED), 0);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE)) return 0;
    return g_edit_old_proc
        ? CallWindowProcW(g_edit_old_proc, hwnd, msg, wParam, lParam)
        : DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void track_button(HWND hwnd)
{
    TrackedButton *t;
    if (!hwnd || g_tracked_count >= GB_MAX_TRACKED) return;
    t = &g_tracked[g_tracked_count++];
    t->hwnd = hwnd;
    t->hover = 0;
    t->old_proc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)tracked_proc);
}

static int tracked_hover(HWND hwnd)
{
    TrackedButton *t = tracked_find(hwnd);
    return t ? t->hover : 0;
}

/* -------------------------------------------------------------------- DPI */
static UINT query_window_dpi(HWND hWnd)
{
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn fn = NULL;
    static int loaded = 0;

    if (!loaded) {
        fn = (GetDpiForWindowFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        loaded = 1;
    }
    if (fn) {
        UINT dpi = fn(hWnd);
        if (dpi > 0) return dpi;
    }
    {
        HDC dc = GetDC(NULL);
        UINT dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(NULL, dc);
        return dpi ? dpi : 96;
    }
}

static void adjust_window_rect_for_dpi(RECT *rc, DWORD style, BOOL menu, DWORD ex_style, UINT dpi)
{
    typedef BOOL (WINAPI *AdjustForDpiFn)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static AdjustForDpiFn fn = NULL;
    static int loaded = 0;

    if (!loaded) {
        fn = (AdjustForDpiFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
        loaded = 1;
    }
    if (fn) fn(rc, style, menu, ex_style, dpi);
    else AdjustWindowRectEx(rc, style, menu, ex_style);
}

static void update_fixed_metrics(void)
{
    RECT rc = { 0, 0, S(BASE_CLIENT_W), S(BASE_CLIENT_H) };
    adjust_window_rect_for_dpi(&rc, APP_WINDOW_STYLE, FALSE, APP_WINDOW_EX_STYLE, g_dpi);
    g_total_w = rc.right - rc.left;
    g_total_h = rc.bottom - rc.top;
}

/* ------------------------------------------------------------------ fonts */
static void destroy_fonts(void)
{
    if (g_fontTitle) { DeleteObject(g_fontTitle); g_fontTitle = NULL; }
    if (g_fontBold) { DeleteObject(g_fontBold); g_fontBold = NULL; }
    if (g_fontNormal) { DeleteObject(g_fontNormal); g_fontNormal = NULL; }
    if (g_fontSmall) { DeleteObject(g_fontSmall); g_fontSmall = NULL; }
    if (g_fontSmallBold) { DeleteObject(g_fontSmallBold); g_fontSmallBold = NULL; }
}

static void create_fonts(void)
{
    destroy_fonts();
    g_fontTitle = CreateFontW(-S(20), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
    g_fontBold = CreateFontW(-S(14), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
    g_fontNormal = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
    g_fontSmall = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
    g_fontSmallBold = CreateFontW(-S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
}

/* ------------------------------------------------------------------- text */
static void set_status_text(const wchar_t *s, int is_error)
{
    g_status_error = is_error ? 1 : 0;
    if (!s) s = L"";
    wcsncpy(g_status, s, 511);
    g_status[511] = L'\0';
    if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, FALSE);
}

static void set_status_id(StringId id, int is_error)
{
    set_status_text(text(id), is_error);
}

static const wchar_t *video_codec_label(const wchar_t *c)
{
    if (!c || !*c) return text(STR_CODEC_UNKNOWN);
    if (_wcsicmp(c, L"h264") == 0 || _wcsicmp(c, L"avc") == 0) return text(STR_CODEC_H264);
    if (_wcsicmp(c, L"hevc") == 0 || _wcsicmp(c, L"h265") == 0) return text(STR_CODEC_HEVC);
    if (_wcsicmp(c, L"av1") == 0) return text(STR_CODEC_AV1);
    if (_wcsicmp(c, L"vp9") == 0) return text(STR_CODEC_VP9);
    if (_wcsicmp(c, L"vp8") == 0) return text(STR_CODEC_VP8);
    if (_wcsicmp(c, L"mpeg4") == 0) return text(STR_CODEC_MPEG4);
    if (_wcsicmp(c, L"mpeg2video") == 0 || _wcsicmp(c, L"mpeg2") == 0) return text(STR_CODEC_MPEG2);
    if (_wcsicmp(c, L"prores") == 0) return text(STR_CODEC_PRORES);
    if (_wcsicmp(c, L"theora") == 0) return text(STR_CODEC_THEORA);
    if (_wcsicmp(c, L"wmv1") == 0 || _wcsicmp(c, L"wmv2") == 0 || _wcsicmp(c, L"wmv3") == 0) return text(STR_CODEC_WMV);
    if (_wcsicmp(c, L"vc1") == 0) return text(STR_CODEC_VC1);
    return c;
}

static const wchar_t *audio_codec_label(const wchar_t *c)
{
    if (!c || !*c) return text(STR_CODEC_UNKNOWN);
    if (_wcsicmp(c, L"aac") == 0) return text(STR_CODEC_AAC);
    if (_wcsicmp(c, L"opus") == 0) return text(STR_CODEC_OPUS);
    if (_wcsicmp(c, L"mp3") == 0) return text(STR_CODEC_MP3);
    if (_wcsicmp(c, L"flac") == 0) return text(STR_CODEC_FLAC);
    if (_wcsicmp(c, L"ac3") == 0) return text(STR_CODEC_AC3);
    if (_wcsicmp(c, L"eac3") == 0) return text(STR_CODEC_EAC3);
    if (_wcsicmp(c, L"vorbis") == 0) return text(STR_CODEC_VORBIS);
    if (_wcsicmp(c, L"pcm_s16le") == 0 || _wcsicmp(c, L"pcm_s24le") == 0 ||
        _wcsicmp(c, L"pcm_s32le") == 0) return text(STR_CODEC_PCM);
    if (_wcsicmp(c, L"dts") == 0) return text(STR_CODEC_DTS);
    if (_wcsicmp(c, L"truehd") == 0) return text(STR_CODEC_TRUEHD);
    return c;
}

static void format_track_channels(int channels, const wchar_t *layout,
                                  wchar_t *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (layout && *layout && _wcsicmp(layout, L"unknown") != 0) {
        wcsncpy(out, layout, cap - 1);
        out[cap - 1] = L'\0';
        return;
    }
    if (channels == 1) {
        wcsncpy(out, text(STR_COMP_TRACK_MONO), cap - 1);
    } else if (channels == 2) {
        wcsncpy(out, text(STR_COMP_TRACK_STEREO), cap - 1);
    } else if (channels > 0) {
        _snwprintf(out, cap, text(STR_COMP_TRACK_CHANNELS_FMT), channels);
    }
    out[cap - 1] = L'\0';
}

static void format_audio_track_item(int index, wchar_t *out, size_t cap)
{
    const AudioTrackInfo *at;
    wchar_t name[64];
    wchar_t channels[48];
    wchar_t rate[32];
    const wchar_t *source;

    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (index < 0 || index >= g_info.audio_tracks_stored) return;
    at = &g_info.audio_tracks[index];

    source = at->title[0] ? at->title : (at->language[0] ? at->language
                                                        : text(STR_COMP_TRACK_NO_LANG));
    if (at->title[0] && at->language[0]) {
        _snwprintf(name, 64, L"%ls - %ls", at->language, at->title);
    } else {
        wcsncpy(name, source, 63);
    }
    name[63] = L'\0';
    format_track_channels(at->channels, at->layout, channels, 48);
    rate[0] = L'\0';
    if (at->bitrate > 0) {
        util_format_bitrate(at->bitrate, rate, 32);
    } else if (at->sample_rate > 0) {
        _snwprintf(rate, 32, L"%d Hz", at->sample_rate);
        rate[31] = L'\0';
    }
    _snwprintf(out, cap, text(STR_COMP_TRACK_ITEM_FMT),
               at->has_index ? at->index : (index + 1),
               name, audio_codec_label(at->codec), channels, rate);
    out[cap - 1] = L'\0';
}

static void format_video_track_item(int ordinal, int stored, wchar_t *out, size_t cap)
{
    const VideoTrackInfo *vt;
    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (stored < 0 || stored >= g_info.video_tracks_stored) return;
    vt = &g_info.video_tracks[stored];
    _snwprintf(out, cap, text(STR_COMP_VIDEO_ITEM_FMT),
               ordinal + 1, video_codec_label(vt->codec), vt->width, vt->height);
    out[cap - 1] = L'\0';
}

static const wchar_t *subtitle_codec_label(const wchar_t *c)
{
    if (!c || !*c) return text(STR_CODEC_UNKNOWN);
    if (_wcsicmp(c, L"subrip") == 0 || _wcsicmp(c, L"srt") == 0) return L"SRT";
    if (_wcsicmp(c, L"ass") == 0 || _wcsicmp(c, L"ssa") == 0) return L"ASS";
    if (_wcsicmp(c, L"hdmv_pgs_subtitle") == 0 || _wcsicmp(c, L"pgssub") == 0) return L"PGS";
    if (_wcsicmp(c, L"dvd_subtitle") == 0 || _wcsicmp(c, L"dvdsub") == 0) return L"VobSub";
    if (_wcsicmp(c, L"mov_text") == 0) return L"MOV text";
    if (_wcsicmp(c, L"webvtt") == 0) return L"WebVTT";
    return c;
}

static int wide_contains_ci(const wchar_t *haystack, const wchar_t *needle)
{
    size_t nlen;
    if (!haystack || !needle || !*needle) return 0;
    nlen = wcslen(needle);
    while (*haystack) {
        if (_wcsnicmp(haystack, needle, nlen) == 0) return 1;
        haystack++;
    }
    return 0;
}

static void format_subtitle_track_item(int index, wchar_t *out, size_t cap)
{
    const SubtitleTrackInfo *st;
    wchar_t name[96];
    const wchar_t *source;
    const wchar_t *kind;

    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (index < 0 || index >= g_info.subtitle_tracks_stored) return;
    st = &g_info.subtitle_tracks[index];
    source = st->title[0] ? st->title : (st->language[0] ? st->language
                                                        : text(STR_COMP_TRACK_NO_LANG));
    if (st->title[0] && st->language[0]) {
        _snwprintf(name, 96, L"%ls - %ls", st->language, st->title);
    } else {
        wcsncpy(name, source, 95);
    }
    name[95] = L'\0';
    if (st->forced && !wide_contains_ci(name, L"forced")) {
        wchar_t suffix[32];
        size_t len, remaining;
        _snwprintf(suffix, 32, L" (%ls)", text(STR_COMP_TRACK_FORCED));
        suffix[31] = L'\0';
        len = wcslen(name);
        remaining = (len < 95) ? 95 - len : 0;
        if (remaining > 0) {
            wcsncat(name, suffix, remaining);
        }
        name[95] = L'\0';
    }
    kind = encode_subtitle_is_text(st->codec) ? text(STR_COMP_TRACK_TEXT)
                                              : text(STR_COMP_TRACK_BITMAP);
    _snwprintf(out, cap, text(STR_COMP_SUB_ITEM_FMT),
               st->has_index ? st->index : (index + 1),
               name, subtitle_codec_label(st->codec), kind);
    out[cap - 1] = L'\0';
}

static const wchar_t *adv_quality_label(void)
{
    switch (g_adv_quality) {
        case 1: return presets_level_name(QUALITY_TINY);
        case 2: return presets_level_name(QUALITY_SMALL);
        case 3: return presets_level_name(QUALITY_BALANCED);
        case 4: return presets_level_name(QUALITY_HIGH);
        case 5: return text(STR_COMP_ADV_Q_ARCHIVE);
        case 6: return text(STR_COMP_ADV_Q_CUSTOM);
        default: return text(STR_COMP_ADV_Q_PRESET);
    }
}

static const wchar_t *container_label(const SourceInfo *s)
{
    const wchar_t *dot;
    if (!s) return text(STR_CONT_UNKNOWN);
    dot = wcsrchr(s->file_name, L'.');
    if (dot) {
        if (_wcsicmp(dot, L".mp4") == 0 || _wcsicmp(dot, L".m4v") == 0) return text(STR_CONT_MP4);
        if (_wcsicmp(dot, L".mkv") == 0) return text(STR_CONT_MKV);
        if (_wcsicmp(dot, L".webm") == 0) return text(STR_CONT_WEBM);
        if (_wcsicmp(dot, L".mov") == 0) return text(STR_CONT_MOV);
        if (_wcsicmp(dot, L".avi") == 0) return text(STR_CONT_AVI);
        if (_wcsicmp(dot, L".ts") == 0 || _wcsicmp(dot, L".m2ts") == 0 || _wcsicmp(dot, L".mts") == 0) return text(STR_CONT_TS);
        if (_wcsicmp(dot, L".mpg") == 0 || _wcsicmp(dot, L".mpeg") == 0) return text(STR_CONT_MPEG);
        if (_wcsicmp(dot, L".wmv") == 0 || _wcsicmp(dot, L".asf") == 0) return text(STR_CONT_WMV);
        if (_wcsicmp(dot, L".flv") == 0) return text(STR_CONT_FLV);
        if (_wcsicmp(dot, L".ogg") == 0 || _wcsicmp(dot, L".ogv") == 0) return text(STR_CONT_OGG);
        if (_wcsicmp(dot, L".vob") == 0) return text(STR_CONT_VOB);
        if (_wcsicmp(dot, L".3gp") == 0) return text(STR_CONT_3GP);
    }
    if (wcsstr(s->container, L"matroska")) return text(STR_CONT_MKV);
    if (wcsstr(s->container, L"webm")) return text(STR_CONT_WEBM);
    if (wcsstr(s->container, L"mp4") || wcsstr(s->container, L"mov")) return text(STR_CONT_MP4);
    if (wcsstr(s->container, L"avi")) return text(STR_CONT_AVI);
    if (wcsstr(s->container, L"mpegts")) return text(STR_CONT_TS);
    if (wcsstr(s->container, L"mpeg")) return text(STR_CONT_MPEG);
    return text(STR_CONT_UNKNOWN);
}

static const wchar_t *color_primaries_label(const wchar_t *v)
{
    if (!v || !*v) return NULL;
    if (_wcsicmp(v, L"bt709") == 0) return text(STR_COLOR_BT709);
    if (_wcsicmp(v, L"bt2020") == 0) return text(STR_COLOR_BT2020);
    if (_wcsicmp(v, L"bt601") == 0 || _wcsicmp(v, L"smpte170m") == 0) return text(STR_COLOR_BT601);
    return v;
}

static const wchar_t *color_transfer_label(const wchar_t *v)
{
    if (!v || !*v) return NULL;
    if (_wcsicmp(v, L"bt709") == 0) return text(STR_COLOR_BT709);
    if (_wcsicmp(v, L"smpte2084") == 0) return text(STR_COLOR_PQ);
    if (_wcsicmp(v, L"arib-std-b67") == 0) return text(STR_COLOR_HLG);
    return v;
}

static const wchar_t *color_space_label(const wchar_t *v)
{
    if (!v || !*v) return NULL;
    if (_wcsicmp(v, L"bt709") == 0) return text(STR_COLOR_BT709);
    if (_wcsicmp(v, L"bt2020nc") == 0) return text(STR_COLOR_BT2020NC);
    if (_wcsicmp(v, L"bt2020c") == 0) return text(STR_COLOR_BT2020C);
    return v;
}

static const wchar_t *color_range_label(const wchar_t *v)
{
    if (!v || !*v) return NULL;
    if (_wcsicmp(v, L"tv") == 0) return text(STR_COLOR_TV);
    if (_wcsicmp(v, L"pc") == 0) return text(STR_COLOR_PC);
    return v;
}

static void format_color_value(const SourceInfo *s, wchar_t *out, size_t cap)
{
    const wchar_t *p = color_primaries_label(s->color_primaries);
    const wchar_t *t = color_transfer_label(s->color_transfer);
    const wchar_t *m = color_space_label(s->color_space);
    const wchar_t *r = color_range_label(s->color_range);
    if (!p && !t && !m) {
        wcsncpy(out, text(STR_VALUE_UNKNOWN), cap - 1);
        out[cap - 1] = L'\0';
        return;
    }
    if (p && t && m && r) {
        _snwprintf(out, cap, text(STR_COLOR_FMT),
                   p, t, m, r);
    } else {
        _snwprintf(out, cap, text(STR_COLOR_FMT_3),
                   p ? p : text(STR_VALUE_UNKNOWN),
                   t ? t : text(STR_VALUE_UNKNOWN),
                   m ? m : text(STR_VALUE_UNKNOWN));
    }
    out[cap - 1] = L'\0';
}

static int is_efficient(const SourceInfo *s)
{
    if (!s || s->bits_per_pixel <= 0.0) return 0;
    if (_wcsicmp(s->video_codec, L"hevc") == 0 ||
        _wcsicmp(s->video_codec, L"av1") == 0 ||
        _wcsicmp(s->video_codec, L"vp9") == 0) {
        return s->bits_per_pixel < 0.075;
    }
    if (_wcsicmp(s->video_codec, L"h264") == 0) return s->bits_per_pixel < 0.055;
    return 0;
}

/* ------------------------------------------------------------------- draw */
static int measure_text(HDC hdc, HFONT font, const wchar_t *s)
{
    SIZE sz;
    HGDIOBJ old;
    if (!s) return 0;
    old = SelectObject(hdc, font);
    GetTextExtentPoint32W(hdc, s, (int)wcslen(s), &sz);
    SelectObject(hdc, old);
    return sz.cx;
}

static void draw_panel(HDC hdc, const RECT *prc, const wchar_t *title)
{
    RECT tab;
    int tab_h = S(20);

    gb_frame_round(hdc, prc, GB_DARKEST, S(2), GB_DARK, GB_LIGHTEST, 1);
    tab.left = prc->left + S(2);
    tab.top = prc->top + S(2);
    tab.right = prc->right - S(2);
    tab.bottom = tab.top + tab_h;
    gb_fill(hdc, &tab, GB_DARKEST);
    {
        RECT tr = tab;
        tr.left += S(8);
        tr.right -= S(8);
        gb_text(hdc, g_fontBold, GB_LIGHTEST, &tr, title,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static void draw_step_tab(HDC hdc, const RECT *rc, const wchar_t *label, int active)
{
    if (active) {
        gb_fill_round(hdc, rc, GB_DARKEST, GB_DARK);
        gb_frame_round(hdc, rc, GB_DARKEST, S(2), GB_DARK, GB_DARKEST, 0);
        gb_text(hdc, g_fontSmallBold, GB_LIGHTEST, rc, label,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        gb_fill_round(hdc, rc, GB_LIGHTEST, GB_DARK);
        gb_frame_round(hdc, rc, GB_DARK, S(2), GB_DARK, GB_LIGHTEST, 0);
        gb_text(hdc, g_fontSmallBold, GB_DARKEST, rc, label,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static void draw_steps(HDC hdc, int client_w)
{
    RECT r;
    int x = S(20), y = S(52), h = S(22);
    int gap = S(8);
    int w = (client_w - S(40) - gap * 2) / 3;

    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    draw_step_tab(hdc, &r, text(STR_STEP_VIDEO), g_step == 0);
    x += w + gap;
    r.left = x; r.right = x + w;
    draw_step_tab(hdc, &r, text(STR_STEP_QUALITY), g_step == 1);
    x += w + gap;
    r.left = x; r.right = x + w;
    draw_step_tab(hdc, &r, text(STR_STEP_COMPRESS), g_step == 2);
}

static void draw_status_strip(HDC hdc)
{
    RECT bar;
    RECT dot;

    gb_fill_round(hdc, &g_rc_status, GB_LIGHTEST, GB_DARK);
    gb_frame_round(hdc, &g_rc_status, GB_DARKEST, S(2), GB_DARK, GB_LIGHTEST, 0);

    dot.left = g_rc_status.left + S(10);
    dot.top = g_rc_status.top + (g_rc_status.bottom - g_rc_status.top - S(10)) / 2;
    dot.right = dot.left + S(10);
    dot.bottom = dot.top + S(10);
    if (g_status_error) {
        gb_fill(hdc, &dot, GB_DARKEST);
    } else {
        gb_fill(hdc, &dot, GB_LIGHTEST);
        gb_frame(hdc, &dot, GB_DARKEST, S(2));
    }

    bar = g_rc_status;
    bar.left += S(28);
    bar.right -= S(10);
    bar.top += S(4);
    bar.bottom -= S(4);
    gb_text(hdc, g_fontSmall, GB_DARKEST, &bar, g_status,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static int draw_badge(HDC hdc, int x, int y, int max_right, const wchar_t *label, int dark)
{
    RECT r;
    int w, h = S(18);
    w = measure_text(hdc, g_fontSmallBold, label) + S(14);
    if (x + w > max_right) return x;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    if (dark) {
        gb_fill_round(hdc, &r, GB_DARKEST, GB_LIGHTEST);
        gb_frame_round(hdc, &r, GB_DARKEST, S(2), GB_LIGHTEST, GB_DARKEST, 0);
        gb_text(hdc, g_fontSmallBold, GB_LIGHTEST, &r, label,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        gb_fill_round(hdc, &r, GB_LIGHTEST, GB_LIGHTEST);
        gb_frame_round(hdc, &r, GB_DARK, S(2), GB_LIGHTEST, GB_LIGHTEST, 0);
        gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, label,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    return x + w + S(6);
}

static void draw_badges(HDC hdc, const SourceInfo *s, int x, int y, int max_right)
{
    if (s->is_dolby_vision) {
        x = draw_badge(hdc, x, y, max_right, text(STR_BADGE_DV), 1);
    } else if (s->is_hlg) {
        x = draw_badge(hdc, x, y, max_right, text(STR_BADGE_HLG), 1);
    } else if (s->is_hdr) {
        x = draw_badge(hdc, x, y, max_right, text(STR_BADGE_HDR10), 1);
    } else {
        x = draw_badge(hdc, x, y, max_right, text(STR_VALUE_SDR), 0);
    }

    if (s->bit_depth >= 12) {
        x = draw_badge(hdc, x, y, max_right, text(STR_BADGE_12BIT), 1);
    } else if (s->bit_depth >= 10) {
        x = draw_badge(hdc, x, y, max_right, text(STR_BADGE_10BIT), 1);
    } else {
        x = draw_badge(hdc, x, y, max_right, text(STR_BADGE_8BIT), 0);
    }

    if (s->is_vfr)          x = draw_badge(hdc, x, y, max_right, text(STR_BADGE_VFR), 0);
    if (s->is_interlaced)   x = draw_badge(hdc, x, y, max_right, text(STR_BADGE_INTERLACED), 0);
    if (s->rotation != 0)   x = draw_badge(hdc, x, y, max_right, text(STR_BADGE_ROTATED), 0);
    if (s->is_anamorphic)   x = draw_badge(hdc, x, y, max_right, text(STR_BADGE_ANAMORPHIC), 0);
    (void)x;
}

static void draw_field(HDC hdc, int x, int y, int w, const wchar_t *label, const wchar_t *value)
{
    RECT lr = { x, y, x + S(96), y + S(20) };
    RECT vr = { x + S(96), y, x + w, y + S(20) };
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &lr, label,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    gb_text(hdc, g_fontSmall, GB_DARKEST, &vr, value ? value : L"",
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void format_audio_value(const SourceInfo *s, wchar_t *out, size_t cap)
{
    wchar_t br[64];
    wchar_t line[256];
    if (!s || s->audio_track_count <= 0) {
        wcsncpy(out, text(STR_VALUE_NONE), cap - 1);
        out[cap - 1] = L'\0';
        return;
    }
    util_format_bitrate(s->audio_bitrate, br, 64);
    _snwprintf(line, 256, text(STR_AUDIO_LINE_FMT),
        audio_codec_label(s->audio_codec), s->audio_channels, br);
    line[255] = L'\0';
    if (s->audio_track_count > 1) {
        wchar_t more[64];
        _snwprintf(more, 64, text(STR_AUDIO_MORE_FMT), s->audio_track_count - 1);
        more[63] = L'\0';
        _snwprintf(out, cap, L"%ls%ls", line, more);
    } else {
        wcsncpy(out, line, cap - 1);
    }
    out[cap - 1] = L'\0';
}

static void draw_empty(HDC hdc, const SourceInfo *unused)
{
    RECT inner;
    int cx;
    (void)unused;

    gb_fill_round(hdc, &g_rc_drop, GB_LIGHTEST, GB_LIGHTEST);
    gb_frame_round(hdc, &g_rc_drop, GB_DARK, S(2), GB_LIGHTEST, GB_LIGHTEST, 1);
    inner = g_rc_drop;
    InflateRect(&inner, -S(5), -S(5));
    gb_frame(hdc, &inner, GB_LIGHT, S(1));

    cx = (g_rc_drop.left + g_rc_drop.right) / 2;
    gb_icon(hdc, cx - S(24), g_rc_drop.top + S(28), S(2) * 2, 0);

    {
        RECT tr = { g_rc_drop.left + S(10), g_rc_drop.top + S(82),
                    g_rc_drop.right - S(10), g_rc_drop.top + S(106) };
        gb_text(hdc, g_fontBold, GB_DARKEST, &tr, text(STR_DROP_TITLE),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    {
        RECT tr = { g_rc_drop.left + S(10), g_rc_drop.top + S(108),
                    g_rc_drop.right - S(10), g_rc_drop.top + S(130) };
        gb_text(hdc, g_fontSmall, GB_DARKEST, &tr, text(STR_DROP_HINT),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    {
        RECT tr = { g_rc_drop.left + S(10), g_rc_drop.bottom - S(32),
                    g_rc_drop.right - S(10), g_rc_drop.bottom - S(12) };
        gb_text(hdc, g_fontSmall, GB_DARK, &tr, text(STR_SUPPORTED_FORMATS),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static void draw_probing(HDC hdc)
{
    RECT inner;
    int cx;

    gb_fill_round(hdc, &g_rc_drop, GB_LIGHTEST, GB_LIGHTEST);
    gb_frame_round(hdc, &g_rc_drop, GB_DARK, S(2), GB_LIGHTEST, GB_LIGHTEST, 1);
    inner = g_rc_drop;
    InflateRect(&inner, -S(5), -S(5));
    gb_frame(hdc, &inner, GB_LIGHT, S(1));

    cx = (g_rc_drop.left + g_rc_drop.right) / 2;
    gb_icon(hdc, cx - S(24), g_rc_drop.top + S(28), S(2) * 2, 0);

    {
        RECT tr = { g_rc_drop.left + S(20), g_rc_drop.top + S(84),
                    g_rc_drop.right - S(20), g_rc_drop.top + S(108) };
        gb_text(hdc, g_fontBold, GB_DARKEST, &tr, g_pending_name,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    {
        RECT tr = { g_rc_drop.left + S(10), g_rc_drop.top + S(112),
                    g_rc_drop.right - S(10), g_rc_drop.top + S(134) };
        gb_text(hdc, g_fontSmall, GB_DARKEST, &tr, text(STR_STATUS_PROBING),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static void draw_summary(HDC hdc, const SourceInfo *s)
{
    wchar_t buf[256];
    wchar_t buf2[256];
    const wchar_t *notes[3];
    int note_count = 0;
    int x1 = S(32), x2 = S(330);
    int w1 = S(276), w2 = S(284);
    int y = S(164);
    int i;
    RECT tr;

    tr.left = S(32);
    tr.top = S(106);
    tr.right = S(614);
    tr.bottom = S(130);
    gb_text(hdc, g_fontBold, GB_DARKEST, &tr, s->file_name,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    draw_badges(hdc, s, S(32), S(136), S(614));

    util_format_bytes(s->file_size, buf, 256);
    draw_field(hdc, x1, y, w1, text(STR_LABEL_SIZE), buf);
    util_format_duration(s->duration, buf, 256);
    draw_field(hdc, x1, y + S(24), w1, text(STR_LABEL_DURATION), buf);
    _snwprintf(buf, 256, text(STR_RESOLUTION_FMT), s->display_width, s->display_height);
    buf[255] = L'\0';
    draw_field(hdc, x1, y + S(48), w1, text(STR_LABEL_RESOLUTION), buf);
    draw_field(hdc, x1, y + S(72), w1, text(STR_LABEL_VIDEO), video_codec_label(s->video_codec));

    util_format_bitrate(s->video_bitrate, buf, 256);
    draw_field(hdc, x2, y, w2, text(STR_LABEL_BITRATE), buf);
    util_format_fps(s->fps, buf, 256);
    draw_field(hdc, x2, y + S(24), w2, text(STR_LABEL_FPS), buf);
    format_audio_value(s, buf, 256);
    draw_field(hdc, x2, y + S(48), w2, text(STR_LABEL_AUDIO), buf);
    draw_field(hdc, x2, y + S(72), w2, text(STR_LABEL_CONTAINER), container_label(s));

    if (is_efficient(s)) notes[note_count++] = text(STR_NOTE_EFFICIENT);
    if (s->is_dolby_vision) notes[note_count++] = text(STR_NOTE_DOLBY_VISION);
    else if (s->is_hdr) notes[note_count++] = text(STR_NOTE_HDR);
    if (s->is_interlaced) notes[note_count++] = text(STR_NOTE_INTERLACED);
    else if (s->is_anamorphic) notes[note_count++] = text(STR_NOTE_ANAMORPHIC);
    else if (s->is_vfr) notes[note_count++] = text(STR_NOTE_VFR);
    else if (s->duration > 3600.0) notes[note_count++] = text(STR_NOTE_LONG);
    if (note_count > 3) note_count = 3;

    for (i = 0; i < note_count; i++) {
        int ny = S(272) + i * S(18);
        RECT bar = { S(32), ny + S(3), S(35), ny + S(15) };
        RECT ntr = { S(42), ny, S(614), ny + S(18) };
        gb_fill(hdc, &bar, GB_DARKEST);
        gb_text(hdc, g_fontSmall, GB_DARKEST, &ntr, notes[i],
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    (void)buf2;
}

static void draw_details(HDC hdc, const SourceInfo *s)
{
    wchar_t buf[256];
    wchar_t buf2[256];
    int x1 = S(32), x2 = S(330);
    int w1 = S(276), w2 = S(284);
    int y = S(136), row = S(20);
    RECT tr;

    tr.left = S(32);
    tr.top = S(106);
    tr.right = S(614);
    tr.bottom = S(130);
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &tr, text(STR_LABEL_TECHNICAL),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    draw_field(hdc, x1, y, w1, text(STR_LABEL_FILE), s->file_name);
    draw_field(hdc, x1, y + row, w1, text(STR_LABEL_CONTAINER), container_label(s));
    util_format_bytes(s->file_size, buf, 256);
    draw_field(hdc, x1, y + row * 2, w1, text(STR_LABEL_SIZE), buf);
    util_format_duration(s->duration, buf, 256);
    draw_field(hdc, x1, y + row * 3, w1, text(STR_LABEL_DURATION), buf);
    if (s->coded_width != s->display_width || s->coded_height != s->display_height) {
        _snwprintf(buf, 256, text(STR_RESOLUTION_FMT), s->coded_width, s->coded_height);
    } else {
        _snwprintf(buf, 256, text(STR_RESOLUTION_FMT), s->display_width, s->display_height);
    }
    buf[255] = L'\0';
    draw_field(hdc, x1, y + row * 4, w1, text(STR_LABEL_RESOLUTION), buf);
    _snwprintf(buf, 256, text(STR_RESOLUTION_FMT), s->display_width, s->display_height);
    buf[255] = L'\0';
    draw_field(hdc, x1, y + row * 5, w1, text(STR_LABEL_DISPLAY), buf);
    draw_field(hdc, x1, y + row * 6, w1, text(STR_LABEL_SCAN),
               s->is_interlaced ? text(STR_VALUE_INTERLACED) : text(STR_VALUE_PROGRESSIVE));
    draw_field(hdc, x1, y + row * 7, w1, text(STR_LABEL_FRAME_RATE_TYPE),
               s->is_vfr ? text(STR_VALUE_VFR) : text(STR_VALUE_CFR));
    if (s->rotation != 0) {
        _snwprintf(buf, 256, text(STR_ROTATION_FMT), s->rotation);
        buf[255] = L'\0';
        draw_field(hdc, x1, y + row * 8, w1, text(STR_LABEL_ROTATION), buf);
    } else {
        draw_field(hdc, x1, y + row * 8, w1, text(STR_LABEL_ROTATION), text(STR_VALUE_NONE));
    }

    draw_field(hdc, x2, y, w2, text(STR_LABEL_VIDEO), video_codec_label(s->video_codec));
    draw_field(hdc, x2, y + row, w2, text(STR_LABEL_PROFILE), s->profile[0] ? s->profile : text(STR_VALUE_UNKNOWN));
    draw_field(hdc, x2, y + row * 2, w2, text(STR_LABEL_PIXEL_FORMAT), s->pix_fmt[0] ? s->pix_fmt : text(STR_VALUE_UNKNOWN));
    _snwprintf(buf, 256, text(STR_BIT_DEPTH_FMT), s->bit_depth);
    buf[255] = L'\0';
    draw_field(hdc, x2, y + row * 3, w2, text(STR_LABEL_BIT_DEPTH), buf);
    draw_field(hdc, x2, y + row * 4, w2, text(STR_LABEL_RANGE),
               s->is_dolby_vision ? text(STR_VALUE_DOLBY_VISION) :
               (s->is_hlg ? text(STR_VALUE_HLG) : (s->is_hdr ? text(STR_VALUE_HDR10) : text(STR_VALUE_SDR))));
    format_color_value(s, buf, 256);
    draw_field(hdc, x2, y + row * 5, w2, text(STR_LABEL_COLOR), buf);
    _snwprintf(buf, 256, text(STR_PIXEL_ASPECT_FMT), s->sar[0] ? s->sar : text(STR_VALUE_UNKNOWN));
    buf[255] = L'\0';
    draw_field(hdc, x2, y + row * 6, w2, text(STR_LABEL_PIXEL_ASPECT), buf);
    format_audio_value(s, buf, 256);
    draw_field(hdc, x2, y + row * 7, w2, text(STR_LABEL_AUDIO), buf);
    if (s->audio_track_count > 0 && s->audio_sample_rate > 0) {
        _snwprintf(buf, 256, text(STR_SAMPLE_RATE_FMT), s->audio_sample_rate);
        buf[255] = L'\0';
    } else {
        wcsncpy(buf, text(STR_VALUE_UNKNOWN), 255);
        buf[255] = L'\0';
    }
    draw_field(hdc, x2, y + row * 8, w2, text(STR_LABEL_SAMPLE_RATE), buf);
    _snwprintf(buf2, 256, text(STR_TRACKS_FMT), s->audio_track_count, s->subtitle_track_count);
    buf2[255] = L'\0';
    draw_field(hdc, x2, y + row * 9, w2, text(STR_LABEL_TRACKS), buf2);
}

static void draw_progress_bar(HDC hdc, const RECT *prc, int percent)
{
    RECT inner;
    int inner_w, fill_w, x;
    int block, gap;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    gb_fill(hdc, prc, GB_LIGHT);
    gb_frame_round(hdc, prc, GB_DARKEST, S(2), GB_LIGHTEST, GB_LIGHT, 0);

    inner = *prc;
    InflateRect(&inner, -S(3), -S(3));
    inner_w = inner.right - inner.left;
    if (inner_w <= 0) return;

    fill_w = MulDiv(inner_w, percent, 100);
    block = S(7);
    gap = S(2);
    if (block < 2) block = 2;
    if (gap < 1) gap = 1;

    for (x = block + gap; x < inner_w; x += block + gap) {
        RECT s;
        s.left = inner.left + x - gap;
        s.top = inner.top;
        s.right = s.left + 1;
        s.bottom = inner.bottom;
        gb_fill(hdc, &s, GB_DARK);
    }
    for (x = 0; x < fill_w; x += block + gap) {
        RECT b;
        b.left = inner.left + x;
        b.top = inner.top;
        b.right = b.left + block;
        if (b.right > inner.left + fill_w) b.right = inner.left + fill_w;
        b.bottom = inner.bottom;
        if (b.right > b.left) gb_fill(hdc, &b, GB_DARKEST);
    }
}

static void draw_hw_test(HDC hdc)
{
    wchar_t buf[512];
    RECT tr;
    RECT bar;
    int percent = 0;
    int elapsed = (int)((GetTickCount64() - g_hw_start_tick) / 1000);

    if (g_hw_progress.run_total > 0) {
        percent = g_hw_progress.run_done * 100 / g_hw_progress.run_total;
    }

    tr.left = S(32);
    tr.top = S(114);
    tr.right = S(614);
    tr.bottom = S(156);
    gb_text(hdc, g_fontBold, GB_DARKEST, &tr, text(STR_HW_HEADING),
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    _snwprintf(buf, 512, text(STR_HW_ABOUT_FMT), g_hw_est_seconds);
    buf[511] = L'\0';
    tr.top = S(158);
    tr.bottom = S(178);
    gb_text(hdc, g_fontSmall, GB_DARKEST, &tr, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    buf[0] = L'\0';
    if (g_hw_progress.event == HW_EVT_RUN) {
        if (g_hw_progress.warmup) {
            _snwprintf(buf, 512, text(STR_HW_WARMUP_FMT),
                       hw_encoder_name(g_hw_progress.encoder), g_hw_progress.bit_depth);
        } else {
            _snwprintf(buf, 512, text(STR_HW_RUN_FMT),
                       hw_encoder_name(g_hw_progress.encoder), g_hw_progress.bit_depth,
                       g_hw_progress.run_done, g_hw_progress.run_total);
        }
        buf[511] = L'\0';
    } else {
        wcsncpy(buf, text(STR_STATUS_HW), 511);
        buf[511] = L'\0';
    }
    tr.top = S(184);
    tr.bottom = S(206);
    gb_text(hdc, g_fontSmall, GB_DARKEST, &tr, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SetRect(&bar, S(32), S(216), S(614), S(230));
    draw_progress_bar(hdc, &bar, percent);

    _snwprintf(buf, 512, text(STR_HW_ELAPSED_FMT), elapsed);
    buf[511] = L'\0';
    tr.top = S(238);
    tr.bottom = S(258);
    gb_text(hdc, g_fontSmall, GB_DARKEST, &tr, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    tr.top = S(276);
    tr.bottom = S(320);
    gb_text(hdc, g_fontSmall, GB_DARK, &tr, text(STR_HW_NOTE),
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

static void draw_chip(HDC hdc, const RECT *rc, const wchar_t *label)
{
    RECT r = *rc;
    gb_fill_round(hdc, &r, GB_LIGHTEST, GB_LIGHTEST);
    gb_frame_round(hdc, &r, GB_DARKEST, S(2), GB_LIGHTEST, GB_LIGHTEST, 0);
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, label,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void preset_quality_hint(int encoder, int level, wchar_t *out, size_t cap)
{
    int q = presets_quality_value(encoder, level, PRESETS_VIDEO_HEVC);
    if (!out || cap == 0) return;
    switch (encoder) {
        case HW_ENC_NVENC: _snwprintf(out, cap, L"NVENC CQ %d", q); break;
        case HW_ENC_QSV:   _snwprintf(out, cap, L"QSV gq %d", q); break;
        case HW_ENC_AMF:   _snwprintf(out, cap, L"AMF QP %d", q); break;
        case HW_ENC_X265:
        default:           _snwprintf(out, cap, L"x265 CRF %d", q); break;
    }
    out[cap - 1] = L'\0';
}

static void draw_quality(HDC hdc)
{
    wchar_t buf[512];
    wchar_t size_lo[64], size_hi[64], time_lo[64], time_hi[64];
    wchar_t size_mid[64], time_mid[64];
    RECT tr;
    int levels[ESTIMATE_MAX_LEVELS];
    int count;
    int i;
    int enc = g_quality_has_result ? g_est.encoder
                                   : estimate_choose_encoder(&g_info, &g_hw_info);
    int x = S(32), y = S(140), gap = S(8), card_h = S(140);
    int card_w;

    count = g_est.level_count;
    if (count <= 0) {
        count = presets_available_levels(quality_current_profile(), levels);
    } else {
        for (i = 0; i < count; i++) levels[i] = g_est.levels_used[i];
    }
    if (count <= 0) count = 1;
    card_w = (S(582) - gap * (count - 1)) / count;
    g_card_count = count;

    SetRect(&tr, S(32), S(110), S(614), S(132));
    gb_text(hdc, g_fontBold, GB_DARKEST, &tr, text(STR_QUALITY_HEADING),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    for (i = 0; i < count; i++) {
        RECT r;
        EstimateLevel *e = NULL;
        int level = levels[i];
        int selected = (level == g_quality_selected);
        COLORREF ink = selected ? GB_LIGHTEST : GB_DARKEST;

        r.left = x + i * (card_w + gap);
        r.top = y;
        r.right = r.left + card_w;
        r.bottom = y + card_h;
        g_rc_cards[i] = r;

        gb_fill_round(hdc, &r, selected ? GB_DARKEST : GB_LIGHTEST, GB_LIGHTEST);
        gb_frame_round(hdc, &r, GB_DARKEST, S(2), GB_LIGHTEST,
                       selected ? GB_DARKEST : GB_LIGHTEST, 0);

        {
            RECT nr = { r.left + S(8), r.top + S(8), r.right - S(8), r.top + S(30) };
            gb_text(hdc, g_fontBold, ink, &nr, presets_level_name(level),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        {
            RECT nr = { r.left + S(8), r.top + S(28), r.right - S(8), r.top + S(42) };
            gb_text(hdc, g_fontSmall, ink, &nr, presets_level_note(level),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        {
            wchar_t hint[64];
            RECT qr = { r.left + S(8), r.top + S(42), r.right - S(8), r.top + S(54) };
            preset_quality_hint(enc, level, hint, 64);
            gb_text(hdc, g_fontSmall, selected ? ink : GB_DARK, &qr, hint,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        if (g_quality_has_result) e = quality_level_result(level);
        if (e && e->available) {
            double card_video = e->video_size_bytes > 0.0
                ? e->video_size_bytes : e->est_size_bytes;
            double src_video = (g_info.video_bitrate > 0 ? (double)g_info.video_bitrate
                                                         : (double)g_info.format_bitrate) *
                               g_info.duration / 8.0;
            util_format_bytes((long long)card_video, size_mid, 64);
            util_format_duration(e->est_seconds, time_mid, 64);
            {
                RECT sr = { r.left + S(8), r.top + S(54), r.right - S(8), r.top + S(76) };
                _snwprintf(buf, 512, text(STR_QUALITY_SIZE_FMT), size_mid);
                buf[511] = L'\0';
                gb_text(hdc, g_fontBold, ink, &sr, buf,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            {
                RECT trr = { r.left + S(8), r.top + S(78), r.right - S(8), r.top + S(96) };
                _snwprintf(buf, 512, text(STR_QUALITY_TIME_FMT), time_mid);
                buf[511] = L'\0';
                gb_text(hdc, g_fontSmall, ink, &trr, buf,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            if (src_video > 0.0 && card_video > 0.0) {
                int save = (int)((1.0 - card_video / src_video) * 100.0);
                if (card_video > src_video) {
                    wcsncpy(buf, text(STR_QUALITY_LARGER), 511);
                } else if (save < 5) {
                    wcsncpy(buf, text(STR_QUALITY_SAME_SIZE), 511);
                } else {
                    if (save > 99) save = 99;
                    _snwprintf(buf, 512, text(STR_QUALITY_SAVINGS_FMT), save);
                }
                buf[511] = L'\0';
                {
                    RECT sr = { r.left + S(8), r.top + S(98), r.right - S(8), r.top + S(116) };
                    gb_text(hdc, g_fontSmall, ink, &sr, buf,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
            }
        } else {
            RECT nr = { r.left + S(8), r.top + S(60), r.right - S(8), r.top + S(110) };
            gb_text(hdc, g_fontSmall, ink, &nr, text(STR_QUALITY_NO_ESTIMATE),
                    DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        }

        if (e && e->ssim_available) {
            RECT rr = { r.left + S(8), r.bottom - S(24), r.right - S(8), r.bottom - S(8) };
            _snwprintf(buf, 512, text(STR_QUALITY_SSIM_FMT), e->ssim);
            buf[511] = L'\0';
            gb_text(hdc, g_fontSmallBold, ink, &rr, buf,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    /* Selected summary, preset details and notes. */
    {
        EstimateLevel *sel = quality_level_result(g_quality_selected);
        double src_video_bps = g_info.video_bitrate > 0
            ? (double)g_info.video_bitrate : (double)g_info.format_bitrate;
        double src_video_bytes = src_video_bps * g_info.duration / 8.0;

        SetRect(&g_rc_q_custom, S(500), S(286), S(614), S(306));
        SetRect(&g_rc_q_details, S(500), S(306), S(614), S(326));
        SetRect(&g_rc_q_deep, S(32), S(354), S(190), S(376));

        if (g_quality_show_details) {
            RECT sr = { S(32), S(288), S(614), S(302) };
            _snwprintf(buf, 512, L"%ls  /  %ls", text(STR_QUALITY_DETAILS),
                       hw_encoder_name(enc));
            buf[511] = L'\0';
            gb_text(hdc, g_fontSmallBold, GB_DARKEST, &sr, buf,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            for (i = 0; i < 4; i++) {
                SetRect(&sr, S(32), S(300 + i * 13), S(614), S(312 + i * 13));
                _snwprintf(buf, 512, text(STR_QUALITY_DETAIL_LINE_FMT),
                           presets_level_name(i),
                           presets_quality_value(HW_ENC_X265, i, PRESETS_VIDEO_HEVC),
                           presets_quality_value(HW_ENC_QSV, i, PRESETS_VIDEO_HEVC),
                           presets_quality_value(HW_ENC_NVENC, i, PRESETS_VIDEO_HEVC),
                           presets_quality_value(HW_ENC_AMF, i, PRESETS_VIDEO_HEVC));
                buf[511] = L'\0';
                gb_text(hdc, g_fontSmall, GB_DARKEST, &sr, buf,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            draw_chip(hdc, &g_rc_q_deep, text(STR_QUALITY_DEEP));
            SetRect(&sr, S(198), S(354), S(492), S(376));
            gb_text(hdc, g_fontSmall, GB_DARK, &sr, text(STR_QUALITY_DEEP_NOTE),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            draw_chip(hdc, &g_rc_q_details, text(STR_QUALITY_SUMMARY));
        } else {
            RECT sr = { S(32), S(288), S(490), S(306) };
            if (sel && sel->available) {
                wchar_t left[128], right[128];
                double ratio = (sel->est_size_bytes > 0.0 && sel->video_size_bytes > 0.0)
                    ? sel->video_size_bytes / sel->est_size_bytes : 1.0;
                util_format_bytes((long long)(sel->est_size_min * ratio), size_lo, 64);
                util_format_bytes((long long)(sel->est_size_max * ratio), size_hi, 64);
                util_format_duration(sel->est_seconds_min, time_lo, 64);
                util_format_duration(sel->est_seconds_max, time_hi, 64);
                _snwprintf(left, 128, text(STR_QUALITY_RANGE_FMT), size_lo, size_hi);
                left[127] = L'\0';
                _snwprintf(right, 128, text(STR_QUALITY_RANGE_FMT), time_lo, time_hi);
                right[127] = L'\0';
                _snwprintf(buf, 512, L"%ls  %ls  /  about %ls",
                           presets_level_name(g_quality_selected), left, right);
                buf[511] = L'\0';
            } else {
                wcsncpy(buf, text(STR_QUALITY_NO_ESTIMATE), 511);
                buf[511] = L'\0';
            }
            gb_text(hdc, g_fontSmallBold, GB_DARKEST, &sr, buf,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            draw_chip(hdc, &g_rc_q_custom, text(STR_QUALITY_CUSTOM));

            SetRect(&g_rc_q_encoder, S(32), S(306), S(390), S(326));
            _snwprintf(buf, 512, text(STR_QUALITY_ENCODER_PICK_FMT),
                       hw_encoder_name(enc));
            buf[511] = L'\0';
            draw_chip(hdc, &g_rc_q_encoder, buf);
            draw_chip(hdc, &g_rc_q_details, text(STR_QUALITY_DETAILS));

            SetRect(&sr, S(32), S(326), S(614), S(342));
            if (sel && sel->available && sel->video_bps > 0.0 && src_video_bps > 0.0) {
                wchar_t ssrc[64], sdst[64], brs[64], brd[64];
                util_format_bytes((long long)src_video_bytes, ssrc, 64);
                util_format_bytes((long long)sel->video_size_bytes, sdst, 64);
                util_format_bitrate((long long)src_video_bps, brs, 64);
                util_format_bitrate((long long)sel->video_bps, brd, 64);
                _snwprintf(buf, 512, text(STR_COMP_VIDEO_BREAKDOWN_FMT),
                           ssrc, video_codec_label(g_info.video_codec), brs,
                           sdst, text(STR_CODEC_HEVC), brd);
                buf[511] = L'\0';
                gb_text(hdc, g_fontSmall, GB_DARKEST, &sr, buf,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            SetRect(&sr, S(32), S(344), S(614), S(358));
            gb_text(hdc, g_fontSmall, GB_DARK, &sr, text(STR_QUALITY_COMPAT),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SetRect(&sr, S(32), S(360), S(614), S(376));
            gb_text(hdc, g_fontSmall, GB_DARK, &sr, text(STR_QUALITY_AUDIO_NOTE),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
    }
}

static void draw_quality_progress(HDC hdc)
{
    wchar_t buf[512];
    RECT tr;
    RECT bar;
    int percent = 0;
    int elapsed = (int)((GetTickCount64() - g_est_start_tick) / 1000);

    SetRect(&tr, S(32), S(112), S(614), S(156));
    gb_text(hdc, g_fontBold, GB_DARKEST, &tr, text(STR_QUALITY_HEADING),
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    if (g_quality_last_seconds > 0.0) {
        _snwprintf(buf, 512, text(STR_QUALITY_ABOUT_LAST_FMT), g_est_seconds);
    } else {
        int low = (int)((double)g_est_seconds * 0.7);
        int high = (int)((double)g_est_seconds * 2.5);
        if (low < 3) low = 3;
        if (high < low + 5) high = low + 5;
        _snwprintf(buf, 512, text(STR_QUALITY_ABOUT_RANGE_FMT), low, high);
    }
    buf[511] = L'\0';
    SetRect(&tr, S(32), S(160), S(614), S(180));
    gb_text(hdc, g_fontSmall, GB_DARKEST, &tr, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (g_est_progress.level_total > 0) {
        _snwprintf(buf, 512, text(STR_QUALITY_TESTING_FMT),
                   hw_encoder_name(g_est.encoder),
                   presets_level_name(g_est_progress.level),
                   g_est_progress.clip_index, g_est_progress.clip_total);
        buf[511] = L'\0';
        percent = ((g_est_progress.level_index * g_est_progress.clip_total) +
                   g_est_progress.clip_index) * 100 /
                  (g_est_progress.level_total * g_est_progress.clip_total);
    } else {
        wcsncpy(buf, text(STR_STATUS_QUALITY_TESTING), 511);
        buf[511] = L'\0';
    }
    SetRect(&tr, S(32), S(184), S(614), S(206));
    gb_text(hdc, g_fontSmall, GB_DARKEST, &tr, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SetRect(&bar, S(32), S(216), S(614), S(230));
    draw_progress_bar(hdc, &bar, percent);

    _snwprintf(buf, 512, text(STR_HW_ELAPSED_FMT), elapsed);
    buf[511] = L'\0';
    SetRect(&tr, S(32), S(238), S(614), S(258));
    gb_text(hdc, g_fontSmall, GB_DARKEST, &tr, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&tr, S(32), S(276), S(614), S(320));
    gb_text(hdc, g_fontSmall, GB_DARK, &tr, text(STR_QUALITY_PROGRESS_NOTE),
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

static void draw_bitmap_fit(HDC hdc, HBITMAP bmp, const RECT *box)
{
    BITMAP bm;
    HDC mem;
    HGDIOBJ old;
    int bw, bh;
    double scale;
    int w, h, x, y;

    if (!bmp || !GetObjectW(bmp, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        RECT tr = *box;
        gb_text(hdc, g_fontSmall, GB_LIGHTEST, &tr, text(STR_QUALITY_PREVIEW_UNAVAILABLE),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    bw = box->right - box->left;
    bh = box->bottom - box->top;
    scale = (double)bw / (double)bm.bmWidth;
    if ((double)bh / (double)bm.bmHeight < scale) scale = (double)bh / (double)bm.bmHeight;
    w = (int)((double)bm.bmWidth * scale);
    h = (int)((double)bm.bmHeight * scale);
    x = box->left + (bw - w) / 2;
    y = box->top + (bh - h) / 2;

    mem = CreateCompatibleDC(hdc);
    if (!mem) return;
    old = SelectObject(mem, bmp);
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, NULL);
    StretchBlt(hdc, x, y, w, h, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void draw_quality_preview(HDC hdc)
{
    wchar_t buf[256];
    RECT r;
    RECT rr;
    int levels[ESTIMATE_MAX_LEVELS];
    int count = g_est.level_count;
    int i;
    int x = S(32), y = S(110), gap = S(8);
    int chip_w;
    EstimateLevel *sel = quality_level_result(g_quality_selected);

    if (count <= 0) return;
    for (i = 0; i < count; i++) levels[i] = g_est.levels_used[i];
    chip_w = (S(582) - gap * (count - 1)) / count;
    g_preview_chip_count = count;

    /* Level chips with SSIM. */
    for (i = 0; i < count; i++) {
        EstimateLevel *e = &g_est.levels[i];
        int selected = (levels[i] == g_quality_selected);
        COLORREF ink = selected ? GB_LIGHTEST : GB_DARKEST;
        r.left = x + i * (chip_w + gap);
        r.top = y;
        r.right = r.left + chip_w;
        r.bottom = y + S(24);
        g_rc_preview_chips[i] = r;

        if (e->ssim_available) {
            wchar_t ssim_buf[32];
            _snwprintf(ssim_buf, 32, L"%.3f", e->ssim);
            ssim_buf[31] = L'\0';
            _snwprintf(buf, 256, L"%ls %ls", presets_level_name(levels[i]), ssim_buf);
        } else {
            _snwprintf(buf, 256, L"%ls %ls", presets_level_name(levels[i]),
                       text(STR_QUALITY_SSIM_NA));
        }
        buf[255] = L'\0';

        gb_fill_round(hdc, &r, selected ? GB_DARKEST : GB_LIGHTEST, GB_LIGHTEST);
        gb_frame_round(hdc, &r, GB_DARKEST, S(2), GB_LIGHTEST,
                       selected ? GB_DARKEST : GB_LIGHTEST, 0);
        gb_text(hdc, g_fontSmallBold, ink, &r, buf,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    /* Two frame boxes. */
    r.left = S(32); r.top = S(142); r.right = S(312); r.bottom = S(302);
    rr.left = S(334); rr.top = S(142); rr.right = S(614); rr.bottom = S(302);

    gb_fill_round(hdc, &r, GB_DARKEST, GB_LIGHTEST);
    gb_frame_round(hdc, &r, GB_DARKEST, S(2), GB_LIGHTEST, GB_DARKEST, 0);
    gb_fill_round(hdc, &rr, GB_DARKEST, GB_LIGHTEST);
    gb_frame_round(hdc, &rr, GB_DARKEST, S(2), GB_LIGHTEST, GB_DARKEST, 0);

    {
        RECT lab = r;
        RECT img = r;
        lab.bottom = lab.top + S(18);
        img.top = lab.bottom;
        InflateRect(&img, -S(2), -S(2));
        gb_text(hdc, g_fontSmallBold, GB_LIGHTEST, &lab, text(STR_QUALITY_ORIGINAL),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        draw_bitmap_fit(hdc, g_preview_orig_bmp, &img);
    }
    {
        RECT lab = rr;
        RECT img = rr;
        lab.bottom = lab.top + S(18);
        img.top = lab.bottom;
        InflateRect(&img, -S(2), -S(2));
        gb_text(hdc, g_fontSmallBold, GB_LIGHTEST, &lab, text(STR_QUALITY_COMPRESSED),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        draw_bitmap_fit(hdc, g_preview_enc_bmp, &img);
    }

    /* SSIM result and note. */
    if (sel && sel->available && sel->ssim_available) {
        _snwprintf(buf, 256, text(STR_QUALITY_SSIM_LABEL_FMT), sel->ssim,
                   quality_ssim_label(sel->ssim));
    } else {
        wcsncpy(buf, text(STR_QUALITY_SSIM_NA), 255);
    }
    buf[255] = L'\0';
    SetRect(&r, S(32), S(308), S(614), S(326));
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&r, S(32), S(328), S(614), S(344));
    gb_text(hdc, g_fontSmall, GB_DARK, &r, text(STR_QUALITY_PREVIEW_NOTE),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SetRect(&r, S(32), S(344), S(614), S(360));
    if (g_info.is_hdr) {
        gb_text(hdc, g_fontSmall, GB_DARK, &r, text(STR_QUALITY_HDR_PREVIEW),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

static const wchar_t *bitrate_preset_name(int preset)
{
    switch (preset) {
        case ENC_ABITRATE_VOICE:  return text(STR_COMP_BITRATE_VOICE);
        case ENC_ABITRATE_MUSIC:  return text(STR_COMP_BITRATE_MUSIC);
        case ENC_ABITRATE_HIGH:   return text(STR_COMP_BITRATE_HIGH);
        default:                  return text(STR_COMP_BITRATE_CUSTOM);
    }
}

static const wchar_t *encode_audio_codec_label(int codec)
{
    switch (codec) {
        case ENC_ACODEC_COPY:   return text(STR_COMP_AUDIO_COPY_LABEL);
        case ENC_ACODEC_OPUS:   return text(STR_CODEC_OPUS);
        case ENC_ACODEC_AAC:    return text(STR_CODEC_AAC);
        case ENC_ACODEC_MP3:    return text(STR_CODEC_MP3);
        case ENC_ACODEC_FLAC:   return text(STR_CODEC_FLAC);
        case ENC_ACODEC_AC3:    return text(STR_CODEC_AC3);
        case ENC_ACODEC_EAC3:   return text(STR_CODEC_EAC3);
        case ENC_ACODEC_VORBIS: return text(STR_CODEC_VORBIS);
        default:                return text(STR_VALUE_UNKNOWN);
    }
}

static const wchar_t *encode_video_encoder_name(void)
{
    int enc;
    if (g_enc_opt.video_codec == ENC_VCODEC_H264) return text(STR_ENC_X264);
    if (g_enc_opt.encoder_override >= 0) {
        return hw_encoder_name(g_enc_opt.encoder_override);
    }
    enc = g_quality_has_result ? g_est.encoder
                               : estimate_choose_encoder(&g_info, &g_hw_info);
    return hw_encoder_name(enc);
}

static const wchar_t *encode_error_text(EncodeError err)
{
    switch (err) {
        case ENCODE_ERR_TOOL:    return text(STR_COMP_ERR_TOOL);
        case ENCODE_ERR_CODEC:   return text(STR_COMP_ERR_CODEC);
        case ENCODE_ERR_AUDIO:   return text(STR_COMP_ERR_AUDIO);
        case ENCODE_ERR_AUDIO_LAYOUT: return text(STR_COMP_ERR_AUDIO_LAYOUT);
        case ENCODE_ERR_SUBS:    return text(STR_COMP_ERR_SUBS);
        case ENCODE_ERR_PATH:    return text(STR_COMP_ERR_PATH);
        case ENCODE_ERR_FOLDER:  return text(STR_COMP_ERR_FOLDER);
        case ENCODE_ERR_START:   return text(STR_COMP_ERR_START);
        case ENCODE_ERR_FFMPEG:  return text(STR_COMP_ERR_FFMPEG);
        case ENCODE_ERR_VERIFY:  return text(STR_COMP_ERR_VERIFY);
        case ENCODE_ERR_RENAME:  return text(STR_COMP_ERR_RENAME);
        case ENCODE_ERR_OUT_OF_MEMORY: return text(STR_COMP_ERR_MEMORY);
        default:                 return text(STR_COMP_FAILED);
    }
}

static void bitrate_button_text(wchar_t *out, size_t cap)
{
    if (!out || cap == 0) return;
    if (!encode_audio_has_bitrate(g_enc_opt.audio_codec)) {
        wcsncpy(out, g_enc_opt.audio_codec == ENC_ACODEC_COPY
                    ? text(STR_COMP_NO_BITRATE_COPY)
                    : text(STR_COMP_NO_BITRATE_FLAC), cap - 1);
        out[cap - 1] = L'\0';
        return;
    }
    _snwprintf(out, cap, text(STR_COMP_BITRATE_VALUE_FMT),
               bitrate_preset_name(g_enc_opt.bitrate),
               encode_bitrate_kbps(&g_enc_opt));
    out[cap - 1] = L'\0';
}

static void update_audio_dropdown_items(void)
{
    int codec, i = 0;
    for (codec = 0; codec < ENC_ACODEC_COUNT; codec++) {
        if (!encode_audio_available(g_enc_opt.container, codec)) continue;
        if (codec == ENC_ACODEC_COPY &&
            !encode_audio_copy_available_sel(&g_info, &g_enc_opt,
                                             g_enc_opt.container)) continue;
        wcsncpy(g_audio_items[i], encode_audio_codec_label(codec), 47);
        g_audio_items[i][47] = L'\0';
        i++;
    }
}

static void update_bitrate_dropdown_items(void)
{
    int n = 0;
    if (g_enc_opt.audio_codec == ENC_ACODEC_AC3 ||
        g_enc_opt.audio_codec == ENC_ACODEC_EAC3) {
        wcsncpy(g_bitrate_items[n++], text(STR_COMP_BITRATE_HIGH), 47);
    } else {
        wcsncpy(g_bitrate_items[n++], text(STR_COMP_BITRATE_VOICE), 47);
        wcsncpy(g_bitrate_items[n++], text(STR_COMP_BITRATE_MUSIC), 47);
        wcsncpy(g_bitrate_items[n++], text(STR_COMP_BITRATE_HIGH), 47);
    }
    {
        wchar_t custom[48];
        _snwprintf(custom, 48, L"%ls %d kbps",
                   text(STR_COMP_BITRATE_CUSTOM),
                   g_enc_opt.bitrate == ENC_ABITRATE_CUSTOM
                       ? g_enc_opt.custom_kbps : ENC_BITRATE_HIGH_KBPS);
        custom[47] = L'\0';
        wcsncpy(g_bitrate_items[n++], custom, 47);
    }
    while (n < 4) {
        g_bitrate_items[n][0] = L'\0';
        n++;
    }
}

static double selected_video_source_bps(void)
{
    int vt = encode_selected_video_track(&g_info, &g_enc_opt);
    long long bps = 0;
    if (g_info.video_tracks_stored > 0 && vt >= 0 &&
        vt < g_info.video_tracks_stored) {
        bps = g_info.video_tracks[vt].bitrate;
    }
    if (bps <= 0) bps = g_info.video_bitrate;
    if (bps <= 0) bps = g_info.format_bitrate;
    return (double)bps;
}

static int preset_estimate_applies(void)
{
    if (!g_quality_has_result) return 0;
    if (g_enc_options_ready) {
        if (g_enc_opt.crf_override > 0) return 0;
        if (g_enc_opt.video_codec != ENC_VCODEC_HEVC) return 0;
        if (g_enc_opt.encoder_override >= 0 &&
            g_enc_opt.encoder_override != g_est.encoder) {
            return 0;
        }
        if (g_info.video_primary >= 0 &&
            encode_selected_video_track(&g_info, &g_enc_opt) != g_info.video_primary) {
            return 0;
        }
    }
    return 1;
}

static double compress_video_bytes(void)
{
    EstimateLevel *sel;
    if (g_custom_est_ready) return g_custom_est.video_size_bytes;
    if (!preset_estimate_applies()) return 0.0;
    sel = quality_level_result(g_quality_selected);
    return (sel && sel->available) ? sel->video_size_bytes : 0.0;
}

static double compress_video_seconds(void)
{
    EstimateLevel *sel;
    if (g_custom_est_ready) return g_custom_est.est_seconds;
    if (!preset_estimate_applies()) return 0.0;
    sel = quality_level_result(g_quality_selected);
    return (sel && sel->available) ? sel->est_seconds : 0.0;
}

static double compress_audio_bytes(void)
{
    long long bps = encode_estimated_audio_bitrate(&g_info, &g_enc_opt);
    if (bps <= 0 || g_info.duration <= 0.0) return 0.0;
    return (double)bps * g_info.duration / 8.0 * 1.01;
}

static void encode_ui_refresh_status_estimate(void)
{
    wchar_t size[64], time[64], buf[256];
    double video = compress_video_bytes();
    double audio = compress_audio_bytes();
    double seconds = compress_video_seconds();

    if (g_encode_state != 0) return;
    if (g_custom_est_testing) {
        _snwprintf(buf, 256, text(STR_COMP_ADV_TESTING_FMT),
                   g_custom_est_progress.clip_total > 0
                       ? g_custom_est_progress.clip_index * 100 /
                         g_custom_est_progress.clip_total : 0);
        buf[255] = L'\0';
        set_status_text(buf, 0);
        return;
    }
    if (video <= 0.0) {
        set_status_id(STR_COMP_HEADING, 0);
        return;
    }
    util_format_bytes((long long)(video + audio), size, 64);
    util_format_duration(seconds, time, 64);
    _snwprintf(buf, 256, text(STR_COMP_TOTAL_FMT), size, time);
    buf[255] = L'\0';
    set_status_text(buf, 0);
}

static void draw_compress_settings(HDC hdc)
{
    wchar_t buf[1024];
    wchar_t tmp[512];
    wchar_t path[UTIL_PATH_MAX];
    wchar_t src_size[64], dst_size[64], src_br[64], dst_br[64];
    wchar_t audio_src[64], audio_dst[64], audio_desc[96];
    RECT r;
    int has_audio = g_info.audio_track_count > 0 && !g_enc_opt.audio_none;
    int has_bitrate = has_audio && encode_audio_has_bitrate(g_enc_opt.audio_codec);
    int downmix_visible = has_bitrate && encode_any_downmix(&g_info, &g_enc_opt);
    int selected_audio = encode_selected_audio_count(&g_info, &g_enc_opt);
    int total_audio = g_info.audio_track_count;
    double video_bytes = compress_video_bytes();
    double src_video_bps = selected_video_source_bps();
    double dst_video_bps = (video_bytes > 0.0 && g_info.duration > 0.0)
        ? video_bytes * 8.0 / g_info.duration : 0.0;
    const wchar_t *path_note = text(STR_COMP_OUTPUT_UNKNOWN);

    SetRect(&r, S(32), S(108), S(614), S(128));
    gb_text(hdc, g_fontBold, GB_DARKEST, &r, text(STR_COMP_HEADING),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (g_enc_opt.crf_override > 0) {
        if (g_enc_opt.encoder_override == HW_ENC_X265 &&
            g_enc_opt.crf_override == 19) {
            wcsncpy(buf, text(STR_COMP_ADV_Q_ARCHIVE), 1023);
        } else {
            _snwprintf(buf, 1024, text(STR_COMP_CUSTOM_CRF_FMT),
                       g_enc_opt.crf_override, encode_video_encoder_name());
        }
    } else {
        _snwprintf(buf, 1024, text(STR_COMP_QUALITY_FMT),
                   presets_level_name(g_quality_selected),
                   encode_video_encoder_name());
    }
    buf[1023] = L'\0';
    SetRect(&r, S(360), S(108), S(614), S(128));
    gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (g_custom_est_testing) {
        _snwprintf(buf, 1024, text(STR_COMP_ADV_TESTING_FMT),
                   g_custom_est_progress.clip_total > 0
                       ? g_custom_est_progress.clip_index * 100 /
                         g_custom_est_progress.clip_total : 0);
        buf[1023] = L'\0';
        SetRect(&r, S(32), S(126), S(614), S(142));
        gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else if (video_bytes > 0.0) {
        util_format_bytes((long long)(src_video_bps * g_info.duration / 8.0),
                          src_size, 64);
        util_format_bytes((long long)video_bytes, dst_size, 64);
        util_format_bitrate((long long)src_video_bps, src_br, 64);
        util_format_bitrate((long long)dst_video_bps, dst_br, 64);
        _snwprintf(buf, 1024, text(STR_COMP_VIDEO_BREAKDOWN_FMT),
                   src_size, video_codec_label(g_info.video_codec), src_br,
                   dst_size,
                   text(g_enc_opt.video_codec == ENC_VCODEC_H264
                        ? STR_CODEC_H264 : STR_CODEC_HEVC),
                   dst_br);
        buf[1023] = L'\0';
        SetRect(&r, S(32), S(126), S(614), S(142));
        gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        wcsncpy(buf, text(STR_QUALITY_NO_ESTIMATE), 1023);
        buf[1023] = L'\0';
        SetRect(&r, S(32), S(126), S(614), S(142));
        gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SetRect(&r, S(32), S(150), S(104), S(178));
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, text(STR_COMP_FORMAT_LABEL),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&r, S(32), S(188), S(104), S(216));
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, text(STR_COMP_VIDEO_LABEL),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&r, S(32), S(226), S(104), S(254));
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, text(STR_COMP_AUDIO_LABEL),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (!has_audio) {
        SetRect(&r, S(110), S(226), S(614), S(254));
        gb_text(hdc, g_fontSmall, GB_DARK, &r,
                g_info.audio_track_count > 0
                    ? text(STR_COMP_AUDIO_NONE_SELECTED)
                    : text(STR_COMP_AUDIO_NONE),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else if (!has_bitrate) {
        SetRect(&r, S(292), S(226), S(614), S(254));
        gb_text(hdc, g_fontSmall, GB_DARK, &r,
                g_enc_opt.audio_codec == ENC_ACODEC_COPY
                    ? text(STR_COMP_NO_BITRATE_COPY)
                    : text(STR_COMP_NO_BITRATE_FLAC),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (encode_preview_output_path(&g_info, g_enc_opt.container,
                                   path, UTIL_PATH_MAX)) {
        _snwprintf(buf, 1024, text(STR_COMP_OUTPUT_FMT), path);
        path_note = NULL;
    } else {
        buf[0] = L'\0';
    }
    SetRect(&r, S(32), S(278), S(614), S(296));
    if (path_note) {
        gb_text(hdc, g_fontSmall, GB_DARK, &r, path_note,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
    }

    /* Audio summary: selected source size -> estimated output. */
    if (has_audio) {
        long long src_bps = encode_selected_audio_bitrate(&g_info, &g_enc_opt);
        long long dst_bps = encode_estimated_audio_bitrate(&g_info, &g_enc_opt);
        util_format_bytes((long long)((double)src_bps * g_info.duration / 8.0),
                          audio_src, 64);
        util_format_bytes((long long)((double)dst_bps * g_info.duration / 8.0 * 1.01),
                          audio_dst, 64);
        if (g_enc_opt.audio_codec == ENC_ACODEC_COPY) {
            wcsncpy(audio_desc, text(STR_COMP_AUDIO_COPY_LABEL), 95);
        } else if (g_enc_opt.audio_codec == ENC_ACODEC_FLAC) {
            wcsncpy(audio_desc, text(STR_CODEC_FLAC), 95);
        } else {
            _snwprintf(audio_desc, 96, L"%ls %d kbps",
                       encode_audio_codec_label(g_enc_opt.audio_codec),
                       encode_bitrate_kbps(&g_enc_opt));
        }
        audio_desc[95] = L'\0';
        _snwprintf(buf, 1024, text(STR_COMP_AUDIO_BREAKDOWN_FMT),
                   audio_src, audio_dst, audio_desc);
        buf[1023] = L'\0';
        SetRect(&r, S(32), S(299), S(614), S(315));
        gb_text(hdc, g_fontSmall, GB_DARKEST, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    /* Subtitle summary. */
    {
        int selected_subs = encode_selected_subtitle_count(&g_info, &g_enc_opt,
                                                           g_enc_opt.container);
        int show_button = g_info.subtitle_track_count > 0;
        SetRect(&r, S(32), S(314), S(104), S(338));
        gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, text(STR_COMP_SUBS_LABEL),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (g_info.subtitle_track_count <= 0) {
            wcsncpy(buf, text(STR_COMP_SUBS_NONE), 1023);
        } else if (g_enc_opt.subtitles_none || selected_subs <= 0) {
            wcsncpy(buf, text(STR_COMP_TRACK_NO_SUBS), 1023);
        } else if (g_enc_opt.container == ENC_CONT_MKV) {
            _snwprintf(buf, 1024, text(STR_COMP_SUBS_SELECTED_FMT),
                       selected_subs, g_info.subtitle_track_count);
        } else {
            int text_subs = encode_selected_text_subtitle_count(&g_info, &g_enc_opt);
            int bitmap_subs = encode_selected_bitmap_subtitle_count(&g_info, &g_enc_opt);
            buf[0] = L'\0';
            if (text_subs > 0) {
                _snwprintf(tmp, 512, text(STR_COMP_SUBS_CONVERT_FMT), text_subs);
                tmp[511] = L'\0';
                wcscat(buf, tmp);
            }
            if (bitmap_subs > 0) {
                _snwprintf(tmp, 512, text(STR_COMP_SUBS_DROP_FMT), bitmap_subs);
                tmp[511] = L'\0';
                if (buf[0]) wcscat(buf, L" ");
                wcscat(buf, tmp);
            }
        }
        buf[1023] = L'\0';
        SetRect(&r, S(show_button ? 340 : 110), S(314), S(614), S(338));
        gb_text(hdc, g_fontSmall, GB_DARKEST, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    /* Context note: HDR conversion, track selection, downmix, or multi-video. */
    if (g_info.is_hdr && g_enc_opt.video_codec == ENC_VCODEC_H264) {
        wcsncpy(buf, text(STR_COMP_HDR_TONEMAP), 1023);
        buf[1023] = L'\0';
        SetRect(&r, S(32), S(340), S(614), S(354));
        gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else if (g_enc_opt.audio_none) {
        wcsncpy(buf, text(STR_COMP_AUDIO_NONE_SELECTED), 1023);
        buf[1023] = L'\0';
        SetRect(&r, S(32), S(340), S(614), S(354));
        gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else if (selected_audio < total_audio) {
        wcsncpy(buf, text(STR_COMP_TRACK_SELECTED_NOTE), 1023);
        buf[1023] = L'\0';
        SetRect(&r, S(32), S(340), S(614), S(354));
        gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else if (downmix_visible) {
        wcsncpy(buf, text(STR_COMP_DOWNMIX_NOTE), 1023);
        buf[1023] = L'\0';
        SetRect(&r, S(32), S(340), S(614), S(354));
        gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else if (g_info.video_real_count > 1) {
        _snwprintf(buf, 1024, text(STR_COMP_MULTI_VIDEO_NOTE),
                   g_info.video_real_count);
        buf[1023] = L'\0';
        SetRect(&r, S(32), S(340), S(614), S(354));
        gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    SetRect(&r, S(32), S(356), S(614), S(374));
    gb_text(hdc, g_fontSmall, GB_DARK, &r,
            g_enc_opt.container == ENC_CONT_MKV
                ? text(STR_COMP_OPUS_NOTE)
                : text(STR_COMP_HINT),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void draw_compress_progress(HDC hdc)
{
    wchar_t buf[512];
    RECT r, bar;
    int percent = (int)(g_encode_progress.fraction * 100.0);
    int elapsed = (int)((GetTickCount64() - g_encode_start_tick) / 1000);
    int total = 0;
    double estimated = compress_video_seconds();

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (estimated > 0.0) total = (int)(estimated + 0.5);

    SetRect(&r, S(32), S(112), S(614), S(132));
    gb_text(hdc, g_fontBold, GB_DARKEST, &r,
            text(g_encode_progress.verifying ? STR_COMP_VERIFYING
                                             : STR_COMP_PROGRESS_HEADING),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&r, S(32), S(138), S(614), S(162));
    gb_text(hdc, g_fontSmall, GB_DARKEST, &r, g_info.file_name,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SetRect(&bar, S(32), S(176), S(614), S(192));
    draw_progress_bar(hdc, &bar, percent);

    _snwprintf(buf, 512, text(STR_COMP_PROGRESS_FMT), percent);
    buf[511] = L'\0';
    SetRect(&r, S(32), S(200), S(614), S(224));
    gb_text(hdc, g_fontBold, GB_DARKEST, &r, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (total > 0) {
        _snwprintf(buf, 512, text(STR_COMP_ELAPSED_FMT), elapsed, total);
    } else {
        _snwprintf(buf, 512, text(STR_HW_ELAPSED_FMT), elapsed);
    }
    buf[511] = L'\0';
    SetRect(&r, S(32), S(228), S(614), S(248));
    gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (g_encode_progress.speed > 0.0) {
        _snwprintf(buf, 512, L"%.2fx", g_encode_progress.speed);
        buf[511] = L'\0';
        SetRect(&r, S(32), S(250), S(614), S(268));
        gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SetRect(&r, S(32), S(300), S(614), S(352));
    gb_text(hdc, g_fontSmall, GB_DARK, &r, text(STR_COMP_PROGRESS_NOTE),
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

static void draw_compress_done(HDC hdc)
{
    wchar_t buf[1024];
    wchar_t size[64];
    wchar_t folder[UTIL_PATH_MAX];
    RECT r;

    SetRect(&r, S(32), S(112), S(614), S(132));
    gb_text(hdc, g_fontBold, GB_DARKEST, &r, text(STR_COMP_DONE_HEADING),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    util_dirname(g_encode_output, folder, UTIL_PATH_MAX);
    _snwprintf(buf, 1024, text(STR_COMP_DONE_FOLDER_FMT), folder);
    buf[1023] = L'\0';
    SetRect(&r, S(32), S(140), S(614), S(158));
    gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);

    _snwprintf(buf, 1024, text(STR_COMP_DONE_FILE_FMT),
               util_basename(g_encode_output));
    buf[1023] = L'\0';
    SetRect(&r, S(32), S(160), S(614), S(180));
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    util_format_bytes(g_encode_size, size, 64);
    _snwprintf(buf, 1024, text(STR_COMP_DONE_SIZE_FMT), size);
    buf[1023] = L'\0';
    SetRect(&r, S(32), S(190), S(614), S(212));
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, buf,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&r, S(32), S(220), S(614), S(258));
    gb_text(hdc, g_fontSmall, GB_DARK, &r, text(STR_COMP_DONE_VERIFIED),
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    SetRect(&r, S(32), S(286), S(614), S(304));
    gb_text(hdc, g_fontSmall, GB_DARK, &r, text(STR_COMP_DONE_NOTE),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void draw_compress_error(HDC hdc)
{
    wchar_t buf[1024];
    RECT r;

    SetRect(&r, S(32), S(112), S(614), S(134));
    gb_text(hdc, g_fontBold, GB_DARKEST, &r, text(STR_COMP_FAILED),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&r, S(32), S(146), S(614), S(196));
    gb_text(hdc, g_fontSmall, GB_DARKEST, &r, encode_error_text(g_encode_error_code),
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    if (g_encode_detail[0]) {
        _snwprintf(buf, 1024, text(STR_COMP_DETAIL_FMT), g_encode_detail);
        buf[1023] = L'\0';
        SetRect(&r, S(32), S(206), S(614), S(300));
        gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    SetRect(&r, S(32), S(316), S(614), S(348));
    gb_text(hdc, g_fontSmall, GB_DARK, &r, text(STR_COMP_PROGRESS_NOTE),
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

static void draw_custom_input(HDC hdc)
{
    wchar_t buf[256];
    RECT r;
    int min_kbps = encode_audio_min_kbps(g_enc_opt.audio_codec);

    SetRect(&r, S(32), S(108), S(614), S(130));
    gb_text(hdc, g_fontBold, GB_DARKEST, &r, text(STR_COMP_CUSTOM_HEADING),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    _snwprintf(buf, 256, text(STR_COMP_CUSTOM_FMT), min_kbps,
               ENC_BITRATE_MAX_KBPS);
    buf[255] = L'\0';
    SetRect(&r, S(32), S(140), S(614), S(180));
    gb_text(hdc, g_fontSmall, GB_DARK, &r, buf,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    SetRect(&r, S(106), S(186), S(334), S(222));
    gb_frame(hdc, &r, GB_DARKEST, S(2));
}

static void draw_advanced_settings(HDC hdc)
{
    wchar_t buf[512];
    RECT r;
    int percent = 0;

    SetRect(&r, S(32), S(108), S(614), S(130));
    gb_text(hdc, g_fontBold, GB_DARKEST, &r, text(STR_COMP_ADV_HEADING),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&r, S(32), S(146), S(196), S(176));
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, text(STR_COMP_ADV_ENCODER),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&r, S(32), S(186), S(196), S(216));
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, text(STR_COMP_ADV_QUALITY),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&r, S(32), S(226), S(196), S(256));
    gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, text(STR_COMP_ADV_CRF),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetRect(&r, S(32), S(262), S(614), S(300));
    gb_text(hdc, g_fontSmall, GB_DARK, &r, text(STR_COMP_ADV_NOTE),
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    if (g_custom_est_testing) {
        if (g_custom_est_progress.clip_total > 0) {
            percent = g_custom_est_progress.clip_index * 100 /
                      g_custom_est_progress.clip_total;
        }
        _snwprintf(buf, 512, text(STR_COMP_ADV_TESTING_FMT), percent);
        buf[511] = L'\0';
        SetRect(&r, S(32), S(300), S(614), S(318));
        gb_text(hdc, g_fontSmallBold, GB_DARKEST, &r, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static void paint_main(HWND hWnd, HDC hdc)
{
    RECT rc;
    RECT inner;
    RECT title_rc;
    int cw;

    GetClientRect(hWnd, &rc);
    cw = rc.right;

    gb_fill(hdc, &rc, GB_DARK);
    gb_frame(hdc, &rc, GB_DARKEST, S(6));
    inner = rc;
    InflateRect(&inner, -S(6), -S(6));
    gb_frame(hdc, &inner, GB_LIGHTEST, S(1));

    gb_icon(hdc, S(22), S(14), S(2), 0);
    title_rc.left = S(22) + S(24) + S(10);
    title_rc.top = S(10);
    title_rc.right = cw - S(90);
    title_rc.bottom = S(38);
    gb_text(hdc, g_fontTitle, GB_LIGHTEST, &title_rc, text(STR_APP_NAME),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    {
        RECT divider = { S(12), S(43), cw - S(12), S(44) };
        gb_fill(hdc, &divider, GB_DARKEST);
    }

    draw_steps(hdc, cw);
    if (g_advanced_active) {
        draw_panel(hdc, &g_rc_panel, text(STR_COMP_ADV_TITLE));
        draw_advanced_settings(hdc);
    } else if (g_input_active) {
        draw_panel(hdc, &g_rc_panel, text(STR_COMP_CUSTOM_TITLE));
        draw_custom_input(hdc);
    } else if (g_hw_testing) {
        draw_panel(hdc, &g_rc_panel, text(STR_HW_PANEL));
        draw_hw_test(hdc);
    } else if (g_step == 2) {
        draw_panel(hdc, &g_rc_panel, text(STR_COMP_PANEL));
        if (g_encode_state == 1) draw_compress_progress(hdc);
        else if (g_encode_state == 2) draw_compress_done(hdc);
        else if (g_encode_state == 3) draw_compress_error(hdc);
        else draw_compress_settings(hdc);
    } else if (g_step == 1) {
        if (g_quality_preview && !g_quality_testing) {
            draw_panel(hdc, &g_rc_panel, text(STR_QUALITY_PREVIEW_PANEL));
            draw_quality_preview(hdc);
        } else {
            draw_panel(hdc, &g_rc_panel, text(STR_QUALITY_PANEL));
            if (g_quality_testing) draw_quality_progress(hdc);
            else draw_quality(hdc);
        }
    } else {
        draw_panel(hdc, &g_rc_panel, text(STR_PANEL_VIDEO));
        if (g_probing) {
            draw_probing(hdc);
        } else if (g_has_info) {
            if (g_show_details) draw_details(hdc, &g_info);
            else draw_summary(hdc, &g_info);
        } else {
            draw_empty(hdc, NULL);
        }
    }

    draw_status_strip(hdc);
}

/* ------------------------------------------------------------------ layout */
static void layout_controls(void)
{
    RECT rc;
    int cw;
    int btn_w = S(140);
    int btn_h = S(38);

    if (!g_hWnd) return;
    GetClientRect(g_hWnd, &rc);
    cw = rc.right;

    SetRect(&g_rc_panel, S(20), S(82), cw - S(20), S(378));
    SetRect(&g_rc_drop, S(32), S(112), S(614), S(292));
    SetRect(&g_rc_status, S(20), S(434), cw - S(20), S(470));

    SetWindowPos(g_hBtnChoose, NULL, S(32), S(388), btn_w, btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnRetest, NULL, S(180), S(388), S(110), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnDepth, NULL, S(298), S(388), S(170), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnContinue, NULL, cw - S(32) - btn_w, S(388), btn_w, btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnSkip, NULL, cw - S(32) - S(160), S(388), S(160), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnBack, NULL, S(32), S(388), S(140), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnPreview, NULL, S(180), S(388), S(110), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnTestAgain, NULL, S(298), S(388), S(130), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnQCancel, NULL, cw - S(32) - S(170), S(388), S(170), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnCompress, NULL, cw - S(32) - S(140), S(388), S(140), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnDetails, NULL, S(32), S(340), S(150), S(26), SWP_NOZORDER);

    SetWindowPos(g_hBtnContMkv, NULL, S(110), S(146), S(70), S(28), SWP_NOZORDER);
    SetWindowPos(g_hBtnContMp4, NULL, S(186), S(146), S(70), S(28), SWP_NOZORDER);
    SetWindowPos(g_hBtnVcHevc, NULL, S(110), S(184), S(150), S(28), SWP_NOZORDER);
    SetWindowPos(g_hBtnVcH264, NULL, S(266), S(184), S(150), S(28), SWP_NOZORDER);
    SetWindowPos(g_hBtnAudio, NULL, S(110), S(222), S(170), S(30), SWP_NOZORDER);
    SetWindowPos(g_hBtnABitrate, NULL, S(292), S(222), S(170), S(30), SWP_NOZORDER);
    SetWindowPos(g_hBtnAudioTrack, NULL, S(468), S(222), S(146), S(30), SWP_NOZORDER);
    SetWindowPos(g_hBtnVideoTrack, NULL, S(422), S(184), S(192), S(28), SWP_NOZORDER);
    SetWindowPos(g_hBtnSubtitleTrack, NULL, S(110), S(314), S(220), S(24), SWP_NOZORDER);
    SetWindowPos(g_hBtnDownmix, NULL, S(110), S(258), S(220), S(26), SWP_NOZORDER);
    SetWindowPos(g_hBtnAdvanced, NULL, cw - S(32) - S(180) - S(8) - S(140),
                 S(388), S(140), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnAdvEnc, NULL, S(200), S(144), S(300), S(30), SWP_NOZORDER);
    SetWindowPos(g_hBtnAdvQ, NULL, S(200), S(184), S(300), S(30), SWP_NOZORDER);
    SetWindowPos(g_hEditCrf, NULL, S(200), S(224), S(120), S(28), SWP_NOZORDER);
    SetWindowPos(g_hBtnAdvOk, NULL, S(200), S(320), S(180), S(34), SWP_NOZORDER);
    SetWindowPos(g_hBtnAdvCancel, NULL, S(388), S(320), S(120), S(34), SWP_NOZORDER);
    SetWindowPos(g_hBtnStart, NULL, cw - S(32) - S(180), S(388), S(180), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnECancel, NULL, cw - S(32) - S(170), S(388), S(170), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hBtnShow, NULL, cw - S(32) - S(180), S(388), S(180), btn_h, SWP_NOZORDER);
    SetWindowPos(g_hEditCustom, NULL, S(110), S(190), S(220), S(28), SWP_NOZORDER);
    SetWindowPos(g_hBtnInputOk, NULL, S(110), S(250), S(100), S(32), SWP_NOZORDER);
    SetWindowPos(g_hBtnInputCancel, NULL, S(222), S(250), S(110), S(32), SWP_NOZORDER);

    {
        int close_w = S(26);
        int close_h = S(24);
        int close_x = cw - S(14) - close_w;
        int min_x = close_x - S(6) - close_w;
        SetWindowPos(g_hBtnClose, NULL, close_x, S(9), close_w, close_h, SWP_NOZORDER);
        SetWindowPos(g_hBtnMin, NULL, min_x, S(9), close_w, close_h, SWP_NOZORDER);
    }

    gb_round_window(g_hWnd, 1);
    InvalidateRect(g_hWnd, NULL, TRUE);
}

static void hide_encode_controls(void)
{
    ShowWindow(g_hBtnContMkv, SW_HIDE);
    ShowWindow(g_hBtnContMp4, SW_HIDE);
    ShowWindow(g_hBtnVcHevc, SW_HIDE);
    ShowWindow(g_hBtnVcH264, SW_HIDE);
    ShowWindow(g_hBtnAudio, SW_HIDE);
    ShowWindow(g_hBtnABitrate, SW_HIDE);
    ShowWindow(g_hBtnDownmix, SW_HIDE);
    ShowWindow(g_hBtnAudioTrack, SW_HIDE);
    ShowWindow(g_hBtnVideoTrack, SW_HIDE);
    ShowWindow(g_hBtnSubtitleTrack, SW_HIDE);
    ShowWindow(g_hBtnAdvanced, SW_HIDE);
    ShowWindow(g_hBtnAdvEnc, SW_HIDE);
    ShowWindow(g_hBtnAdvQ, SW_HIDE);
    ShowWindow(g_hEditCrf, SW_HIDE);
    ShowWindow(g_hBtnAdvOk, SW_HIDE);
    ShowWindow(g_hBtnAdvCancel, SW_HIDE);
    ShowWindow(g_hBtnStart, SW_HIDE);
    ShowWindow(g_hBtnECancel, SW_HIDE);
    ShowWindow(g_hBtnShow, SW_HIDE);
    ShowWindow(g_hBtnInputOk, SW_HIDE);
    ShowWindow(g_hBtnInputCancel, SW_HIDE);
    ShowWindow(g_hEditCustom, SW_HIDE);
}

static void update_controls_visibility(void)
{
    if (!g_hWnd) return;
    hide_encode_controls();

    if (g_advanced_active) {
        ShowWindow(g_hBtnChoose, SW_HIDE);
        ShowWindow(g_hBtnContinue, SW_HIDE);
        ShowWindow(g_hBtnDetails, SW_HIDE);
        ShowWindow(g_hBtnRetest, SW_HIDE);
        ShowWindow(g_hBtnDepth, SW_HIDE);
        ShowWindow(g_hBtnSkip, SW_HIDE);
        ShowWindow(g_hBtnBack, SW_HIDE);
        ShowWindow(g_hBtnTestAgain, SW_HIDE);
        ShowWindow(g_hBtnQCancel, SW_HIDE);
        ShowWindow(g_hBtnCompress, SW_HIDE);
        ShowWindow(g_hBtnPreview, SW_HIDE);
        ShowWindow(g_hBtnStart, SW_HIDE);
        ShowWindow(g_hBtnECancel, SW_HIDE);
        ShowWindow(g_hBtnShow, SW_HIDE);
        ShowWindow(g_hBtnAdvEnc, SW_SHOW);
        ShowWindow(g_hBtnAdvQ, SW_SHOW);
        ShowWindow(g_hBtnAdvOk, SW_SHOW);
        ShowWindow(g_hBtnAdvCancel, SW_SHOW);
        if (g_adv_quality == 5 || g_adv_quality == 6) {
            ShowWindow(g_hEditCrf, SW_SHOW);
            EnableWindow(g_hEditCrf, g_adv_quality == 6 ? TRUE : FALSE);
        }
        encode_ui_update_adv_labels();
        return;
    }

    if (g_input_active) {
        ShowWindow(g_hBtnChoose, SW_HIDE);
        ShowWindow(g_hBtnContinue, SW_HIDE);
        ShowWindow(g_hBtnDetails, SW_HIDE);
        ShowWindow(g_hBtnRetest, SW_HIDE);
        ShowWindow(g_hBtnDepth, SW_HIDE);
        ShowWindow(g_hBtnSkip, SW_HIDE);
        ShowWindow(g_hBtnBack, SW_HIDE);
        ShowWindow(g_hBtnTestAgain, SW_HIDE);
        ShowWindow(g_hBtnQCancel, SW_HIDE);
        ShowWindow(g_hBtnCompress, SW_HIDE);
        ShowWindow(g_hBtnPreview, SW_HIDE);
        ShowWindow(g_hEditCustom, SW_SHOW);
        ShowWindow(g_hBtnInputOk, SW_SHOW);
        ShowWindow(g_hBtnInputCancel, SW_SHOW);
        SetFocus(g_hEditCustom);
    } else if (g_hw_testing) {
        ShowWindow(g_hBtnChoose, SW_HIDE);
        ShowWindow(g_hBtnContinue, SW_HIDE);
        ShowWindow(g_hBtnDetails, SW_HIDE);
        ShowWindow(g_hBtnRetest, SW_HIDE);
        ShowWindow(g_hBtnDepth, SW_HIDE);
        ShowWindow(g_hBtnBack, SW_HIDE);
        ShowWindow(g_hBtnTestAgain, SW_HIDE);
        ShowWindow(g_hBtnQCancel, SW_HIDE);
        ShowWindow(g_hBtnCompress, SW_HIDE);
        ShowWindow(g_hBtnPreview, SW_HIDE);
        ShowWindow(g_hBtnSkip, SW_SHOW);
        EnableWindow(g_hBtnSkip, g_hw_skipped ? FALSE : TRUE);
    } else if (g_step == 2) {
        encode_ui_update_controls();
    } else if (g_step == 1) {
        ShowWindow(g_hBtnChoose, SW_HIDE);
        ShowWindow(g_hBtnContinue, SW_HIDE);
        ShowWindow(g_hBtnDetails, SW_HIDE);
        ShowWindow(g_hBtnRetest, SW_HIDE);
        ShowWindow(g_hBtnDepth, SW_HIDE);
        ShowWindow(g_hBtnSkip, SW_HIDE);
        if (g_quality_testing) {
            ShowWindow(g_hBtnBack, SW_HIDE);
            ShowWindow(g_hBtnTestAgain, SW_HIDE);
            ShowWindow(g_hBtnPreview, SW_HIDE);
            ShowWindow(g_hBtnCompress, SW_HIDE);
            ShowWindow(g_hBtnQCancel, SW_SHOW);
            EnableWindow(g_hBtnQCancel, TRUE);
        } else {
            int idx = quality_level_index(g_quality_selected);
            int preview_ok = (g_quality_has_result && idx >= 0 && g_est.preview_available[idx]);
            ShowWindow(g_hBtnBack, SW_SHOW);
            ShowWindow(g_hBtnTestAgain, SW_SHOW);
            ShowWindow(g_hBtnQCancel, SW_HIDE);
            ShowWindow(g_hBtnCompress, SW_SHOW);
            SetWindowTextW(g_hBtnBack, text(g_quality_preview ? STR_QUALITY_BACK_CARDS : STR_QUALITY_BACK));
            if (g_quality_preview) {
                ShowWindow(g_hBtnPreview, SW_HIDE);
            } else {
                ShowWindow(g_hBtnPreview, SW_SHOW);
                EnableWindow(g_hBtnPreview, preview_ok ? TRUE : FALSE);
            }
            EnableWindow(g_hBtnBack, TRUE);
            EnableWindow(g_hBtnTestAgain, TRUE);
            EnableWindow(g_hBtnCompress, g_quality_has_result ? TRUE : FALSE);
        }
    } else {
        ShowWindow(g_hBtnSkip, SW_HIDE);
        ShowWindow(g_hBtnBack, SW_HIDE);
        ShowWindow(g_hBtnTestAgain, SW_HIDE);
        ShowWindow(g_hBtnQCancel, SW_HIDE);
        ShowWindow(g_hBtnCompress, SW_HIDE);
        ShowWindow(g_hBtnPreview, SW_HIDE);
        ShowWindow(g_hBtnChoose, SW_SHOW);
        ShowWindow(g_hBtnContinue, SW_SHOW);
        ShowWindow(g_hBtnRetest, SW_SHOW);
        ShowWindow(g_hBtnDepth, SW_SHOW);
        ShowWindow(g_hBtnDetails, g_has_info ? SW_SHOW : SW_HIDE);
        SetWindowTextW(g_hBtnDetails, text(g_show_details ? STR_BTN_HIDE_DETAILS : STR_BTN_DETAILS));
        EnableWindow(g_hBtnContinue, (g_has_info && !g_probing) ? TRUE : FALSE);
        EnableWindow(g_hBtnChoose, g_probing ? FALSE : TRUE);
        EnableWindow(g_hBtnRetest, g_probing ? FALSE : TRUE);
        EnableWindow(g_hBtnDepth, TRUE);
    }
    InvalidateRect(g_hWnd, NULL, FALSE);
}

/* ------------------------------------------------------------------- probe */
static void on_probe_done(ProbeError error, const SourceInfo *info, void *user)
{
    ProbeDoneMsg *msg;
    (void)user;
    if (!g_hWnd || !IsWindow(g_hWnd)) return;
    msg = (ProbeDoneMsg*)calloc(1, sizeof(ProbeDoneMsg));
    if (!msg) return;
    msg->error = error;
    if (info) msg->info = *info;
    if (!PostMessageW(g_hWnd, WM_APP_PROBE_DONE, 0, (LPARAM)msg)) free(msg);
}

static void start_probe(HWND hWnd, const wchar_t *path)
{
    if (!path || !*path) return;

    if (wcslen(path) >= UTIL_PATH_MAX) {
        set_status_id(STR_PATH_TOO_LONG, 1);
        gb_message_box(hWnd, text(STR_MSG_PROBE_TITLE), text(STR_PATH_TOO_LONG), MB_OK | MB_ICONERROR);
        return;
    }
    if (engine_is_busy()) {
        set_status_id(STR_MSG_BUSY, 1);
        gb_message_box(hWnd, text(STR_MSG_PROBE_TITLE), text(STR_MSG_BUSY), MB_OK | MB_ICONWARNING);
        return;
    }

    g_has_info = 0;
    g_probing = 1;
    g_show_details = 0;
    g_step = 0;
    quality_ui_reset();
    memset(&g_info, 0, sizeof(g_info));
    wcsncpy(g_pending_name, util_basename(path), 255);
    g_pending_name[255] = L'\0';
    set_status_id(STR_STATUS_PROBING, 0);
    update_controls_visibility();

    if (!engine_probe_async(path, on_probe_done, NULL)) {
        g_probing = 0;
        set_status_id(STR_MSG_BUSY, 1);
        update_controls_visibility();
        gb_message_box(hWnd, text(STR_MSG_PROBE_TITLE), text(STR_MSG_BUSY), MB_OK | MB_ICONWARNING);
    }
}

static void hw_ui_refresh_ready_status(void)
{
    wchar_t buf[256];
    if (g_hw_override != HW_PROFILE_AUTO) {
        _snwprintf(buf, 256, text(STR_HW_READY_DEPTH_FMT), hw_profile_name(g_hw_override));
        buf[255] = L'\0';
        set_status_text(buf, 0);
    } else if (g_hw_has_info && g_hw_info.profile >= 0) {
        _snwprintf(buf, 256, text(STR_HW_READY_FMT), hw_profile_name(g_hw_info.profile));
        buf[255] = L'\0';
        set_status_text(buf, 0);
    } else {
        set_status_id(STR_STATUS_READY, 0);
    }
}

static void hw_ui_update_depth_label(void)
{
    wchar_t buf[64];
    _snwprintf(buf, 64, text(STR_BTN_DEPTH_FMT), hw_profile_name(g_hw_override));
    buf[63] = L'\0';
    SetWindowTextW(g_hBtnDepth, buf);
}

static void on_depth_picked(int index, void *user)
{
    (void)user;
    g_hw_override = (index == 0) ? HW_PROFILE_AUTO : (index - 1);
    hw_set_override(g_hw_override);
    hw_ui_update_depth_label();
    hw_ui_refresh_ready_status();
    InvalidateRect(g_hWnd, NULL, FALSE);
}

static void hw_ui_open_depth_dropdown(void)
{
    const wchar_t *ptrs[4];
    RECT anchor;
    int selected;
    int i;

    wcsncpy(g_depth_items[0], text(STR_PROFILE_AUTO), 31);
    wcsncpy(g_depth_items[1], text(STR_PROFILE_LIGHT), 31);
    wcsncpy(g_depth_items[2], text(STR_PROFILE_BALANCED), 31);
    wcsncpy(g_depth_items[3], text(STR_PROFILE_THOROUGH), 31);
    for (i = 0; i < 4; i++) {
        g_depth_items[i][31] = L'\0';
        ptrs[i] = g_depth_items[i];
    }
    selected = (g_hw_override == HW_PROFILE_AUTO) ? 0 : (g_hw_override + 1);

    GetWindowRect(g_hBtnDepth, &anchor);
    MapWindowPoints(NULL, g_hWnd, (POINT*)&anchor, 2);
    gb_dropdown_open(g_hWnd, &anchor, ptrs, 4, selected, on_depth_picked, NULL);
}

static void hw_progress_cb(const HwProgress *p, void *user)
{
    HwProgress *copy;
    HWND hWnd = (HWND)user;
    if (!p || !hWnd) return;
    copy = (HwProgress*)malloc(sizeof(HwProgress));
    if (!copy) return;
    *copy = *p;
    if (!PostMessageW(hWnd, WM_APP_HW_PROGRESS, 0, (LPARAM)copy)) free(copy);
}

typedef struct {
    HWND hwnd;
} HwJob;

static DWORD WINAPI hw_thread_proc(LPVOID param)
{
    HwJob *job = (HwJob*)param;
    HwDoneMsg *msg;
    HwTestResult result;

    msg = (HwDoneMsg*)calloc(1, sizeof(HwDoneMsg));
    if (!msg) {
        free(job);
        return 0;
    }
    result = hw_run_test(&msg->info, hw_progress_cb, job->hwnd, &g_hw_cancel);
    msg->result = result;
    if (!PostMessageW(job->hwnd, WM_APP_HW_DONE, 0, (LPARAM)msg)) free(msg);
    free(job);
    return 0;
}

static void hw_ui_skip(void)
{
    if (!g_hw_testing) return;
    g_hw_skipped = 1;
    g_hw_cancel = 1;
    EnableWindow(g_hBtnSkip, FALSE);
    set_status_text(text(STR_HW_SKIPPED), 0);
}

static void hw_ui_start(HWND hWnd, int auto_test, const wchar_t *pending_path)
{
    HwJob *job;
    HANDLE thread;

    if (g_hw_testing) return;
    (void)auto_test;

    g_hw_testing = 1;
    g_hw_skipped = 0;
    g_hw_error = HW_ERR_NONE;
    memset(&g_hw_progress, 0, sizeof(g_hw_progress));
    g_hw_start_tick = GetTickCount64();
    g_hw_est_seconds = (g_hw_has_info && g_hw_info.total_seconds > 0.0)
        ? (int)(g_hw_info.total_seconds + 0.5) : 30;
    g_hw_cancel = 0;
    g_hw_pending_path[0] = L'\0';
    if (pending_path && *pending_path) {
        wcsncpy(g_hw_pending_path, pending_path, UTIL_PATH_MAX - 1);
        g_hw_pending_path[UTIL_PATH_MAX - 1] = L'\0';
    }

    set_status_id(STR_STATUS_HW, 0);
    update_controls_visibility();
    SetFocus(g_hBtnSkip);
    InvalidateRect(hWnd, NULL, FALSE);

    job = (HwJob*)calloc(1, sizeof(HwJob));
    if (!job) {
        g_hw_testing = 0;
        set_status_text(text(STR_HW_FAILED), 1);
        update_controls_visibility();
        return;
    }
    job->hwnd = hWnd;
    thread = CreateThread(NULL, 0, hw_thread_proc, job, 0, NULL);
    if (thread) {
        CloseHandle(thread);
    } else {
        free(job);
        g_hw_testing = 0;
        set_status_text(text(STR_HW_FAILED), 1);
        update_controls_visibility();
    }
}

static void hw_ui_on_done(HWND hWnd, HwDoneMsg *msg)
{
    wchar_t buf[256];
    int profile;
    int should_probe = 0;
    wchar_t probe_path[UTIL_PATH_MAX];

    if (!msg) return;
    probe_path[0] = L'\0';

    if (g_hw_skipped || msg->result == HW_TEST_CANCELLED) {
        g_hw_info = msg->info;
        g_hw_info.test_version = HW_TEST_VERSION;
        g_hw_info.profile_raw = HW_PROFILE_LIGHT;
        g_hw_info.profile = HW_PROFILE_LIGHT;
        g_hw_info.skipped = 1;
        g_hw_info.total_seconds = (double)(GetTickCount64() - g_hw_start_tick) / 1000.0;
        hw_stamp_now(&g_hw_info);
        hw_save(&g_hw_info);
        hw_log_result(&g_hw_info);
        g_hw_has_info = 1;
        set_status_text(text(STR_HW_SKIPPED), 0);
        gb_message_box(hWnd, text(STR_HW_PANEL), text(STR_HW_SKIPPED),
                       MB_OK | MB_ICONINFORMATION);
    } else if (msg->result == HW_TEST_ERROR) {
        g_hw_info = msg->info;
        g_hw_info.test_version = 0; /* retry automatically on next launch */
        g_hw_info.profile_raw = HW_PROFILE_LIGHT;
        g_hw_info.profile = HW_PROFILE_LIGHT;
        g_hw_info.skipped = 1;
        g_hw_info.total_seconds = (double)(GetTickCount64() - g_hw_start_tick) / 1000.0;
        hw_stamp_now(&g_hw_info);
        hw_save(&g_hw_info);
        hw_log_result(&g_hw_info);
        g_hw_has_info = 1;
        if (g_hw_error == HW_ERR_FFMPEG) {
            set_status_text(text(STR_HW_ERROR_FFMPEG), 1);
            gb_message_box(hWnd, text(STR_HW_PANEL), text(STR_HW_ERROR_FFMPEG), MB_OK | MB_ICONERROR);
        } else if (g_hw_error == HW_ERR_NO_ENCODER) {
            set_status_text(text(STR_HW_ERROR_NO_ENCODER), 1);
            gb_message_box(hWnd, text(STR_HW_PANEL), text(STR_HW_ERROR_NO_ENCODER), MB_OK | MB_ICONERROR);
        } else {
            set_status_text(text(STR_HW_FAILED), 1);
            gb_message_box(hWnd, text(STR_HW_PANEL), text(STR_HW_FAILED), MB_OK | MB_ICONERROR);
        }
    } else {
        g_hw_info = msg->info;
        g_hw_info.skipped = 0;
        g_hw_has_info = 1;
        hw_save(&g_hw_info);
        hw_log_result(&g_hw_info);
        profile = g_hw_info.profile;
        _snwprintf(buf, 256, text(STR_HW_DONE_FMT),
                   hw_profile_name(profile), (int)(g_hw_info.total_seconds + 0.5));
        buf[255] = L'\0';
        set_status_text(buf, 0);
    }

    if (g_hw_pending_path[0]) {
        wcsncpy(probe_path, g_hw_pending_path, UTIL_PATH_MAX - 1);
        probe_path[UTIL_PATH_MAX - 1] = L'\0';
        g_hw_pending_path[0] = L'\0';
        should_probe = 1;
    }

    g_hw_testing = 0;
    g_hw_skipped = 0;
    g_hw_cancel = 0;
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
    SetFocus(g_has_info ? g_hBtnContinue : g_hBtnChoose);

    if (should_probe && probe_path[0]) start_probe(hWnd, probe_path);
    free(msg);
}

static int quality_current_profile(void)
{
    if (g_hw_override != HW_PROFILE_AUTO) return g_hw_override;
    if (g_hw_has_info && g_hw_info.profile >= 0) return g_hw_info.profile;
    return HW_PROFILE_BALANCED;
}

static EstimateLevel *quality_level_result(int level)
{
    int i;
    for (i = 0; i < g_est.level_count; i++) {
        if (g_est.levels_used[i] == level) return &g_est.levels[i];
    }
    return NULL;
}

static int quality_level_index(int level)
{
    int i;
    for (i = 0; i < g_est.level_count; i++) {
        if (g_est.levels_used[i] == level) return i;
    }
    return -1;
}

static const wchar_t *quality_ssim_label(double ssim)
{
    if (ssim >= 0.98) return text(STR_QUALITY_SSIM_IDENTICAL);
    if (ssim >= 0.96) return text(STR_QUALITY_SSIM_VERY_GOOD);
    if (ssim >= 0.93) return text(STR_QUALITY_SSIM_GOOD);
    if (ssim >= 0.90) return text(STR_QUALITY_SSIM_SOFT);
    return text(STR_QUALITY_SSIM_LOW);
}

static void quality_clear_preview_bitmaps(void)
{
    if (g_preview_orig_bmp) { DeleteObject(g_preview_orig_bmp); g_preview_orig_bmp = NULL; }
    if (g_preview_enc_bmp) { DeleteObject(g_preview_enc_bmp); g_preview_enc_bmp = NULL; }
}

static void quality_cleanup_previews(void)
{
    quality_clear_preview_bitmaps();
    estimate_cleanup_preview_dir(&g_est);
    g_quality_preview = 0;
}

static void quality_load_preview(void)
{
    wchar_t path[UTIL_PATH_MAX];
    int index = quality_level_index(g_quality_selected);

    quality_clear_preview_bitmaps();
    if (index < 0 || !g_est.preview_available[index] || !g_est.preview_dir[0]) return;

    _snwprintf(path, UTIL_PATH_MAX, L"%ls\\prev_%d_orig.bmp", g_est.preview_dir, index);
    path[UTIL_PATH_MAX - 1] = L'\0';
    g_preview_orig_bmp = (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0,
                                             LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    _snwprintf(path, UTIL_PATH_MAX, L"%ls\\prev_%d_enc.bmp", g_est.preview_dir, index);
    path[UTIL_PATH_MAX - 1] = L'\0';
    g_preview_enc_bmp = (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0,
                                            LR_LOADFROMFILE | LR_CREATEDIBSECTION);
}

static void quality_ui_open_preview(HWND hWnd)
{
    int index = quality_level_index(g_quality_selected);
    if (!g_quality_has_result || index < 0 || !g_est.preview_available[index]) {
        gb_message_box(hWnd, text(STR_QUALITY_PREVIEW_PANEL),
                       text(STR_QUALITY_PREVIEW_UNAVAILABLE), MB_OK | MB_ICONINFORMATION);
        return;
    }
    g_quality_preview = 1;
    g_quality_show_details = 0;
    quality_load_preview();
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
}

static void quality_ui_update_selection(void)
{
    int i;
    int found = 0;
    for (i = 0; i < g_est.level_count; i++) {
        if (g_est.levels_used[i] == g_quality_selected) { found = 1; break; }
    }
    if (!found) {
        g_quality_selected = g_est.level_count ? g_est.levels_used[0] : QUALITY_BALANCED;
        for (i = 0; i < g_est.level_count; i++) {
            if (g_est.levels_used[i] == QUALITY_BALANCED) {
                g_quality_selected = QUALITY_BALANCED;
                break;
            }
        }
    }
}

typedef struct {
    HWND hwnd;
    int gen;
    SourceInfo src;
    HwInfo hw;
    int profile;
    int deep;
    int encoder_override;
} EstimateJob;

static void quality_progress_cb(const EstimateProgress *p, void *user)
{
    EstProgressMsg *copy;
    EstimateJob *job = (EstimateJob*)user;
    if (!p || !job) return;
    copy = (EstProgressMsg*)malloc(sizeof(EstProgressMsg));
    if (!copy) return;
    copy->gen = job->gen;
    copy->progress = *p;
    if (!PostMessageW(job->hwnd, WM_APP_EST_PROGRESS, 0, (LPARAM)copy)) free(copy);
}

static DWORD WINAPI quality_thread_proc(LPVOID param)
{
    EstimateJob *job = (EstimateJob*)param;
    EstimateDoneMsg *msg;
    EstimateResult result;

    msg = (EstimateDoneMsg*)calloc(1, sizeof(EstimateDoneMsg));
    if (!msg) {
        free(job);
        return 0;
    }
    msg->gen = job->gen;
    result = estimate_run(&job->src, &job->hw, job->profile, job->deep,
                          job->encoder_override, &msg->est,
                          quality_progress_cb, job, &g_est_cancel);
    msg->result = result;
    if (!PostMessageW(job->hwnd, WM_APP_EST_DONE, 0, (LPARAM)msg)) free(msg);
    free(job);
    return 0;
}

static void quality_ui_start(HWND hWnd, int force)
{
    EstimateJob *job;
    HANDLE thread;
    (void)force;

    if (!g_has_info || g_quality_testing) return;

    g_step = 1;
    g_quality_testing = 1;
    g_quality_has_result = 0;
    g_quality_show_details = 0;
    g_custom_est_ready = 0;
    if (g_custom_est_testing) {
        g_custom_est_cancel = 1;
        g_custom_est_gen++;
        g_custom_est_testing = 0;
    }
    g_quality_gen++;
    quality_cleanup_previews();
    g_est_cancel = 0;
    g_est_start_tick = GetTickCount64();
    memset(&g_est, 0, sizeof(g_est));
    memset(&g_est_progress, 0, sizeof(g_est_progress));
    g_est.encoder = (g_quality_encoder >= 0) ? g_quality_encoder
                                             : estimate_choose_encoder(&g_info, &g_hw_info);
    g_est_seconds = (g_quality_last_seconds > 0.0)
        ? (int)(g_quality_last_seconds + 0.5)
        : estimate_demo_seconds(&g_info, &g_hw_info, quality_current_profile());
    if (g_quality_deep) {
        int levels[ESTIMATE_MAX_LEVELS], level_count = 0, clips = 0;
        double clip_sec = 0.0;
        estimate_plan(&g_info, quality_current_profile(), levels, &level_count,
                      &clips, &clip_sec);
        if (level_count > 0 && clips > 0 && clip_sec > 0.0) {
            /* DEEP always uses 4 x 6 s per level; scale from the normal
               sample budget instead of a fixed multiplier. */
            double ratio = (4.0 * 6.0) / ((double)clips * clip_sec);
            g_est_seconds = (int)((double)g_est_seconds * ratio + 0.5);
        } else {
            g_est_seconds = g_est_seconds * 2 + 3;
        }
    }
    set_status_id(STR_STATUS_QUALITY_TESTING, 0);
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);

    job = (EstimateJob*)calloc(1, sizeof(EstimateJob));
    if (!job) {
        g_quality_testing = 0;
        set_status_text(text(STR_QUALITY_ERROR), 1);
        update_controls_visibility();
        return;
    }
    job->hwnd = hWnd;
    job->gen = g_quality_gen;
    job->src = g_info;
    job->hw = g_hw_info;
    job->profile = quality_current_profile();
    job->deep = g_quality_deep;
    job->encoder_override = g_quality_encoder;
    thread = CreateThread(NULL, 0, quality_thread_proc, job, 0, NULL);
    if (thread) {
        CloseHandle(thread);
    } else {
        free(job);
        g_quality_testing = 0;
        set_status_text(text(STR_QUALITY_ERROR), 1);
        update_controls_visibility();
    }
}

static void quality_ui_start_deep(HWND hWnd)
{
    if (!g_has_info || g_quality_testing) return;
    g_quality_deep = 1;
    quality_ui_start(hWnd, 1);
}

static int encoder_usable_for_source(int enc)
{
    if (enc == HW_ENC_X265) return 1;
    if (enc < 0 || enc >= HW_ENC_COUNT) return 0;
    if (g_info.is_hdr || g_info.bit_depth >= 10) {
        return g_hw_info.enc_ok10[enc] != 0;
    }
    return g_hw_info.enc_ok8[enc] || g_hw_info.enc_ok10[enc];
}

static void quality_ui_refresh_encoder_items(void)
{
    int i;
    g_quality_enc_count = 0;
    g_quality_enc_map[g_quality_enc_count] = -1;
    wcsncpy(g_quality_enc_items[g_quality_enc_count], text(STR_QUALITY_ENC_AUTO), 63);
    g_quality_enc_items[g_quality_enc_count][63] = L'\0';
    g_quality_enc_count++;

    g_quality_enc_map[g_quality_enc_count] = HW_ENC_X265;
    wcsncpy(g_quality_enc_items[g_quality_enc_count], text(STR_QUALITY_ENC_CPU), 63);
    g_quality_enc_items[g_quality_enc_count][63] = L'\0';
    g_quality_enc_count++;

    for (i = 0; i < HW_ENC_COUNT && g_quality_enc_count < HW_ENC_COUNT + 1; i++) {
        if (i == HW_ENC_X265) continue;
        if (!encoder_usable_for_source(i)) continue;
        g_quality_enc_map[g_quality_enc_count] = i;
        _snwprintf(g_quality_enc_items[g_quality_enc_count], 64,
                   text(STR_QUALITY_ENC_GPU_FMT), hw_encoder_name(i));
        g_quality_enc_items[g_quality_enc_count][63] = L'\0';
        g_quality_enc_count++;
    }
    {
        int found = 0;
        for (i = 0; i < g_quality_enc_count; i++) {
            if (g_quality_enc_map[i] == g_quality_encoder) { found = 1; break; }
        }
        if (!found) {
            g_quality_encoder = -1;
            if (g_enc_options_ready) g_enc_opt.encoder_override = -1;
        }
    }
}

static void quality_ui_encoder_picked(int index, void *user)
{
    (void)user;
    if (index < 0 || index >= g_quality_enc_count) return;
    if (g_quality_encoder != g_quality_enc_map[index]) {
        g_quality_last_seconds = 0.0;
    }
    g_quality_encoder = g_quality_enc_map[index];
    if (!g_enc_options_ready) {
        encode_options_defaults(&g_enc_opt, &g_info);
        g_enc_options_ready = 1;
    }
    g_enc_opt.encoder_override = g_quality_encoder;
    quality_ui_start(g_hWnd, 1);
    update_controls_visibility();
    InvalidateRect(g_hWnd, NULL, FALSE);
}

static void quality_ui_open_encoder_dropdown(HWND hWnd)
{
    const wchar_t *ptrs[HW_ENC_COUNT + 1];
    RECT anchor;
    int i, selected = 0;

    quality_ui_refresh_encoder_items();
    for (i = 0; i < g_quality_enc_count; i++) {
        ptrs[i] = g_quality_enc_items[i];
        if (g_quality_enc_map[i] == g_quality_encoder) selected = i;
    }
    anchor = g_rc_q_encoder;
    gb_dropdown_open(hWnd, &anchor, ptrs, g_quality_enc_count, selected,
                     quality_ui_encoder_picked, NULL);
}

static void quality_ui_on_done(HWND hWnd, EstimateDoneMsg *msg)
{
    if (!msg) return;
    if (msg->gen != g_quality_gen) {
        free(msg);
        return;
    }
    quality_clear_preview_bitmaps();
    g_quality_preview = 0;
    if (msg->result == ESTIMATE_OK) {
        g_est = msg->est;
        g_quality_has_result = 1;
        g_quality_last_seconds = g_est.total_seconds;
        if (g_est.used_fallback) {
            /* The estimate fell back to CPU x265 after the GPU test failed.
               The actual encode must follow the estimate, not the encoder the
               user picked before the failure. */
            g_quality_encoder = -1;
            if (g_enc_options_ready) g_enc_opt.encoder_override = -1;
        }
        quality_ui_update_selection();
        if (g_est.used_fallback) set_status_text(text(STR_QUALITY_FALLBACK), 0);
        else set_status_id(STR_QUALITY_DONE, 0);
    } else if (msg->result == ESTIMATE_CANCELLED) {
        g_quality_has_result = 0;
        set_status_id(STR_QUALITY_CANCELED, 0);
    } else {
        g_quality_has_result = 0;
        set_status_text(text(STR_QUALITY_ERROR), 1);
        gb_message_box(hWnd, text(STR_QUALITY_PANEL), text(STR_QUALITY_ERROR),
                       MB_OK | MB_ICONERROR);
    }
    g_quality_testing = 0;
    g_est_cancel = 0;
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
    free(msg);
}

static void quality_ui_test_again(HWND hWnd)
{
    wchar_t note[512];
    int seconds;
    if (!g_has_info || g_quality_testing) return;
    seconds = (g_quality_last_seconds > 0.0)
        ? (int)(g_quality_last_seconds + 0.5)
        : estimate_demo_seconds(&g_info, &g_hw_info, quality_current_profile());
    _snwprintf(note, 512, text(STR_QUALITY_RETEST_FMT), seconds);
    note[511] = L'\0';
    if (gb_message_box(hWnd, text(STR_QUALITY_RETEST_TITLE), note,
                       MB_YESNO | MB_ICONQUESTION) == IDYES) {
        g_quality_deep = 0;
        quality_ui_start(hWnd, 1);
    }
}

static void quality_ui_cancel(void)
{
    if (!g_quality_testing) return;
    g_est_cancel = 1;
    EnableWindow(g_hBtnQCancel, FALSE);
}

static void quality_ui_back(HWND hWnd)
{
    if (g_quality_testing) return;
    if (g_quality_preview) {
        g_quality_preview = 0;
        quality_clear_preview_bitmaps();
        update_controls_visibility();
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }
    g_step = 0;
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
}

static void quality_ui_compress(HWND hWnd)
{
    if (!g_quality_has_result) return;
    encode_ui_enter(hWnd);
}

static void quality_select_at(HWND hWnd, int x, int y)
{
    int i;
    POINT pt;
    pt.x = x;
    pt.y = y;
    if (g_quality_testing || !g_quality_has_result) return;
    if (!g_quality_preview) {
        if (g_quality_show_details) {
            if (PtInRect(&g_rc_q_deep, pt)) {
                quality_ui_start_deep(hWnd);
                return;
            }
            if (PtInRect(&g_rc_q_details, pt)) {
                g_quality_show_details = 0;
                InvalidateRect(hWnd, NULL, FALSE);
                return;
            }
        } else {
            if (PtInRect(&g_rc_q_encoder, pt)) {
                quality_ui_open_encoder_dropdown(hWnd);
                return;
            }
            if (PtInRect(&g_rc_q_custom, pt)) {
                encode_ui_open_advanced(hWnd);
                return;
            }
            if (PtInRect(&g_rc_q_details, pt)) {
                g_quality_show_details = 1;
                InvalidateRect(hWnd, NULL, FALSE);
                return;
            }
        }
    }
    if (g_quality_preview) {
        for (i = 0; i < g_preview_chip_count; i++) {
            if (PtInRect(&g_rc_preview_chips[i], pt)) {
                int level = g_est.levels_used[i];
                g_quality_selected = level;
                g_enc_opt.crf_override = 0;
                g_enc_opt.encoder_override = g_quality_encoder;
                g_custom_est_ready = 0;
                quality_load_preview();
                InvalidateRect(hWnd, NULL, FALSE);
                return;
            }
        }
        return;
    }
    for (i = 0; i < g_card_count; i++) {
        if (PtInRect(&g_rc_cards[i], pt)) {
            int level = g_est.levels_used[i];
            g_quality_selected = level;
            g_enc_opt.crf_override = 0;
            g_enc_opt.encoder_override = g_quality_encoder;
            g_custom_est_ready = 0;
            InvalidateRect(hWnd, NULL, FALSE);
            return;
        }
    }
}

static void quality_ui_reset(void)
{
    g_quality_gen++;
    g_est_cancel = 1;
    g_quality_testing = 0;
    g_quality_has_result = 0;
    g_quality_last_seconds = 0.0;
    quality_cleanup_previews();
    memset(&g_est, 0, sizeof(g_est));
    memset(&g_est_progress, 0, sizeof(g_est_progress));
    g_quality_selected = QUALITY_BALANCED;
    g_quality_encoder = -1;
    encode_ui_reset();
}

static void encode_ui_reset(void)
{
    g_encode_gen++;
    g_encode_cancel = 1;
    g_encode_state = 0;
    g_enc_options_ready = 0;
    g_encode_output[0] = L'\0';
    g_encode_detail[0] = L'\0';
    g_encode_error_code = ENCODE_ERR_NONE;
    g_encode_size = 0;
    g_input_active = 0;
    g_advanced_active = 0;
    g_quality_show_details = 0;
    g_quality_deep = 0;
    g_custom_est_gen++;
    g_custom_est_cancel = 1;
    g_custom_est_testing = 0;
    g_custom_est_ready = 0;
    memset(&g_custom_est, 0, sizeof(g_custom_est));
    memset(&g_custom_est_progress, 0, sizeof(g_custom_est_progress));
    memset(&g_enc_opt, 0, sizeof(g_enc_opt));
    memset(&g_encode_progress, 0, sizeof(g_encode_progress));
}

static void encode_ui_update_controls(void)
{
    int has_audio = g_info.audio_track_count > 0 && !g_enc_opt.audio_none;
    int has_bitrate = has_audio && encode_audio_has_bitrate(g_enc_opt.audio_codec);
    int downmix_visible = has_bitrate && encode_any_downmix(&g_info, &g_enc_opt);
    wchar_t buf[128];

    ShowWindow(g_hBtnChoose, SW_HIDE);
    ShowWindow(g_hBtnContinue, SW_HIDE);
    ShowWindow(g_hBtnDetails, SW_HIDE);
    ShowWindow(g_hBtnRetest, SW_HIDE);
    ShowWindow(g_hBtnDepth, SW_HIDE);
    ShowWindow(g_hBtnSkip, SW_HIDE);
    ShowWindow(g_hBtnTestAgain, SW_HIDE);
    ShowWindow(g_hBtnQCancel, SW_HIDE);
    ShowWindow(g_hBtnCompress, SW_HIDE);
    ShowWindow(g_hBtnPreview, SW_HIDE);

    ShowWindow(g_hBtnBack, g_encode_state == 1 ? SW_HIDE : SW_SHOW);
    ShowWindow(g_hBtnStart, (g_encode_state == 0 || g_encode_state == 3) ? SW_SHOW : SW_HIDE);
    if (g_encode_state == 0 || g_encode_state == 3) {
        EnableWindow(g_hBtnStart, g_custom_est_testing ? FALSE : TRUE);
    }
    ShowWindow(g_hBtnECancel, g_encode_state == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hBtnShow, g_encode_state == 2 ? SW_SHOW : SW_HIDE);

    SetWindowTextW(g_hBtnBack,
                   text(g_encode_state == 2 ? STR_COMP_BACK_QUALITY : STR_COMP_BACK));
    SetWindowTextW(g_hBtnStart,
                   text(g_encode_state == 3 ? STR_COMP_TRY_AGAIN : STR_COMP_START));

    if (g_encode_state == 0) {
        ShowWindow(g_hBtnContMkv, SW_SHOW);
        ShowWindow(g_hBtnContMp4, SW_SHOW);
        ShowWindow(g_hBtnVcHevc, SW_SHOW);
        ShowWindow(g_hBtnVcH264, SW_SHOW);
        if (has_audio) {
            ShowWindow(g_hBtnAudio, SW_SHOW);
            SetWindowTextW(g_hBtnAudio, encode_audio_codec_label(g_enc_opt.audio_codec));
        }
        if (g_info.audio_tracks_stored > 1) {
            ShowWindow(g_hBtnAudioTrack, SW_SHOW);
        }
        if (g_info.video_real_count > 1) {
            ShowWindow(g_hBtnVideoTrack, SW_SHOW);
        }
        if (g_info.subtitle_track_count > 0) {
            ShowWindow(g_hBtnSubtitleTrack, SW_SHOW);
            encode_ui_refresh_subtitle_button();
        }
        ShowWindow(g_hBtnAdvanced, SW_SHOW);
        encode_ui_update_track_labels();
        if (has_bitrate) {
            ShowWindow(g_hBtnABitrate, SW_SHOW);
            bitrate_button_text(buf, 128);
            SetWindowTextW(g_hBtnABitrate, buf);
            if (downmix_visible) {
                ShowWindow(g_hBtnDownmix, SW_SHOW);
                EnableWindow(g_hBtnDownmix,
                             encode_audio_force_stereo(g_enc_opt.audio_codec) ? FALSE : TRUE);
            }
        }
    }
    if (g_encode_state == 0) encode_ui_refresh_status_estimate();
}

static void encode_ui_enter(HWND hWnd)
{
    if (!g_has_info || !g_quality_has_result) return;

    if (!g_enc_options_ready) {
        encode_options_defaults(&g_enc_opt, &g_info);
        g_enc_options_ready = 1;
    }
    if (g_info.video_tracks_stored > 0 &&
        (g_enc_opt.video_track < 0 ||
         g_enc_opt.video_track >= g_info.video_tracks_stored)) {
        g_enc_opt.video_track = (g_info.video_primary >= 0)
            ? g_info.video_primary : 0;
    }
    if (g_info.audio_track_count <= 0) {
        g_enc_opt.audio_codec = ENC_ACODEC_COPY;
    } else if (!encode_audio_available(g_enc_opt.container, g_enc_opt.audio_codec) ||
               (g_enc_opt.audio_codec == ENC_ACODEC_COPY &&
                !encode_audio_copy_available_sel(&g_info, &g_enc_opt,
                                                 g_enc_opt.container))) {
        g_enc_opt.audio_codec = encode_default_audio(g_enc_opt.container);
    }
    encode_fix_bitrate(&g_enc_opt);

    g_step = 2;
    g_encode_state = 0;
    g_encode_cancel = 0;
    g_encode_output[0] = L'\0';
    g_encode_detail[0] = L'\0';
    g_encode_error_code = ENCODE_ERR_NONE;
    g_encode_size = 0;
    memset(&g_encode_progress, 0, sizeof(g_encode_progress));
    set_status_text(text(STR_COMP_HEADING), 0);
    update_controls_visibility();
    encode_ui_restart_custom_test(hWnd);
    InvalidateRect(hWnd, NULL, FALSE);
    SetFocus(g_hBtnStart);
}

static void encode_ui_audio_picked(int index, void *user)
{
    int codec, i = 0;
    (void)user;
    for (codec = 0; codec < ENC_ACODEC_COUNT; codec++) {
        if (!encode_audio_available(g_enc_opt.container, codec)) continue;
        if (codec == ENC_ACODEC_COPY &&
            !encode_audio_copy_available_sel(&g_info, &g_enc_opt,
                                             g_enc_opt.container)) continue;
        if (i == index) {
            g_enc_opt.audio_codec = codec;
            encode_fix_bitrate(&g_enc_opt);
            if (encode_audio_force_stereo(codec)) g_enc_opt.downmix_stereo = 1;
            update_controls_visibility();
            InvalidateRect(g_hWnd, NULL, FALSE);
            return;
        }
        i++;
    }
}

static void encode_ui_bitrate_picked(int index, void *user)
{
    int ac3 = g_enc_opt.audio_codec == ENC_ACODEC_AC3 ||
              g_enc_opt.audio_codec == ENC_ACODEC_EAC3;
    (void)user;

    if (ac3) {
        if (index == 0) g_enc_opt.bitrate = ENC_ABITRATE_HIGH;
        else PostMessageW(g_hWnd, WM_APP_OPEN_CUSTOM, 0, 0);
    } else {
        if (index >= 0 && index <= 2) {
            g_enc_opt.bitrate = index;
        } else if (index == 3) {
            PostMessageW(g_hWnd, WM_APP_OPEN_CUSTOM, 0, 0);
        }
    }
    update_controls_visibility();
    InvalidateRect(g_hWnd, NULL, FALSE);
}

static void encode_ui_open_audio_dropdown(HWND hWnd)
{
    const wchar_t *ptrs[ENC_ACODEC_COUNT];
    RECT anchor;
    int count = 0, selected = 0, codec;

    update_audio_dropdown_items();
    for (codec = 0; codec < ENC_ACODEC_COUNT; codec++) {
        if (!encode_audio_available(g_enc_opt.container, codec)) continue;
        if (codec == ENC_ACODEC_COPY &&
            !encode_audio_copy_available_sel(&g_info, &g_enc_opt,
                                             g_enc_opt.container)) continue;
        ptrs[count] = g_audio_items[count];
        if (codec == g_enc_opt.audio_codec) selected = count;
        count++;
    }
    GetWindowRect(g_hBtnAudio, &anchor);
    MapWindowPoints(NULL, hWnd, (POINT*)&anchor, 2);
    gb_dropdown_open(hWnd, &anchor, ptrs, count, selected,
                     encode_ui_audio_picked, NULL);
}

static void encode_ui_update_track_labels(void)
{
    wchar_t buf[96];

    if (g_info.audio_tracks_stored > 1) {
        int selected = encode_selected_audio_count(&g_info, &g_enc_opt);
        buf[0] = L'\0';
        if (g_enc_opt.audio_none) {
            wcsncpy(buf, text(STR_COMP_AUDIO_TRACK_NONE_FMT), 95);
        } else if (g_enc_opt.audio_mask == 0 ||
            selected >= g_info.audio_tracks_stored) {
            _snwprintf(buf, 96, text(STR_COMP_AUDIO_TRACK_ALL_FMT),
                       g_info.audio_tracks_stored);
        } else if (selected == 1) {
            int i, found = 0;
            for (i = 0; i < g_info.audio_tracks_stored; i++) {
                if (encode_audio_selected(&g_info, &g_enc_opt, i)) { found = i; break; }
            }
            _snwprintf(buf, 96, text(STR_COMP_AUDIO_TRACK_ONE_FMT),
                       found + 1, g_info.audio_tracks_stored);
        } else {
            _snwprintf(buf, 96, text(STR_COMP_AUDIO_TRACK_N_FMT), selected);
        }
        buf[95] = L'\0';
        SetWindowTextW(g_hBtnAudioTrack, buf);
    }

    if (g_info.video_real_count > 1) {
        int i, ordinal = 0, count = 0;
        int target = encode_selected_video_track(&g_info, &g_enc_opt);
        for (i = 0; i < g_info.video_tracks_stored; i++) {
            if (g_info.video_tracks[i].attached_pic) continue;
            if (i == target) ordinal = count;
            count++;
        }
        _snwprintf(buf, 96, text(STR_COMP_VIDEO_TRACK_FMT), ordinal + 1, count);
        buf[95] = L'\0';
        SetWindowTextW(g_hBtnVideoTrack, buf);
    }
}

static void encode_ui_audio_track_picked(int index, void *user)
{
    (void)user;
    if (index <= 0) {
        g_enc_opt.audio_mask = 0; /* all tracks */
        g_enc_opt.audio_none = 0;
    } else if (index == 1) {
        g_enc_opt.audio_mask = 0;
        g_enc_opt.audio_none = 1;
    } else {
        int track = index - 2;
        g_enc_opt.audio_none = 0;
        if (track >= 0 && track < g_info.audio_tracks_stored &&
            track < 32) {
            g_enc_opt.audio_mask = 1u << track;
        }
    }
    if (g_enc_opt.audio_codec == ENC_ACODEC_COPY &&
        !encode_audio_copy_available_sel(&g_info, &g_enc_opt,
                                         g_enc_opt.container)) {
        g_enc_opt.audio_codec = encode_default_audio(g_enc_opt.container);
    }
    encode_fix_bitrate(&g_enc_opt);
    update_controls_visibility();
    InvalidateRect(g_hWnd, NULL, FALSE);
}

static void encode_ui_video_track_picked(int index, void *user)
{
    (void)user;
    if (index >= 0 && index < PROBE_MAX_VIDEO_TRACKS) {
        int stored = g_video_item_map[index];
        if (stored >= 0 && stored < g_info.video_tracks_stored) {
            g_enc_opt.video_track = stored;
        }
    }
    update_controls_visibility();
    InvalidateRect(g_hWnd, NULL, FALSE);
    encode_ui_restart_custom_test(g_hWnd);
}

static void encode_ui_open_audio_track_dropdown(HWND hWnd)
{
    const wchar_t *ptrs[PROBE_MAX_AUDIO_TRACKS + 2];
    RECT anchor;
    int count = 0, selected = 0, i;

    _snwprintf(g_track_items[0], 160, text(STR_COMP_TRACK_ALL_FMT),
               g_info.audio_tracks_stored);
    g_track_items[0][159] = L'\0';
    ptrs[count++] = g_track_items[0];
    wcsncpy(g_track_items[1], text(STR_COMP_TRACK_NO_AUDIO), 159);
    g_track_items[1][159] = L'\0';
    ptrs[count++] = g_track_items[1];
    for (i = 0; i < g_info.audio_tracks_stored &&
                count < PROBE_MAX_AUDIO_TRACKS + 2; i++) {
        format_audio_track_item(i, g_track_items[count], 160);
        if (g_enc_opt.audio_mask == (1u << i)) selected = count;
        ptrs[count] = g_track_items[count];
        count++;
    }
    if (g_enc_opt.audio_none) selected = 1;

    GetWindowRect(g_hBtnAudioTrack, &anchor);
    MapWindowPoints(NULL, hWnd, (POINT*)&anchor, 2);
    gb_dropdown_open(hWnd, &anchor, ptrs, count, selected,
                     encode_ui_audio_track_picked, NULL);
}

static void encode_ui_open_video_track_dropdown(HWND hWnd)
{
    const wchar_t *ptrs[PROBE_MAX_VIDEO_TRACKS];
    RECT anchor;
    int count = 0, selected = 0, i;
    int target = encode_selected_video_track(&g_info, &g_enc_opt);

    for (i = 0; i < g_info.video_tracks_stored && count < PROBE_MAX_VIDEO_TRACKS; i++) {
        if (g_info.video_tracks[i].attached_pic) continue;
        g_video_item_map[count] = i;
        format_video_track_item(count, i, g_video_items[count], 128);
        if (i == target) selected = count;
        ptrs[count] = g_video_items[count];
        count++;
    }

    GetWindowRect(g_hBtnVideoTrack, &anchor);
    MapWindowPoints(NULL, hWnd, (POINT*)&anchor, 2);
    gb_dropdown_open(hWnd, &anchor, ptrs, count, selected,
                     encode_ui_video_track_picked, NULL);
}

static void encode_ui_subtitle_track_picked(int index, void *user)
{
    (void)user;
    if (index == 0) {
        g_enc_opt.subtitle_mask = 0;
        g_enc_opt.subtitles_none = 0;
    } else if (index == 1) {
        g_enc_opt.subtitle_mask = 0;
        g_enc_opt.subtitles_none = 1;
    } else {
        int track = index - 2;
        g_enc_opt.subtitles_none = 0;
        if (track >= 0 && track < g_info.subtitle_tracks_stored && track < 64) {
            g_enc_opt.subtitle_mask = 1ull << track;
        }
    }
    update_controls_visibility();
    InvalidateRect(g_hWnd, NULL, FALSE);
}

static void encode_ui_open_subtitle_track_dropdown(HWND hWnd)
{
    const wchar_t *ptrs[PROBE_MAX_SUBTITLE_TRACKS + 2];
    RECT anchor;
    int count = 0, selected = 0, i;

    _snwprintf(g_sub_items[0], 160, text(STR_COMP_TRACK_ALL_SUBS_FMT),
               g_info.subtitle_tracks_stored);
    g_sub_items[0][159] = L'\0';
    ptrs[count++] = g_sub_items[0];
    wcsncpy(g_sub_items[1], text(STR_COMP_TRACK_NO_SUBS), 159);
    g_sub_items[1][159] = L'\0';
    ptrs[count++] = g_sub_items[1];

    for (i = 0; i < g_info.subtitle_tracks_stored && count < PROBE_MAX_SUBTITLE_TRACKS + 2; i++) {
        format_subtitle_track_item(i, g_sub_items[count], 160);
        if (g_enc_opt.subtitle_mask == (1ull << i)) selected = count;
        ptrs[count] = g_sub_items[count];
        count++;
    }
    if (g_enc_opt.subtitles_none) selected = 1;

    GetWindowRect(g_hBtnSubtitleTrack, &anchor);
    MapWindowPoints(NULL, hWnd, (POINT*)&anchor, 2);
    gb_dropdown_open(hWnd, &anchor, ptrs, count, selected,
                     encode_ui_subtitle_track_picked, NULL);
}

static void encode_ui_refresh_subtitle_button(void)
{
    wchar_t buf[96];
    int selected;

    if (!g_hBtnSubtitleTrack) return;
    if (g_info.subtitle_track_count <= 0) {
        SetWindowTextW(g_hBtnSubtitleTrack, text(STR_COMP_TRACK_NO_SUBS));
        return;
    }
    selected = encode_selected_subtitle_count(&g_info, &g_enc_opt,
                                              g_enc_opt.container);
    if (g_enc_opt.subtitles_none) {
        wcsncpy(buf, text(STR_COMP_SUBTITLE_TRACK_NONE_FMT), 95);
    } else if (g_enc_opt.subtitle_mask == 0) {
        int all = (g_enc_opt.container == ENC_CONT_MP4)
            ? encode_text_subtitle_count(&g_info)
            : g_info.subtitle_tracks_stored;
        _snwprintf(buf, 96, text(STR_COMP_SUBTITLE_TRACK_ALL_FMT), all);
    } else if (selected == 1) {
        _snwprintf(buf, 96, text(STR_COMP_SUBTITLE_TRACK_ONE_FMT), selected);
    } else {
        _snwprintf(buf, 96, text(STR_COMP_SUBTITLE_TRACK_N_FMT), selected);
    }
    buf[95] = L'\0';
    SetWindowTextW(g_hBtnSubtitleTrack, buf);
}

static void encode_ui_update_adv_labels(void)
{
    const wchar_t *enc_name = text(STR_COMP_ADV_ENC_AUTO);
    int i;
    for (i = 0; i < g_adv_enc_count; i++) {
        if (g_adv_enc_map[i] == g_adv_encoder) {
            enc_name = g_adv_enc_items[i];
            break;
        }
    }
    if (g_hBtnAdvEnc) SetWindowTextW(g_hBtnAdvEnc, enc_name);
    if (g_hBtnAdvQ) SetWindowTextW(g_hBtnAdvQ, adv_quality_label());
    if (g_hEditCrf) {
        wchar_t buf[16];
        _snwprintf(buf, 16, L"%d", g_adv_crf);
        buf[15] = L'\0';
        SetWindowTextW(g_hEditCrf, buf);
    }
}

static void encode_ui_adv_encoder_picked(int index, void *user)
{
    (void)user;
    if (index >= 0 && index < g_adv_enc_count) {
        g_adv_encoder = g_adv_enc_map[index];
        encode_ui_update_adv_labels();
        InvalidateRect(g_hWnd, NULL, FALSE);
    }
}

static void encode_ui_adv_quality_picked(int index, void *user)
{
    (void)user;
    g_adv_quality = index;
    if (index == 5) {
        g_adv_crf = 19;
        g_adv_encoder = HW_ENC_X265;
    }
    encode_ui_update_adv_labels();
    update_controls_visibility();
    InvalidateRect(g_hWnd, NULL, FALSE);
}

static void encode_ui_open_adv_encoder_dropdown(HWND hWnd)
{
    const wchar_t *ptrs[HW_ENC_COUNT + 1];
    RECT anchor;
    int i, selected = 0;

    for (i = 0; i < g_adv_enc_count; i++) {
        ptrs[i] = g_adv_enc_items[i];
        if (g_adv_enc_map[i] == g_adv_encoder) selected = i;
    }
    GetWindowRect(g_hBtnAdvEnc, &anchor);
    MapWindowPoints(NULL, hWnd, (POINT*)&anchor, 2);
    gb_dropdown_open(hWnd, &anchor, ptrs, g_adv_enc_count, selected,
                     encode_ui_adv_encoder_picked, NULL);
}

static void encode_ui_open_adv_quality_dropdown(HWND hWnd)
{
    const wchar_t *ptrs[7];
    RECT anchor;
    int i;
    for (i = 0; i < 7; i++) ptrs[i] = g_adv_q_items[i];
    GetWindowRect(g_hBtnAdvQ, &anchor);
    MapWindowPoints(NULL, hWnd, (POINT*)&anchor, 2);
    gb_dropdown_open(hWnd, &anchor, ptrs, 7, g_adv_quality,
                     encode_ui_adv_quality_picked, NULL);
}

static void encode_ui_open_advanced(HWND hWnd)
{
    int i;

    if (g_encode_state == 1 || g_advanced_active) return;
    if (!g_enc_options_ready) {
        encode_options_defaults(&g_enc_opt, &g_info);
        g_enc_options_ready = 1;
    }
    g_adv_encoder = g_enc_opt.encoder_override;
    g_adv_quality = 0;
    g_adv_crf = g_enc_opt.crf_override > 0
        ? g_enc_opt.crf_override
        : presets_quality_value(
            g_adv_encoder >= 0 ? g_adv_encoder
                               : estimate_choose_encoder(&g_info, &g_hw_info),
            g_quality_selected, g_enc_opt.video_codec);
    if (g_enc_opt.crf_override > 0) {
        g_adv_quality = (g_enc_opt.encoder_override == HW_ENC_X265 &&
                         g_enc_opt.crf_override == 19) ? 5 : 6;
    }

    g_adv_enc_count = 0;
    g_adv_enc_map[g_adv_enc_count] = -1;
    wcsncpy(g_adv_enc_items[g_adv_enc_count], text(STR_COMP_ADV_ENC_AUTO), 47);
    g_adv_enc_items[g_adv_enc_count][47] = L'\0';
    g_adv_enc_count++;
    for (i = 0; i < HW_ENC_COUNT; i++) {
        int available = encoder_usable_for_source(i);
        if (!available) continue;
        g_adv_enc_map[g_adv_enc_count] = i;
        wcsncpy(g_adv_enc_items[g_adv_enc_count], hw_encoder_name(i), 47);
        g_adv_enc_items[g_adv_enc_count][47] = L'\0';
        g_adv_enc_count++;
    }
    if (g_adv_encoder >= 0) {
        int found = 0;
        for (i = 0; i < g_adv_enc_count; i++) {
            if (g_adv_enc_map[i] == g_adv_encoder) { found = 1; break; }
        }
        if (!found) g_adv_encoder = -1;
    }

    _snwprintf(g_adv_q_items[0], 64, L"%ls (%ls)",
               text(STR_COMP_ADV_Q_PRESET),
               presets_level_name(g_quality_selected));
    g_adv_q_items[0][63] = L'\0';
    wcsncpy(g_adv_q_items[1], presets_level_name(QUALITY_TINY), 63);
    wcsncpy(g_adv_q_items[2], presets_level_name(QUALITY_SMALL), 63);
    wcsncpy(g_adv_q_items[3], presets_level_name(QUALITY_BALANCED), 63);
    wcsncpy(g_adv_q_items[4], presets_level_name(QUALITY_HIGH), 63);
    wcsncpy(g_adv_q_items[5], text(STR_COMP_ADV_Q_ARCHIVE), 63);
    wcsncpy(g_adv_q_items[6], text(STR_COMP_ADV_Q_CUSTOM), 63);
    for (i = 0; i < 7; i++) g_adv_q_items[i][63] = L'\0';

    g_advanced_active = 1;
    encode_ui_update_adv_labels();
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
    SetFocus(g_hBtnAdvOk);
}

static void encode_ui_close_advanced(void)
{
    if (!g_advanced_active) return;
    g_advanced_active = 0;
    update_controls_visibility();
    InvalidateRect(g_hWnd, NULL, FALSE);
}

static void encode_ui_accept_advanced(HWND hWnd)
{
    int was_quality = (g_step == 1);

    if (g_adv_quality == 6) {
        wchar_t buf[16];
        wchar_t *end = NULL;
        long value;
        GetWindowTextW(g_hEditCrf, buf, 16);
        buf[15] = L'\0';
        util_trim(buf);
        value = wcstol(buf, &end, 10);
        if (!buf[0] || !end || *end != L'\0' || value < 1 || value > 51) {
            gb_message_box(hWnd, text(STR_COMP_ADV_TITLE),
                           text(STR_COMP_ADV_CRF_INVALID),
                           MB_OK | MB_ICONWARNING);
            SetFocus(g_hEditCrf);
            return;
        }
        g_adv_crf = (int)value;
    }

    if (g_adv_quality == 5) {
        /* Archive is defined as CPU x265 CRF 19, so make the video codec
           match instead of silently producing libx264 under an x265 label. */
        g_enc_opt.video_codec = ENC_VCODEC_HEVC;
        g_enc_opt.encoder_override = HW_ENC_X265;
        g_enc_opt.crf_override = 19;
        g_quality_encoder = HW_ENC_X265;
    } else if (g_adv_quality == 6) {
        g_enc_opt.encoder_override = g_adv_encoder;
        g_enc_opt.crf_override = g_adv_crf;
        g_quality_encoder = g_adv_encoder;
    } else if (g_adv_quality >= 1 && g_adv_quality <= 4) {
        g_quality_selected = g_adv_quality - 1;
        g_enc_opt.encoder_override = g_adv_encoder;
        g_enc_opt.crf_override = 0;
        g_quality_encoder = g_adv_encoder;
    } else {
        g_enc_opt.encoder_override = g_adv_encoder;
        g_enc_opt.crf_override = 0;
        g_quality_encoder = g_adv_encoder;
    }
    if (g_quality_encoder >= 0 && g_quality_encoder != g_est.encoder) {
        g_quality_last_seconds = 0.0;
    }

    g_advanced_active = 0;
    if (was_quality) encode_ui_enter(hWnd);
    else encode_ui_restart_custom_test(hWnd);
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
}

/* ------------------------------------------------- custom setting test */
static void custom_est_progress_cb(const EstimateProgress *p, void *user)
{
    typedef struct { HWND hwnd; int gen; } Job;
    Job *job = (Job*)user;
    CustomEstProgressMsg *msg;
    if (!p || !job) return;
    msg = (CustomEstProgressMsg*)malloc(sizeof(CustomEstProgressMsg));
    if (!msg) return;
    msg->gen = job->gen;
    msg->progress = *p;
    if (!PostMessageW(job->hwnd, WM_APP_CUSTOM_EST_PROGRESS, 0, (LPARAM)msg)) free(msg);
}

typedef struct {
    HWND hwnd;
    int gen;
    int encoder;
    int video_codec;
    int video_track;
    int quality;
    int preset_level;
    int quality_level;
    int clips;
    double clip_seconds;
    SourceInfo src;
} CustomEstJob;

static DWORD WINAPI custom_est_thread_proc(LPVOID param)
{
    CustomEstJob *job = (CustomEstJob*)param;
    CustomEstDoneMsg *msg;
    msg = (CustomEstDoneMsg*)calloc(1, sizeof(CustomEstDoneMsg));
    if (!msg) { free(job); return 0; }
    msg->gen = job->gen;
    msg->encoder = job->encoder;
    msg->result = estimate_run_custom(&job->src, NULL, job->encoder,
                                      job->video_codec, job->quality,
                                      job->video_track, job->clips,
                                      job->clip_seconds, &msg->est,
                                      custom_est_progress_cb, job,
                                      &g_custom_est_cancel);
    if (msg->result == ESTIMATE_ERROR && job->encoder != HW_ENC_X265) {
        int retry_quality = job->quality;
        /* Match the main estimate: if the hardware encoder cannot run the
           sample, retry on CPU x265 and report the actual encoder. Preset
           values are encoder-specific, so translate them to x265 CRF. */
        if (job->preset_level) {
            int translated = presets_quality_value(HW_ENC_X265,
                                                   job->quality_level,
                                                   job->video_codec);
            if (translated > 0) retry_quality = translated;
        }
        msg->encoder = HW_ENC_X265;
        msg->result = estimate_run_custom(&job->src, NULL, HW_ENC_X265,
                                          job->video_codec, retry_quality,
                                          job->video_track, job->clips,
                                          job->clip_seconds, &msg->est,
                                          custom_est_progress_cb, job,
                                          &g_custom_est_cancel);
        if (msg->result == ESTIMATE_OK) msg->used_fallback = 1;
    }
    if (!PostMessageW(job->hwnd, WM_APP_CUSTOM_EST_DONE, 0, (LPARAM)msg)) free(msg);
    free(job);
    return 0;
}

static void encode_ui_start_custom_test(HWND hWnd)
{
    CustomEstJob *job;
    HANDLE thread;
    int encoder, quality;
    if (!g_has_info || g_custom_est_testing) return;
    encoder = g_enc_opt.encoder_override >= 0
        ? g_enc_opt.encoder_override
        : (g_quality_has_result ? g_est.encoder
                                : estimate_choose_encoder(&g_info, &g_hw_info));
    quality = g_enc_opt.crf_override > 0
        ? g_enc_opt.crf_override
        : presets_quality_value(encoder, g_quality_selected,
                                g_enc_opt.video_codec);
    if (quality <= 0) quality = 24;

    g_custom_est_testing = 1;
    g_custom_est_ready = 0;
    g_custom_est_gen++;
    g_custom_est_cancel = 0;
    memset(&g_custom_est, 0, sizeof(g_custom_est));
    memset(&g_custom_est_progress, 0, sizeof(g_custom_est_progress));
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);

    job = (CustomEstJob*)calloc(1, sizeof(CustomEstJob));
    if (!job) {
        g_custom_est_testing = 0;
        return;
    }
    job->hwnd = hWnd;
    job->gen = g_custom_est_gen;
    job->encoder = encoder;
    job->video_codec = g_enc_opt.video_codec;
    job->video_track = encode_selected_video_track(&g_info, &g_enc_opt);
    job->quality = quality;
    job->preset_level = (g_enc_opt.crf_override <= 0);
    job->quality_level = g_quality_selected;
    job->src = g_info;
    job->clips = 4;
    job->clip_seconds = 4.0;
    thread = CreateThread(NULL, 0, custom_est_thread_proc, job, 0, NULL);
    if (thread) CloseHandle(thread);
    else {
        free(job);
        g_custom_est_testing = 0;
        update_controls_visibility();
    }
}

static void encode_ui_restart_custom_test(HWND hWnd)
{
    int need = (g_enc_opt.encoder_override >= 0 || g_enc_opt.crf_override > 0 ||
                g_enc_opt.video_codec == ENC_VCODEC_H264 ||
                encode_selected_video_track(&g_info, &g_enc_opt) != g_info.video_primary);
    if (!need) {
        if (g_custom_est_testing) g_custom_est_cancel = 1;
        g_custom_est_gen++;
        g_custom_est_ready = 0;
        g_custom_est_testing = 0;
        if (hWnd) {
            update_controls_visibility();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return;
    }
    if (g_custom_est_testing) {
        g_custom_est_cancel = 1;
        g_custom_est_gen++;
        g_custom_est_testing = 0;
    }
    encode_ui_start_custom_test(hWnd);
}

static void encode_ui_on_custom_est_done(HWND hWnd, CustomEstDoneMsg *msg){
    if (!msg) return;
    if (msg->gen != g_custom_est_gen) { free(msg); return; }
    if (msg->result == ESTIMATE_OK) {
        g_custom_est = msg->est;
        g_custom_est_ready = 1;
        if (msg->used_fallback) {
            g_enc_opt.encoder_override = msg->encoder;
            g_quality_encoder = msg->encoder;
            g_quality_last_seconds = 0.0;
            set_status_text(text(STR_QUALITY_FALLBACK), 0);
        } else {
            set_status_id(STR_COMP_ADV_TEST_READY, 0);
        }
    } else if (msg->result == ESTIMATE_CANCELLED) {
        g_custom_est_ready = 0;
    } else {
        g_custom_est_ready = 0;
        set_status_text(text(STR_COMP_ADV_TEST_FAILED), 1);
        gb_message_box(hWnd, text(STR_COMP_ADV_TITLE),
                       text(STR_COMP_ADV_TEST_FAILED), MB_OK | MB_ICONERROR);
    }
    g_custom_est_testing = 0;
    g_custom_est_cancel = 0;
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
    free(msg);
}

static void encode_ui_open_bitrate_dropdown(HWND hWnd)
{
    const wchar_t *ptrs[4];
    RECT anchor;
    int count, selected, i;
    int ac3 = g_enc_opt.audio_codec == ENC_ACODEC_AC3 ||
              g_enc_opt.audio_codec == ENC_ACODEC_EAC3;

    update_bitrate_dropdown_items();
    count = ac3 ? 2 : 4;
    for (i = 0; i < count; i++) ptrs[i] = g_bitrate_items[i];
    if (ac3) selected = (g_enc_opt.bitrate == ENC_ABITRATE_CUSTOM) ? 1 : 0;
    else selected = (g_enc_opt.bitrate >= 0 && g_enc_opt.bitrate < 4)
        ? g_enc_opt.bitrate : 1;

    GetWindowRect(g_hBtnABitrate, &anchor);
    MapWindowPoints(NULL, hWnd, (POINT*)&anchor, 2);
    gb_dropdown_open(hWnd, &anchor, ptrs, count, selected,
                     encode_ui_bitrate_picked, NULL);
}

static void encode_ui_open_custom(HWND hWnd)
{
    wchar_t buf[32];
    if (g_encode_state != 0) return;
    _snwprintf(buf, 32, L"%d", g_enc_opt.custom_kbps);
    buf[31] = L'\0';
    SetWindowTextW(g_hEditCustom, buf);
    SendMessageW(g_hEditCustom, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    g_input_active = 1;
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
}

static void encode_ui_close_custom(void)
{
    g_input_active = 0;
    update_controls_visibility();
    InvalidateRect(g_hWnd, NULL, FALSE);
}

static void encode_ui_accept_custom(HWND hWnd)
{
    wchar_t buf[32];
    wchar_t *end = NULL;
    long value;
    int min_kbps = encode_audio_min_kbps(g_enc_opt.audio_codec);
    int max_kbps = ENC_BITRATE_MAX_KBPS;

    GetWindowTextW(g_hEditCustom, buf, 32);
    buf[31] = L'\0';
    util_trim(buf);
    value = wcstol(buf, &end, 10);
    if (!buf[0] || !end || *end != L'\0' || value < min_kbps || value > max_kbps) {
        gb_message_box(hWnd, text(STR_COMP_CUSTOM_TITLE),
                       text(STR_COMP_CUSTOM_INVALID), MB_OK | MB_ICONWARNING);
        SetFocus(g_hEditCustom);
        return;
    }
    g_enc_opt.custom_kbps = (int)value;
    g_enc_opt.bitrate = ENC_ABITRATE_CUSTOM;
    g_input_active = 0;
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
    SetFocus(g_hBtnStart);
}

typedef struct {
    HWND hwnd;
    int gen;
    SourceInfo src;
    EncodeOptions opt;
    int level;
    int encoder;
} EncodeJob;

static void encode_progress_cb(const EncodeProgress *p, void *user)
{
    EncodeJob *job = (EncodeJob*)user;
    EncodeProgressMsg *msg;
    if (!p || !job) return;
    msg = (EncodeProgressMsg*)malloc(sizeof(EncodeProgressMsg));
    if (!msg) return;
    msg->gen = job->gen;
    msg->progress = *p;
    if (!PostMessageW(job->hwnd, WM_APP_ENCODE_PROGRESS, 0, (LPARAM)msg)) free(msg);
}

static DWORD WINAPI encode_thread_proc(LPVOID param)
{
    EncodeJob *job = (EncodeJob*)param;
    EncodeDoneMsg *msg;
    EncodeError err = ENCODE_ERR_NONE;

    msg = (EncodeDoneMsg*)calloc(1, sizeof(EncodeDoneMsg));
    if (!msg) {
        free(job);
        return 0;
    }
    msg->gen = job->gen;
    msg->result = encode_run(&job->src, job->encoder, job->level, &job->opt,
                             encode_progress_cb, job, &g_encode_cancel,
                             msg->path, UTIL_PATH_MAX, &msg->size,
                             &err, msg->detail, 512);
    msg->error_code = err;
    if (!PostMessageW(job->hwnd, WM_APP_ENCODE_DONE, 0, (LPARAM)msg)) free(msg);
    free(job);
    return 0;
}

static void encode_ui_start(HWND hWnd)
{
    EncodeJob *job;
    HANDLE thread;
    wchar_t path[UTIL_PATH_MAX];
    wchar_t msg_text[512];
    int encoder;

    if (!g_has_info || g_encode_state == 1) return;
    if (g_custom_est_testing) {
        set_status_text(text(STR_COMP_ADV_WAIT), 1);
        return;
    }
    encode_fix_bitrate(&g_enc_opt);

    if (!g_enc_opt.audio_none &&
        (!encode_audio_available(g_enc_opt.container, g_enc_opt.audio_codec) ||
         (g_enc_opt.audio_codec == ENC_ACODEC_COPY &&
          !encode_audio_copy_available_sel(&g_info, &g_enc_opt,
                                           g_enc_opt.container)))) {
        gb_message_box(hWnd, text(STR_COMP_PANEL), text(STR_COMP_ERR_AUDIO),
                       MB_OK | MB_ICONERROR);
        return;
    }

    if (g_enc_opt.container == ENC_CONT_MP4) {
        int bitmap = encode_selected_bitmap_subtitle_count(&g_info, &g_enc_opt);
        if (bitmap > 0) {
            _snwprintf(msg_text, 512, text(STR_COMP_SUBS_DROP_CONFIRM), bitmap);
            msg_text[511] = L'\0';
            if (gb_message_box(hWnd, text(STR_COMP_SUBS_DROP_TITLE), msg_text,
                               MB_YESNO | MB_ICONWARNING) != IDYES) {
                return;
            }
        }
    }

    if (!encode_preview_output_path(&g_info, g_enc_opt.container,
                                    path, UTIL_PATH_MAX)) {
        gb_message_box(hWnd, text(STR_COMP_PANEL), text(STR_COMP_ERR_PATH),
                       MB_OK | MB_ICONERROR);
        return;
    }

    encoder = g_quality_has_result ? g_est.encoder
                                   : estimate_choose_encoder(&g_info, &g_hw_info);

    g_encode_state = 1;
    g_encode_gen++;
    g_encode_cancel = 0;
    memset(&g_encode_progress, 0, sizeof(g_encode_progress));
    g_encode_output[0] = L'\0';
    g_encode_detail[0] = L'\0';
    g_encode_error_code = ENCODE_ERR_NONE;
    g_encode_size = 0;
    g_encode_start_tick = GetTickCount64();
    set_status_id(STR_STATUS_ENCODING, 0);
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
    SetFocus(g_hBtnECancel);

    job = (EncodeJob*)calloc(1, sizeof(EncodeJob));
    if (!job) {
        g_encode_state = 3;
        g_encode_error_code = ENCODE_ERR_OUT_OF_MEMORY;
        set_status_id(STR_STATUS_ENCODE_FAILED, 1);
        update_controls_visibility();
        return;
    }
    job->hwnd = hWnd;
    job->gen = g_encode_gen;
    job->src = g_info;
    job->opt = g_enc_opt;
    job->level = g_quality_selected;
    job->encoder = encoder;

    thread = CreateThread(NULL, 0, encode_thread_proc, job, 0, NULL);
    if (thread) {
        CloseHandle(thread);
    } else {
        free(job);
        g_encode_state = 3;
        g_encode_error_code = ENCODE_ERR_START;
        set_status_id(STR_STATUS_ENCODE_FAILED, 1);
        update_controls_visibility();
    }
}

static void encode_ui_cancel(void)
{
    if (g_encode_state != 1) return;
    g_encode_cancel = 1;
    EnableWindow(g_hBtnECancel, FALSE);
}

static void encode_ui_back(HWND hWnd)
{
    if (g_encode_state == 1) return;
    if (g_custom_est_testing) {
        g_custom_est_cancel = 1;
        g_custom_est_gen++;
        g_custom_est_testing = 0;
        g_custom_est_ready = 0;
    }
    if (g_encode_state == 2) {
        g_step = 1;
        g_encode_state = 0;
        update_controls_visibility();
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }
    if (g_encode_state == 3) {
        g_encode_state = 0;
        update_controls_visibility();
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }
    g_step = 1;
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
}

static void encode_ui_on_done(HWND hWnd, EncodeDoneMsg *msg)
{
    if (!msg) return;
    if (msg->gen != g_encode_gen) {
        free(msg);
        return;
    }

    if (msg->result == ENCODE_OK) {
        g_encode_state = 2;
        g_encode_size = msg->size;
        wcsncpy(g_encode_output, msg->path, UTIL_PATH_MAX - 1);
        g_encode_output[UTIL_PATH_MAX - 1] = L'\0';
        set_status_id(STR_STATUS_ENCODED, 0);
    } else if (msg->result == ENCODE_CANCELLED) {
        g_encode_state = 0;
        g_encode_error_code = ENCODE_ERR_NONE;
        g_encode_detail[0] = L'\0';
        set_status_id(STR_STATUS_ENCODE_CANCELLED, 0);
        gb_message_box(hWnd, text(STR_COMP_PANEL), text(STR_COMP_CANCELED),
                       MB_OK | MB_ICONINFORMATION);
    } else {
        g_encode_state = 3;
        g_encode_error_code = (msg->result == ENCODE_VERIFY_FAILED)
            ? ENCODE_ERR_VERIFY : msg->error_code;
        wcsncpy(g_encode_detail, msg->detail, 511);
        g_encode_detail[511] = L'\0';
        set_status_id(STR_STATUS_ENCODE_FAILED, 1);
    }

    g_encode_cancel = 0;
    update_controls_visibility();
    InvalidateRect(hWnd, NULL, FALSE);
    free(msg);
}

static void choose_file(HWND hWnd)
{
    IFileOpenDialog *pfd = NULL;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IFileOpenDialog, (void**)&pfd);
    if (SUCCEEDED(hr) && pfd) {
        DWORD options = 0;
        COMDLG_FILTERSPEC filters[2];

        filters[0].pszName = text(STR_FILTER_VIDEO);
        filters[0].pszSpec = text(STR_FILTER_VIDEO_PATTERN);
        filters[1].pszName = text(STR_FILTER_ALL);
        filters[1].pszSpec = text(STR_FILTER_ALL_PATTERN);

        if (SUCCEEDED(pfd->lpVtbl->GetOptions(pfd, &options))) {
            pfd->lpVtbl->SetOptions(pfd, options | FOS_FORCEFILESYSTEM |
                                    FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
        }
        pfd->lpVtbl->SetFileTypes(pfd, 2, filters);
        pfd->lpVtbl->SetTitle(pfd, text(STR_DIALOG_CHOOSE_TITLE));

        if (SUCCEEDED(pfd->lpVtbl->Show(pfd, hWnd))) {
            IShellItem *item = NULL;
            if (SUCCEEDED(pfd->lpVtbl->GetResult(pfd, &item)) && item) {
                PWSTR path = NULL;
                if (SUCCEEDED(item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &path)) && path) {
                    start_probe(hWnd, path);
                    CoTaskMemFree(path);
                }
                item->lpVtbl->Release(item);
            }
        }
        pfd->lpVtbl->Release(pfd);
    } else {
        set_status_id(STR_FILE_OPEN_FAILED, 1);
    }
}

/* -------------------------------------------------------------------- WndProc */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = ((LPCREATESTRUCT)lParam)->hInstance;
            g_hWnd = hWnd;
            DragAcceptFiles(hWnd, TRUE);
            g_dpi = query_window_dpi(hWnd);
            update_fixed_metrics();
            gb_set_dpi(g_dpi);
            create_fonts();
            set_status_id(STR_STATUS_READY, 0);

            g_hBtnChoose = CreateWindowExW(0, L"BUTTON", text(STR_BTN_CHOOSE),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 140, 38, hWnd, (HMENU)IDC_BTN_CHOOSE, inst, NULL);
            g_hBtnContinue = CreateWindowExW(0, L"BUTTON", text(STR_BTN_CONTINUE),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 140, 38, hWnd, (HMENU)IDC_BTN_CONTINUE, inst, NULL);
            g_hBtnDetails = CreateWindowExW(0, L"BUTTON", text(STR_BTN_DETAILS),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 150, 26, hWnd, (HMENU)IDC_BTN_DETAILS, inst, NULL);
            g_hBtnRetest = CreateWindowExW(0, L"BUTTON", text(STR_BTN_RETEST),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 110, 38, hWnd, (HMENU)IDC_BTN_RETEST, inst, NULL);
            g_hBtnDepth = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 170, 38, hWnd, (HMENU)IDC_BTN_DEPTH, inst, NULL);
            g_hBtnSkip = CreateWindowExW(0, L"BUTTON", text(STR_HW_SKIP),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 160, 38, hWnd, (HMENU)IDC_BTN_SKIP, inst, NULL);
            g_hBtnBack = CreateWindowExW(0, L"BUTTON", text(STR_QUALITY_BACK),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 100, 38, hWnd, (HMENU)IDC_BTN_BACK, inst, NULL);
            g_hBtnTestAgain = CreateWindowExW(0, L"BUTTON", text(STR_QUALITY_TEST_AGAIN),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 140, 38, hWnd, (HMENU)IDC_BTN_TESTAGAIN, inst, NULL);
            g_hBtnQCancel = CreateWindowExW(0, L"BUTTON", text(STR_QUALITY_CANCEL),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 170, 38, hWnd, (HMENU)IDC_BTN_QCANCEL, inst, NULL);
            g_hBtnCompress = CreateWindowExW(0, L"BUTTON", text(STR_QUALITY_COMPRESS),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 140, 38, hWnd, (HMENU)IDC_BTN_COMPRESS, inst, NULL);
            g_hBtnPreview = CreateWindowExW(0, L"BUTTON", text(STR_QUALITY_PREVIEW),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 110, 38, hWnd, (HMENU)IDC_BTN_PREVIEW, inst, NULL);
            g_hBtnContMkv = CreateWindowExW(0, L"BUTTON", text(STR_COMP_MKV),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 70, 28, hWnd, (HMENU)IDC_BTN_CONT_MKV, inst, NULL);
            g_hBtnContMp4 = CreateWindowExW(0, L"BUTTON", text(STR_COMP_MP4),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 70, 28, hWnd, (HMENU)IDC_BTN_CONT_MP4, inst, NULL);
            g_hBtnVcHevc = CreateWindowExW(0, L"BUTTON", text(STR_COMP_H265),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 150, 28, hWnd, (HMENU)IDC_BTN_VC_HEVC, inst, NULL);
            g_hBtnVcH264 = CreateWindowExW(0, L"BUTTON", text(STR_COMP_H264),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 150, 28, hWnd, (HMENU)IDC_BTN_VC_H264, inst, NULL);
            g_hBtnAudio = CreateWindowExW(0, L"BUTTON", text(STR_COMP_AUDIO_LABEL),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 170, 30, hWnd, (HMENU)IDC_BTN_AUDIO, inst, NULL);
            g_hBtnABitrate = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 170, 30, hWnd, (HMENU)IDC_BTN_ABITRATE, inst, NULL);
            g_hBtnDownmix = CreateWindowExW(0, L"BUTTON", text(STR_COMP_DOWNMIX),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 220, 26, hWnd, (HMENU)IDC_BTN_DOWNMIX, inst, NULL);
            g_hBtnVideoTrack = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 192, 28, hWnd, (HMENU)IDC_BTN_VIDEO_TRACK, inst, NULL);
            g_hBtnAudioTrack = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 146, 30, hWnd, (HMENU)IDC_BTN_AUDIO_TRACK, inst, NULL);
            g_hBtnSubtitleTrack = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 220, 24, hWnd, (HMENU)IDC_BTN_SUBTITLE_TRACK, inst, NULL);
            g_hBtnAdvanced = CreateWindowExW(0, L"BUTTON", text(STR_COMP_ADV_TITLE),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 140, 38, hWnd, (HMENU)IDC_BTN_ADVANCED, inst, NULL);
            g_hBtnAdvEnc = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 300, 30, hWnd, (HMENU)IDC_BTN_ADV_ENC, inst, NULL);
            g_hBtnAdvQ = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 300, 30, hWnd, (HMENU)IDC_BTN_ADV_Q, inst, NULL);
            g_hEditCrf = CreateWindowExW(0, L"EDIT", L"19",
                WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                0, 0, 120, 28, hWnd, (HMENU)IDC_EDIT_CRF, inst, NULL);
            SendMessageW(g_hEditCrf, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
            SendMessageW(g_hEditCrf, EM_SETLIMITTEXT, 2, 0);
            g_hBtnAdvOk = CreateWindowExW(0, L"BUTTON", text(STR_COMP_ADV_APPLY),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 180, 34, hWnd, (HMENU)IDC_BTN_ADV_OK, inst, NULL);
            g_hBtnAdvCancel = CreateWindowExW(0, L"BUTTON", text(STR_COMP_CANCEL),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 120, 34, hWnd, (HMENU)IDC_BTN_ADV_CANCEL, inst, NULL);
            g_hBtnStart = CreateWindowExW(0, L"BUTTON", text(STR_COMP_START),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 180, 38, hWnd, (HMENU)IDC_BTN_START, inst, NULL);
            g_hBtnECancel = CreateWindowExW(0, L"BUTTON", text(STR_COMP_CANCEL),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 170, 38, hWnd, (HMENU)IDC_BTN_ECANCEL, inst, NULL);
            g_hBtnShow = CreateWindowExW(0, L"BUTTON", text(STR_COMP_OPEN_FOLDER),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 180, 38, hWnd, (HMENU)IDC_BTN_SHOW, inst, NULL);
            g_hBtnInputOk = CreateWindowExW(0, L"BUTTON", text(STR_COMP_CUSTOM_OK),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 100, 32, hWnd, (HMENU)IDC_BTN_INPUT_OK, inst, NULL);
            g_hBtnInputCancel = CreateWindowExW(0, L"BUTTON", text(STR_COMP_CANCEL),
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 110, 32, hWnd, (HMENU)IDC_BTN_INPUT_CANCEL, inst, NULL);
            g_hEditCustom = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                0, 0, 220, 28, hWnd, (HMENU)IDC_EDIT_CUSTOM, inst, NULL);
            SendMessageW(g_hEditCustom, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
            SendMessageW(g_hEditCustom, EM_SETLIMITTEXT, 4, 0);
            g_edit_old_proc = (WNDPROC)SetWindowLongPtrW(g_hEditCustom, GWLP_WNDPROC,
                                                        (LONG_PTR)custom_edit_proc);
            g_brushLightest = CreateSolidBrush(GB_LIGHTEST);
            g_hBtnMin = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 26, 24, hWnd, (HMENU)IDC_BTN_MIN, inst, NULL);
            g_hBtnClose = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 26, 24, hWnd, (HMENU)IDC_BTN_CLOSE, inst, NULL);

            track_button(g_hBtnChoose);
            track_button(g_hBtnContinue);
            track_button(g_hBtnDetails);
            track_button(g_hBtnRetest);
            track_button(g_hBtnDepth);
            track_button(g_hBtnSkip);
            track_button(g_hBtnBack);
            track_button(g_hBtnTestAgain);
            track_button(g_hBtnQCancel);
            track_button(g_hBtnCompress);
            track_button(g_hBtnPreview);
            track_button(g_hBtnContMkv);
            track_button(g_hBtnContMp4);
            track_button(g_hBtnVcHevc);
            track_button(g_hBtnVcH264);
            track_button(g_hBtnAudio);
            track_button(g_hBtnABitrate);
            track_button(g_hBtnDownmix);
            track_button(g_hBtnVideoTrack);
            track_button(g_hBtnAudioTrack);
            track_button(g_hBtnSubtitleTrack);
            track_button(g_hBtnAdvanced);
            track_button(g_hBtnAdvEnc);
            track_button(g_hBtnAdvQ);
            track_button(g_hBtnAdvOk);
            track_button(g_hBtnAdvCancel);
            track_button(g_hBtnStart);
            track_button(g_hBtnECancel);
            track_button(g_hBtnShow);
            track_button(g_hBtnInputOk);
            track_button(g_hBtnInputCancel);
            track_button(g_hBtnMin);
            track_button(g_hBtnClose);

            hw_ui_update_depth_label();
            ShowWindow(g_hBtnSkip, SW_HIDE);
            ShowWindow(g_hBtnBack, SW_HIDE);
            ShowWindow(g_hBtnTestAgain, SW_HIDE);
            ShowWindow(g_hBtnQCancel, SW_HIDE);
            ShowWindow(g_hBtnCompress, SW_HIDE);
            ShowWindow(g_hBtnPreview, SW_HIDE);
            EnableWindow(g_hBtnContinue, FALSE);
            layout_controls();
            update_controls_visibility();
            return 0;
        }

        case WM_DPICHANGED: {
            RECT *suggested = (RECT*)lParam;
            g_dpi = HIWORD(wParam);
            if (g_dpi == 0) g_dpi = query_window_dpi(hWnd);
            update_fixed_metrics();
            gb_set_dpi(g_dpi);
            create_fonts();
            if (g_hEditCrf) {
                SendMessageW(g_hEditCrf, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
            }
            if (g_hEditCustom) {
                SendMessageW(g_hEditCustom, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
            }
            if (suggested) {
                SetWindowPos(hWnd, NULL, suggested->left, suggested->top,
                             g_total_w, g_total_h, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            layout_controls();
            update_controls_visibility();
            return 0;
        }

        case WM_GETMINMAXINFO: {
            MINMAXINFO *mmi = (MINMAXINFO*)lParam;
            if (g_total_w > 0 && g_total_h > 0) {
                mmi->ptMinTrackSize.x = g_total_w;
                mmi->ptMaxTrackSize.x = g_total_w;
                mmi->ptMinTrackSize.y = g_total_h;
                mmi->ptMaxTrackSize.y = g_total_h;
            }
            return 0;
        }

        case WM_SIZE: {
            if (wParam != SIZE_MINIMIZED) {
                layout_controls();
                update_controls_visibility();
            }
            return 0;
        }

        case WM_NCHITTEST: {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hWnd, &pt);
            if (pt.y < S(44)) return HTCAPTION;
            break;
        }

        case WM_SETCURSOR: {
            if ((HWND)wParam == hWnd && !g_has_info && !g_probing &&
                !g_hw_testing) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hWnd, &pt);
                if (PtInRect(&g_rc_drop, pt)) {
                    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND));
                    return TRUE;
                }
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            if (g_advanced_active || g_input_active || g_hw_testing) break;
            if (g_step == 1 && !g_quality_testing) {
                quality_select_at(hWnd, pt.x, pt.y);
                return 0;
            }
            if (!g_has_info && !g_probing && !g_hw_testing) {
                if (PtInRect(&g_rc_drop, pt)) {
                    choose_file(hWnd);
                    return 0;
                }
            }
            break;
        }

        case WM_DROPFILES: {
            HDROP drop = (HDROP)wParam;
            wchar_t path[UTIL_PATH_MAX];
            path[0] = L'\0';
            if (g_encode_state == 1) {
                set_status_text(text(STR_COMP_BUSY), 1);
                DragFinish(drop);
                return 0;
            }
            if (g_input_active) encode_ui_close_custom();
            if (DragQueryFileW(drop, 0, path, UTIL_PATH_MAX) > 0) {
                if (g_hw_testing) {
                    wcsncpy(g_hw_pending_path, path, UTIL_PATH_MAX - 1);
                    g_hw_pending_path[UTIL_PATH_MAX - 1] = L'\0';
                    set_status_text(text(STR_HW_QUEUED), 0);
                } else {
                    start_probe(hWnd, path);
                }
            }
            DragFinish(drop);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            HDC mem;
            HBITMAP bmp;
            HGDIOBJ old;
            GetClientRect(hWnd, &rc);
            mem = CreateCompatibleDC(hdc);
            bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            old = SelectObject(mem, bmp);
            paint_main(hWnd, mem);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT*)lParam;
            int role = -1;
            int checked = 0;
            if (!dis) break;
            switch (dis->CtlID) {
                case IDC_BTN_CHOOSE:
                case IDC_BTN_DETAILS:
                case IDC_BTN_RETEST:
                case IDC_BTN_SKIP:
                case IDC_BTN_BACK:
                case IDC_BTN_TESTAGAIN:
                case IDC_BTN_PREVIEW:
                case IDC_BTN_INPUT_CANCEL:
                case IDC_BTN_ADVANCED:
                case IDC_BTN_ADV_CANCEL:
                    role = GB_ROLE_PUSH;
                    break;
                case IDC_BTN_CONTINUE:
                case IDC_BTN_COMPRESS:
                case IDC_BTN_START:
                case IDC_BTN_SHOW:
                case IDC_BTN_INPUT_OK:
                case IDC_BTN_ADV_OK:
                    role = GB_ROLE_PRIMARY;
                    break;
                case IDC_BTN_QCANCEL:
                case IDC_BTN_ECANCEL:
                    role = GB_ROLE_DANGER;
                    break;
                case IDC_BTN_DEPTH:
                case IDC_BTN_AUDIO:
                case IDC_BTN_ABITRATE:
                case IDC_BTN_VIDEO_TRACK:
                case IDC_BTN_AUDIO_TRACK:
                case IDC_BTN_SUBTITLE_TRACK:
                case IDC_BTN_ADV_ENC:
                case IDC_BTN_ADV_Q:
                    role = GB_ROLE_DROP;
                    break;
                case IDC_BTN_CONT_MKV:
                    role = GB_ROLE_CHIP;
                    checked = (g_enc_opt.container == ENC_CONT_MKV);
                    break;
                case IDC_BTN_CONT_MP4:
                    role = GB_ROLE_CHIP;
                    checked = (g_enc_opt.container == ENC_CONT_MP4);
                    break;
                case IDC_BTN_VC_HEVC:
                    role = GB_ROLE_CHIP;
                    checked = (g_enc_opt.video_codec == ENC_VCODEC_HEVC);
                    break;
                case IDC_BTN_VC_H264:
                    role = GB_ROLE_CHIP;
                    checked = (g_enc_opt.video_codec == ENC_VCODEC_H264);
                    break;
                case IDC_BTN_DOWNMIX:
                    role = GB_ROLE_CHECK;
                    checked = g_enc_opt.downmix_stereo ||
                              encode_audio_force_stereo(g_enc_opt.audio_codec);
                    break;
                case IDC_BTN_MIN:
                    role = GB_ROLE_TITLE;
                    break;
                case IDC_BTN_CLOSE:
                    role = GB_ROLE_TITLE;
                    checked = 1;
                    break;
                default:
                    break;
            }
            if (role >= 0) {
                HFONT font = g_fontBold;
                if (dis->CtlID == IDC_BTN_DETAILS ||
                    dis->CtlID == IDC_BTN_RETEST ||
                    dis->CtlID == IDC_BTN_DEPTH ||
                    dis->CtlID == IDC_BTN_SKIP ||
                    dis->CtlID == IDC_BTN_BACK ||
                    dis->CtlID == IDC_BTN_TESTAGAIN ||
                    dis->CtlID == IDC_BTN_PREVIEW ||
                    dis->CtlID == IDC_BTN_QCANCEL ||
                    dis->CtlID == IDC_BTN_COMPRESS ||
                    dis->CtlID == IDC_BTN_CONT_MKV ||
                    dis->CtlID == IDC_BTN_CONT_MP4 ||
                    dis->CtlID == IDC_BTN_VC_HEVC ||
                    dis->CtlID == IDC_BTN_VC_H264 ||
                    dis->CtlID == IDC_BTN_AUDIO ||
                    dis->CtlID == IDC_BTN_ABITRATE ||
                    dis->CtlID == IDC_BTN_VIDEO_TRACK ||
                    dis->CtlID == IDC_BTN_AUDIO_TRACK ||
                    dis->CtlID == IDC_BTN_SUBTITLE_TRACK ||
                    dis->CtlID == IDC_BTN_ADVANCED ||
                    dis->CtlID == IDC_BTN_ADV_ENC ||
                    dis->CtlID == IDC_BTN_ADV_Q ||
                    dis->CtlID == IDC_BTN_ADV_CANCEL ||
                    dis->CtlID == IDC_BTN_DOWNMIX ||
                    dis->CtlID == IDC_BTN_ECANCEL ||
                    dis->CtlID == IDC_BTN_INPUT_CANCEL) {
                    font = g_fontSmall;
                }
                if (dis->CtlID == IDC_BTN_COMPRESS ||
                    dis->CtlID == IDC_BTN_START ||
                    dis->CtlID == IDC_BTN_SHOW ||
                    dis->CtlID == IDC_BTN_INPUT_OK ||
                    dis->CtlID == IDC_BTN_ADV_OK) {
                    font = g_fontBold;
                }
                gb_draw_control(dis, font, role, checked, tracked_hover(dis->hwndItem));
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (code != BN_CLICKED && code != BN_DOUBLECLICKED) break;
            switch (id) {
                case IDC_BTN_CHOOSE:
                    if (!g_probing) choose_file(hWnd);
                    return 0;
                case IDC_BTN_CONTINUE:
                    if (g_has_info) {
                        g_step = 1;
                        quality_ui_update_selection();
                        if (g_quality_has_result &&
                            g_est.profile == quality_current_profile()) {
                            update_controls_visibility();
                            InvalidateRect(hWnd, NULL, FALSE);
                        } else {
                            g_quality_deep = 0;
                            quality_ui_start(hWnd, 0);
                        }
                    }
                    return 0;
                case IDC_BTN_BACK:
                    if (g_step == 2) encode_ui_back(hWnd);
                    else quality_ui_back(hWnd);
                    return 0;
                case IDC_BTN_TESTAGAIN:
                    quality_ui_test_again(hWnd);
                    return 0;
                case IDC_BTN_PREVIEW:
                    quality_ui_open_preview(hWnd);
                    return 0;
                case IDC_BTN_QCANCEL:
                    quality_ui_cancel();
                    return 0;
                case IDC_BTN_COMPRESS:
                    quality_ui_compress(hWnd);
                    return 0;
                case IDC_BTN_CONT_MKV:
                case IDC_BTN_CONT_MP4: {
                    int new_container = (id == IDC_BTN_CONT_MP4)
                        ? ENC_CONT_MP4 : ENC_CONT_MKV;
                    if (g_encode_state != 0 || g_step != 2) return 0;
                    g_enc_opt.container = new_container;
                    if (g_info.audio_track_count <= 0) {
                        g_enc_opt.audio_codec = ENC_ACODEC_COPY;
                    } else if (!encode_audio_available(new_container,
                                                      g_enc_opt.audio_codec) ||
                               (g_enc_opt.audio_codec == ENC_ACODEC_COPY &&
                                !encode_audio_copy_available_sel(&g_info,
                                                                 &g_enc_opt,
                                                                 new_container))) {
                        g_enc_opt.audio_codec = encode_default_audio(new_container);
                    }
                    encode_fix_bitrate(&g_enc_opt);
                    update_controls_visibility();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                case IDC_BTN_VC_HEVC:
                case IDC_BTN_VC_H264:
                    if (g_encode_state != 0 || g_step != 2) return 0;
                    g_enc_opt.video_codec = (id == IDC_BTN_VC_H264)
                        ? ENC_VCODEC_H264 : ENC_VCODEC_HEVC;
                    update_controls_visibility();
                    InvalidateRect(hWnd, NULL, FALSE);
                    encode_ui_restart_custom_test(hWnd);
                    return 0;
                case IDC_BTN_AUDIO:
                    if (g_encode_state == 0 && g_step == 2 &&
                        g_info.audio_track_count > 0) {
                        encode_ui_open_audio_dropdown(hWnd);
                    }
                    return 0;
                case IDC_BTN_AUDIO_TRACK:
                    if (g_encode_state == 0 && g_step == 2 &&
                        g_info.audio_tracks_stored > 1) {
                        encode_ui_open_audio_track_dropdown(hWnd);
                    }
                    return 0;
                case IDC_BTN_VIDEO_TRACK:
                    if (g_encode_state == 0 && g_step == 2 &&
                        g_info.video_real_count > 1) {
                        encode_ui_open_video_track_dropdown(hWnd);
                    }
                    return 0;
                case IDC_BTN_SUBTITLE_TRACK:
                    if (g_encode_state == 0 && g_step == 2 &&
                        g_info.subtitle_track_count > 0) {
                        encode_ui_open_subtitle_track_dropdown(hWnd);
                    }
                    return 0;
                case IDC_BTN_ADVANCED:
                    if (g_encode_state == 0) encode_ui_open_advanced(hWnd);
                    return 0;
                case IDC_BTN_ADV_ENC:
                    if (g_advanced_active) encode_ui_open_adv_encoder_dropdown(hWnd);
                    return 0;
                case IDC_BTN_ADV_Q:
                    if (g_advanced_active) encode_ui_open_adv_quality_dropdown(hWnd);
                    return 0;
                case IDC_BTN_ADV_OK:
                    if (g_advanced_active) encode_ui_accept_advanced(hWnd);
                    return 0;
                case IDC_BTN_ADV_CANCEL:
                    if (g_advanced_active) encode_ui_close_advanced();
                    return 0;
                case IDC_BTN_ABITRATE:
                    if (g_encode_state == 0 && g_step == 2 &&
                        encode_audio_has_bitrate(g_enc_opt.audio_codec)) {
                        encode_ui_open_bitrate_dropdown(hWnd);
                    }
                    return 0;
                case IDC_BTN_DOWNMIX:
                    if (g_encode_state == 0 && g_step == 2 &&
                        !encode_audio_force_stereo(g_enc_opt.audio_codec)) {
                        g_enc_opt.downmix_stereo = !g_enc_opt.downmix_stereo;
                        update_controls_visibility();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;
                case IDC_BTN_START:
                    encode_ui_start(hWnd);
                    return 0;
                case IDC_BTN_ECANCEL:
                    encode_ui_cancel();
                    return 0;
                case IDC_BTN_SHOW:
                    if (g_encode_state == 2) encode_open_folder(g_encode_output);
                    return 0;
                case IDC_BTN_INPUT_OK:
                    encode_ui_accept_custom(hWnd);
                    return 0;
                case IDC_BTN_INPUT_CANCEL:
                    encode_ui_close_custom();
                    return 0;
                case IDC_BTN_DETAILS:
                    if (g_has_info) {
                        g_show_details = !g_show_details;
                        update_controls_visibility();
                    }
                    return 0;
                case IDC_BTN_RETEST:
                    if (!g_hw_testing && !g_probing) hw_ui_start(hWnd, 0, NULL);
                    return 0;
                case IDC_BTN_DEPTH:
                    if (!g_hw_testing) hw_ui_open_depth_dropdown();
                    return 0;
                case IDC_BTN_SKIP:
                    hw_ui_skip();
                    return 0;
                case IDC_BTN_MIN:
                    ShowWindow(hWnd, SW_MINIMIZE);
                    return 0;
                case IDC_BTN_CLOSE:
                    DestroyWindow(hWnd);
                    return 0;
                default:
                    break;
            }
            break;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                if (g_input_active) {
                    encode_ui_close_custom();
                } else if (g_advanced_active) {
                    encode_ui_close_advanced();
                } else if (g_step == 2) {
                    if (g_encode_state == 1) encode_ui_cancel();
                    else encode_ui_back(hWnd);
                } else if (g_step == 1) {
                    if (g_quality_testing) quality_ui_cancel();
                    else quality_ui_back(hWnd);
                } else if (g_show_details) {
                    g_show_details = 0;
                    update_controls_visibility();
                } else {
                    DestroyWindow(hWnd);
                }
                return 0;
            }
            break;

        case WM_APP_PROBE_DONE: {
            ProbeDoneMsg *res = (ProbeDoneMsg*)lParam;
            if (!res) return 0;
            g_probing = 0;
            if (res->error == PROBE_OK) {
                g_info = res->info;
                g_has_info = 1;
                g_show_details = 0;
                set_status_id(STR_STATUS_ANALYZED, 0);
            } else {
                g_has_info = 0;
                set_status_text(probe_error_text(res->error), 1);
                {
                    const wchar_t *title = (res->error == PROBE_ERR_NOT_FOUND)
                        ? text(STR_MSG_MISSING_TOOLS_TITLE) : text(STR_MSG_PROBE_TITLE);
                    gb_message_box(hWnd, title, probe_error_text(res->error), MB_OK | MB_ICONERROR);
                }
            }
            free(res);
            update_controls_visibility();
            return 0;
        }

        case WM_APP_HW_PROGRESS: {
            HwProgress *p = (HwProgress*)lParam;
            if (p) {
                g_hw_progress = *p;
                if (p->event == HW_EVT_ERROR) g_hw_error = p->error;
                free(p);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_APP_HW_DONE: {
            HwDoneMsg *m = (HwDoneMsg*)lParam;
            hw_ui_on_done(hWnd, m);
            return 0;
        }

        case WM_APP_EST_PROGRESS: {
            EstProgressMsg *p = (EstProgressMsg*)lParam;
            if (p) {
                if (p->gen == g_quality_gen && g_quality_testing) {
                    g_est_progress = p->progress;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                free(p);
            }
            return 0;
        }

        case WM_APP_EST_DONE: {
            EstimateDoneMsg *m = (EstimateDoneMsg*)lParam;
            quality_ui_on_done(hWnd, m);
            return 0;
        }

        case WM_APP_CUSTOM_EST_PROGRESS: {
            CustomEstProgressMsg *p = (CustomEstProgressMsg*)lParam;
            if (p) {
                if (p->gen == g_custom_est_gen) {
                    g_custom_est_progress = p->progress;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                free(p);
            }
            return 0;
        }

        case WM_APP_CUSTOM_EST_DONE: {
            CustomEstDoneMsg *m = (CustomEstDoneMsg*)lParam;
            encode_ui_on_custom_est_done(hWnd, m);
            return 0;
        }

        case WM_APP_ENCODE_PROGRESS: {
            EncodeProgressMsg *p = (EncodeProgressMsg*)lParam;
            if (p) {
                if (p->gen == g_encode_gen && g_encode_state == 1) {
                    g_encode_progress = p->progress;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                free(p);
            }
            return 0;
        }

        case WM_APP_ENCODE_DONE: {
            EncodeDoneMsg *m = (EncodeDoneMsg*)lParam;
            encode_ui_on_done(hWnd, m);
            return 0;
        }

        case WM_APP_OPEN_CUSTOM:
            if (g_step == 2 && g_encode_state == 0) encode_ui_open_custom(hWnd);
            return 0;

        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, GB_DARKEST);
            SetBkColor(dc, GB_LIGHTEST);
            return (LRESULT)(g_brushLightest ? g_brushLightest
                                             : GetStockObject(WHITE_BRUSH));
        }

        case WM_CLOSE:
            if (g_encode_state == 1) {
                if (gb_message_box(hWnd, text(STR_COMP_CLOSE_TITLE),
                                   text(STR_COMP_CLOSE_TEXT),
                                   MB_YESNO | MB_ICONWARNING) != IDYES) {
                    return 0;
                }
                g_encode_cancel = 1;
            }
            if (g_hw_testing) g_hw_cancel = 1;
            if (g_quality_testing) g_est_cancel = 1;
            if (g_custom_est_testing) g_custom_est_cancel = 1;
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            if (g_hw_testing) g_hw_cancel = 1;
            if (g_quality_testing) g_est_cancel = 1;
            if (g_custom_est_testing) g_custom_est_cancel = 1;
            if (g_encode_state == 1) g_encode_cancel = 1;
            quality_cleanup_previews();
            if (g_brushLightest) {
                DeleteObject(g_brushLightest);
                g_brushLightest = NULL;
            }
            destroy_fonts();
            g_hWnd = NULL;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int ui_run(HINSTANCE hInstance, int nCmdShow, const wchar_t *initial_path)
{
    WNDCLASSEXW wc;
    HWND hWnd;
    MSG msg;
    RECT rc;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    estimate_cleanup_stale_temp();

    g_dpi = query_window_dpi(NULL);
    update_fixed_metrics();

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = UI_WINDOW_CLASS;
    wc.style = CS_DROPSHADOW;

    if (!RegisterClassExW(&wc)) {
        CoUninitialize();
        return 0;
    }

    g_hw_override = hw_get_override();

    rc.left = 0; rc.top = 0; rc.right = S(BASE_CLIENT_W); rc.bottom = S(BASE_CLIENT_H);
    adjust_window_rect_for_dpi(&rc, APP_WINDOW_STYLE, FALSE, APP_WINDOW_EX_STYLE, g_dpi);

    hWnd = CreateWindowExW(APP_WINDOW_EX_STYLE, UI_WINDOW_CLASS, text(STR_WINDOW_TITLE),
        APP_WINDOW_STYLE, CW_USEDEFAULT, CW_USEDEFAULT,
        g_total_w, g_total_h, NULL, NULL, hInstance, NULL);
    if (!hWnd) {
        CoUninitialize();
        return 0;
    }

    {
        RECT work;
        int x, y;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + (work.right - work.left - g_total_w) / 2;
        y = work.top + (work.bottom - work.top - g_total_h) / 2;
        SetWindowPos(hWnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    SetFocus(g_hBtnChoose);

    if (hw_load(&g_hw_info)) {
        g_hw_has_info = 1;
        if (g_hw_info.total_seconds > 0.0) {
            g_hw_est_seconds = (int)(g_hw_info.total_seconds + 0.5);
        }
        if (hw_needs_retest(&g_hw_info)) {
            hw_ui_start(hWnd, 1, initial_path);
        } else {
            hw_ui_refresh_ready_status();
            if (initial_path && *initial_path) start_probe(hWnd, initial_path);
        }
    } else {
        hw_ui_start(hWnd, 1, initial_path);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CoUninitialize();
    return (int)msg.wParam;
}
