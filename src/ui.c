/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui.h"
#include "engine.h"
#include "probe.h"
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

typedef struct {
    ProbeError error;
    SourceInfo info;
} ProbeDoneMsg;

static void layout_controls(void);
static void update_controls_visibility(void);
static void start_probe(HWND hWnd, const wchar_t *path);
static void set_status_text(const wchar_t *s, int is_error);

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
    draw_step_tab(hdc, &r, text(STR_STEP_VIDEO), 1);
    x += w + gap;
    r.left = x; r.right = x + w;
    draw_step_tab(hdc, &r, text(STR_STEP_QUALITY), 0);
    x += w + gap;
    r.left = x; r.right = x + w;
    draw_step_tab(hdc, &r, text(STR_STEP_COMPRESS), 0);
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
    draw_panel(hdc, &g_rc_panel, text(STR_PANEL_VIDEO));

    if (g_probing) {
        draw_probing(hdc);
    } else if (g_has_info) {
        if (g_show_details) draw_details(hdc, &g_info);
        else draw_summary(hdc, &g_info);
    } else {
        draw_empty(hdc, NULL);
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
    SetWindowPos(g_hBtnContinue, NULL, cw - S(32) - btn_w, S(388), btn_w, btn_h, SWP_NOZORDER);
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
    ShowWindow(g_hBtnDetails, g_has_info ? SW_SHOW : SW_HIDE);
    SetWindowTextW(g_hBtnDetails, text(g_show_details ? STR_BTN_HIDE_DETAILS : STR_BTN_DETAILS));
    EnableWindow(g_hBtnContinue, (g_has_info && !g_probing) ? TRUE : FALSE);
    EnableWindow(g_hBtnChoose, g_probing ? FALSE : TRUE);
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
            g_hBtnMin = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 26, 24, hWnd, (HMENU)IDC_BTN_MIN, inst, NULL);
            g_hBtnClose = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 26, 24, hWnd, (HMENU)IDC_BTN_CLOSE, inst, NULL);

            track_button(g_hBtnChoose);
            track_button(g_hBtnContinue);
            track_button(g_hBtnDetails);
            track_button(g_hBtnMin);
            track_button(g_hBtnClose);

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
            if (!g_has_info && !g_probing) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
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
                start_probe(hWnd, path);
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
                    role = GB_ROLE_PUSH;
                    break;
                case IDC_BTN_CONTINUE:
                    role = GB_ROLE_PRIMARY;
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
                if (dis->CtlID == IDC_BTN_DETAILS) font = g_fontSmall;
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
                        gb_message_box(hWnd, text(STR_MSG_CONTINUE_TITLE),
                                       text(STR_MSG_CONTINUE_TEXT), MB_OK | MB_ICONINFORMATION);
                    }
                    return 0;
                case IDC_BTN_DETAILS:
                    if (g_has_info) {
                        g_show_details = !g_show_details;
                        update_controls_visibility();
                    }
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
                if (g_show_details) {
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

        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
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
    if (initial_path && *initial_path) start_probe(hWnd, initial_path);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CoUninitialize();
    return (int)msg.wParam;
}
