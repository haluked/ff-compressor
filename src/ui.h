/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UI_H
#define UI_H

#include <windows.h>

#define WM_APP_PROBE_DONE (WM_APP + 1)
#define UI_WINDOW_CLASS   L"FfCompressorWindow"

int ui_run(HINSTANCE hInstance, int nCmdShow, const wchar_t *initial_path);

#endif /* UI_H */
