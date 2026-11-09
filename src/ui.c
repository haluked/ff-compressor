/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui.h"
#include "engine.h"
#include "hw.h"
#include "probe.h"
#include "presets.h"
#include "estimate.h"
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

#define BASE_CLIENT_W 646
#define BASE_CLIENT_H 485

#define APP_WINDOW_STYLE    (WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU)
#define APP_WINDOW_EX_STYLE (WS_EX_APPWINDOW | WS_EX_CONTROLPARENT)

#define GB_MAX_TRACKED 8
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

static void layout_controls(void);
static void update_controls_visibility(void);
static void start_probe(HWND hWnd, const wchar_t *path);
static void set_status_text(const wchar_t *s, int is_error);
static void hw_ui_start(HWND hWnd, int auto_test, const wchar_t *pending_path);
static void hw_ui_update_depth_label(void);
static void hw_ui_open_depth_dropdown(void);
static void quality_ui_start(HWND hWnd, int force);
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

static int quality_has_savings(void)
{
    int i;
    if (!g_quality_has_result || g_info.file_size <= 0) return 0;
    for (i = 0; i < g_est.level_count; i++) {
        EstimateLevel *e = &g_est.levels[i];
        if (e->available && e->est_size_bytes < (double)g_info.file_size * 0.80) {
            return 1;
        }
    }
    return 0;
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

static void draw_quality(HDC hdc)
{
    wchar_t buf[512];
    wchar_t size_lo[64], size_hi[64], time_lo[64], time_hi[64];
    wchar_t size_mid[64], time_mid[64];
    RECT tr;
    int levels[ESTIMATE_MAX_LEVELS];
    int count;
    int i;
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
            RECT nr = { r.left + S(8), r.top + S(30), r.right - S(8), r.top + S(48) };
            gb_text(hdc, g_fontSmall, ink, &nr, presets_level_note(level),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        if (g_quality_has_result) e = quality_level_result(level);
        if (e && e->available) {
            util_format_bytes((long long)e->est_size_bytes, size_mid, 64);
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
            if (g_info.file_size > 0 && e->est_size_bytes > 0.0) {
                int save = (int)((1.0 - e->est_size_bytes / (double)g_info.file_size) * 100.0);
                if (e->est_size_bytes > (double)g_info.file_size) {
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

    /* Selected summary and notes. */
    {
        EstimateLevel *sel = quality_level_result(g_quality_selected);
        int enc = g_quality_has_result ? g_est.encoder
                                       : estimate_choose_encoder(&g_info, &g_hw_info);
        RECT sr = { S(32), S(288), S(614), S(306) };
        if (sel && sel->available) {
            wchar_t left[128], right[128];
            util_format_bytes((long long)sel->est_size_min, size_lo, 64);
            util_format_bytes((long long)sel->est_size_max, size_hi, 64);
            util_format_duration(sel->est_seconds_min, time_lo, 64);
            util_format_duration(sel->est_seconds_max, time_hi, 64);
            _snwprintf(buf, 512, L"%ls  ", presets_level_name(g_quality_selected));
            buf[511] = L'\0';
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

        _snwprintf(buf, 512, text(STR_QUALITY_ENCODER_FMT), hw_encoder_name(enc));
        buf[511] = L'\0';
        SetRect(&sr, S(32), S(306), S(614), S(322));
        gb_text(hdc, g_fontSmall, GB_DARKEST, &sr, buf,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        SetRect(&sr, S(32), S(322), S(614), S(348));
        gb_text(hdc, g_fontSmall, GB_DARK, &sr, text(STR_QUALITY_COMPAT),
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

        SetRect(&sr, S(32), S(348), S(614), S(362));
        gb_text(hdc, g_fontSmall, GB_DARK, &sr, text(STR_QUALITY_AUDIO_NOTE),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        SetRect(&sr, S(32), S(362), S(614), S(376));
        if (g_est.used_fallback) {
            gb_text(hdc, g_fontSmallBold, GB_DARKEST, &sr, text(STR_QUALITY_FALLBACK),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else if (!quality_has_savings()) {
            gb_text(hdc, g_fontSmallBold, GB_DARKEST, &sr, text(STR_QUALITY_EFFICIENT),
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
    if (g_hw_testing) {
        draw_panel(hdc, &g_rc_panel, text(STR_HW_PANEL));
        draw_hw_test(hdc);
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

static void update_controls_visibility(void)
{
    if (!g_hWnd) return;

    if (g_hw_testing) {
        ShowWindow(g_hBtnChoose, SW_HIDE);
        ShowWindow(g_hBtnContinue, SW_HIDE);
        ShowWindow(g_hBtnDetails, SW_HIDE);
        ShowWindow(g_hBtnRetest, SW_HIDE);
        ShowWindow(g_hBtnDepth, SW_HIDE);
        ShowWindow(g_hBtnBack, SW_HIDE);
        ShowWindow(g_hBtnTestAgain, SW_HIDE);
        ShowWindow(g_hBtnQCancel, SW_HIDE);
        ShowWindow(g_hBtnCompress, SW_HIDE);
        ShowWindow(g_hBtnSkip, SW_SHOW);
        EnableWindow(g_hBtnSkip, g_hw_skipped ? FALSE : TRUE);
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
    result = estimate_run(&g_info, &g_hw_info, quality_current_profile(),
                          &msg->est, quality_progress_cb, job, &g_est_cancel);
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
    g_quality_gen++;
    quality_cleanup_previews();
    g_est_cancel = 0;
    g_est_start_tick = GetTickCount64();
    memset(&g_est, 0, sizeof(g_est));
    memset(&g_est_progress, 0, sizeof(g_est_progress));
    g_est.encoder = estimate_choose_encoder(&g_info, &g_hw_info);
    g_est_seconds = (g_quality_last_seconds > 0.0)
        ? (int)(g_quality_last_seconds + 0.5)
        : estimate_demo_seconds(&g_info, &g_hw_info, quality_current_profile());
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
    gb_message_box(hWnd, text(STR_QUALITY_PANEL), text(STR_QUALITY_M5_NOTE),
                   MB_OK | MB_ICONINFORMATION);
}

static void quality_select_at(HWND hWnd, int x, int y)
{
    int i;
    POINT pt;
    pt.x = x;
    pt.y = y;
    if (g_quality_testing || !g_quality_has_result) return;
    if (g_quality_preview) {
        for (i = 0; i < g_preview_chip_count; i++) {
            if (PtInRect(&g_rc_preview_chips[i], pt)) {
                int level = g_est.levels_used[i];
                if (level != g_quality_selected) {
                    g_quality_selected = level;
                    quality_load_preview();
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return;
            }
        }
        return;
    }
    for (i = 0; i < g_card_count; i++) {
        if (PtInRect(&g_rc_cards[i], pt)) {
            int level = g_est.levels_used[i];
            if (level != g_quality_selected) {
                g_quality_selected = level;
                InvalidateRect(hWnd, NULL, FALSE);
            }
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
            if ((HWND)wParam == hWnd && !g_has_info && !g_probing) {
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
            if (g_step == 1 && !g_quality_testing) {
                quality_select_at(hWnd, pt.x, pt.y);
                return 0;
            }
            if (!g_has_info && !g_probing) {
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
                    role = GB_ROLE_PUSH;
                    break;
                case IDC_BTN_CONTINUE:
                case IDC_BTN_COMPRESS:
                    role = GB_ROLE_PRIMARY;
                    break;
                case IDC_BTN_QCANCEL:
                    role = GB_ROLE_DANGER;
                    break;
                case IDC_BTN_DEPTH:
                    role = GB_ROLE_DROP;
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
                    dis->CtlID == IDC_BTN_COMPRESS) {
                    font = g_fontSmall;
                }
                if (dis->CtlID == IDC_BTN_COMPRESS) font = g_fontBold;
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
                            quality_ui_start(hWnd, 0);
                        }
                    }
                    return 0;
                case IDC_BTN_BACK:
                    quality_ui_back(hWnd);
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
                if (g_step == 1) {
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

        case WM_CLOSE:
            if (g_hw_testing) g_hw_cancel = 1;
            if (g_quality_testing) g_est_cancel = 1;
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            if (g_hw_testing) g_hw_cancel = 1;
            if (g_quality_testing) g_est_cancel = 1;
            quality_cleanup_previews();
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
