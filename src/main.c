/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui.h"
#include "engine.h"
#include "strings.h"
#include "util.h"

#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdio.h>

#define APP_VERSION_STR L"1.0.0"
#define APP_MUTEX_NAME  L"Local\\ff-compressor"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    PWSTR pCmdLine, int nCmdShow)
{
    wchar_t initial_path[UTIL_PATH_MAX];
    LPWSTR *argv;
    int argc = 0;
    HANDLE mutex;
    int ret;

    (void)hPrevInstance;
    (void)pCmdLine;

    initial_path[0] = L'\0';
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {
        if (_wcsicmp(argv[1], L"--version") == 0 || _wcsicmp(argv[1], L"-v") == 0) {
            wchar_t buf[128];
            _snwprintf(buf, 128, text(STR_VERSION_FMT), text(STR_APP_NAME), APP_VERSION_STR);
            buf[127] = L'\0';
            MessageBoxW(NULL, buf, text(STR_WINDOW_TITLE), MB_OK | MB_ICONINFORMATION);
            LocalFree(argv);
            return 0;
        }
        if (argv[1][0] != L'-') {
            wcsncpy(initial_path, argv[1], UTIL_PATH_MAX - 1);
            initial_path[UTIL_PATH_MAX - 1] = L'\0';
            util_trim(initial_path);
        }
    }
    if (argv) LocalFree(argv);

    mutex = CreateMutexW(NULL, TRUE, APP_MUTEX_NAME);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(UI_WINDOW_CLASS, NULL);
        if (existing) {
            ShowWindow(existing, SW_SHOW);
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }

    engine_init();
    ret = ui_run(hInstance, nCmdShow, initial_path[0] ? initial_path : NULL);
    engine_cleanup();

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return ret;
}
