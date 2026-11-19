// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// C entry points of the M0 probes (docs/nativo.md) for the iOS diagnostics panel. Each call is
// synchronous; run them away from the main thread.
typedef struct {
    uint32_t threads;
    uint64_t checks, signals, x18_changed, tpidr_changed;
} SCRegisterProbe;
// checks is 0 on hosts other than arm64.
SCRegisterProbe sc_native_registers(uint32_t threads, uint64_t iterations);
// One character per 256 MiB chunk from 4 GiB to 64 GiB: '+' reservable at that fixed address, '.' not.
void sc_native_addresses(char *map, size_t capacity, uint64_t *largest_base, uint64_t *largest_size);
// "64G+ 128G. ..." for 256 MiB reserved at fixed addresses from 64 GiB up.
void sc_native_high_addresses(char *text, size_t capacity);
uint32_t sc_native_cs_flags(void);
bool sc_native_debugger_attached(void);
typedef struct {
    bool attempted, detached, script_allocated;
    uint64_t region, executable, alias, size;
    int32_t alias_protection, alias_max_protection, protect_error, remap_error;
    int32_t before_detach, after_detach, rewritten, fault;
} SCJitProbe;
// StikDebug universal.js protocol test. Runs brk only while CS_DEBUGGED is set and a debugger is
// attached, and only once per process. Steps are appended to journal_path before they run.
SCJitProbe sc_native_jit(const char *journal_path, char *error, size_t capacity);
#ifdef __cplusplus
}
#endif
