/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UI_H
#define UI_H

#include <windows.h>

#define WM_APP_PROBE_DONE   (WM_APP + 1)
#define WM_APP_HW_PROGRESS  (WM_APP + 2)
#define WM_APP_HW_DONE      (WM_APP + 3)
#define WM_APP_EST_PROGRESS (WM_APP + 4)
#define WM_APP_EST_DONE     (WM_APP + 5)
#define WM_APP_ENCODE_PROGRESS (WM_APP + 6)
#define WM_APP_ENCODE_DONE     (WM_APP + 7)
#define WM_APP_OPEN_CUSTOM     (WM_APP + 8)
#define UI_WINDOW_CLASS     L"FfCompressorWindow"

int ui_run(HINSTANCE hInstance, int nCmdShow, const wchar_t *initial_path);

#endif /* UI_H */
