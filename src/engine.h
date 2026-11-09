/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ENGINE_H
#define ENGINE_H

#include <windows.h>
#include "probe.h"

typedef void (*EngineProbeCallback)(ProbeError error, const SourceInfo *info, void *user);

int  engine_init(void);
void engine_cleanup(void);

int  engine_probe_async(const wchar_t *path, EngineProbeCallback callback, void *user);
int  engine_is_busy(void);
void engine_cancel(void);

#endif /* ENGINE_H */
