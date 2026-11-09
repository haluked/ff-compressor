/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "engine.h"

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct {
    wchar_t path[UTIL_PATH_MAX];
    EngineProbeCallback callback;
    void *user;
} ProbeJob;

static CRITICAL_SECTION g_cs;
static int g_busy = 0;
static int g_cancel_requested = 0;
static volatile LONG g_active_workers = 0;
static HANDLE g_workers_done = NULL;

int engine_init(void)
{
    InitializeCriticalSection(&g_cs);
    g_busy = 0;
    g_cancel_requested = 0;
    g_active_workers = 0;
    g_workers_done = CreateEventW(NULL, TRUE, TRUE, NULL);
    return 1;
}

static void worker_started(void)
{
    InterlockedIncrement(&g_active_workers);
    if (g_workers_done) ResetEvent(g_workers_done);
}

static void worker_finished(void)
{
    if (InterlockedDecrement(&g_active_workers) == 0 && g_workers_done) {
        SetEvent(g_workers_done);
    }
}

void engine_cleanup(void)
{
    engine_cancel();
    if (g_workers_done) WaitForSingleObject(g_workers_done, 5000);
    if (InterlockedCompareExchange(&g_active_workers, 0, 0) == 0) {
        DeleteCriticalSection(&g_cs);
        if (g_workers_done) {
            CloseHandle(g_workers_done);
            g_workers_done = NULL;
        }
    }
}

int engine_is_busy(void)
{
    int busy;
    EnterCriticalSection(&g_cs);
    busy = g_busy;
    LeaveCriticalSection(&g_cs);
    return busy;
}

void engine_cancel(void)
{
    EnterCriticalSection(&g_cs);
    g_cancel_requested = 1;
    LeaveCriticalSection(&g_cs);
}

static DWORD WINAPI probe_thread(LPVOID param)
{
    ProbeJob *job = (ProbeJob*)param;
    SourceInfo info;
    ProbeError err;

    memset(&info, 0, sizeof(info));
    err = probe_run(job->path, &info);

    EnterCriticalSection(&g_cs);
    g_busy = 0;
    g_cancel_requested = 0;
    LeaveCriticalSection(&g_cs);

    if (job->callback) job->callback(err, (err == PROBE_OK) ? &info : NULL, job->user);
    free(job);
    return 0;
}

static DWORD WINAPI probe_thread_proc(LPVOID param)
{
    DWORD rc = probe_thread(param);
    worker_finished();
    return rc;
}

int engine_probe_async(const wchar_t *path, EngineProbeCallback callback, void *user)
{
    ProbeJob *job;
    HANDLE thread;

    if (!path || !*path) return 0;

    EnterCriticalSection(&g_cs);
    if (g_busy) {
        LeaveCriticalSection(&g_cs);
        return 0;
    }
    g_busy = 1;
    g_cancel_requested = 0;
    LeaveCriticalSection(&g_cs);

    job = (ProbeJob*)calloc(1, sizeof(ProbeJob));
    if (!job) {
        EnterCriticalSection(&g_cs);
        g_busy = 0;
        LeaveCriticalSection(&g_cs);
        return 0;
    }
    wcsncpy(job->path, path, UTIL_PATH_MAX - 1);
    job->path[UTIL_PATH_MAX - 1] = L'\0';
    job->callback = callback;
    job->user = user;

    worker_started();
    thread = CreateThread(NULL, 0, probe_thread_proc, job, 0, NULL);
    if (thread) {
        CloseHandle(thread);
        return 1;
    }
    worker_finished();
    EnterCriticalSection(&g_cs);
    g_busy = 0;
    LeaveCriticalSection(&g_cs);
    free(job);
    return 0;
}
