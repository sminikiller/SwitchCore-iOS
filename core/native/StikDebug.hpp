// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Breakpoint protocol of StikDebug's universal.js script, which makes memory executable on iOS 26
// devices with TXM (docs/nativo.md). Each call stops on brk #0xf00d with the command in x16; the
// attached script services it and resumes after the brk. Without the script attached the brk kills
// the process, so nothing here runs a brk unless can_call_jit26() holds.
// Protocol reference: https://github.com/StikDebug/StikJIT/blob/main/INTEGRATION.md (MPL-2.0).
// This is an independent implementation; no StikJIT code is included.
namespace switchcore::native {
// CS_DEBUGGED is set and a debugger is attached right now.
bool can_call_jit26();
// x0 = address (16 KiB pages, below 2^36) or null to let the debugger allocate, x1 = length.
// Returns the executable address in x0.
void *jit26_prepare_region(void *address, size_t length);
// The script detaches; it prepares no further regions for this process.
void jit26_detach();

struct JitReport {
    bool attempted = false, detached = false, script_allocated = false;
    uint64_t region = 0, executable = 0, alias = 0, size = 0;
    int alias_protection = 0, alias_max_protection = 0, protect_error = 0, remap_error = 0;
    // Values returned by the generated code, -1 when not called. fault is the signal it raised.
    int before_detach = -1, after_detach = -1, rewritten = -1, fault = 0;
    std::string error;
};
// M0 test: prepares one page, writes `mov w0, #42; ret` through a vm_remap alias, calls it,
// detaches, calls it again and rewrites it to return 43. Runs once per process. Every step is
// appended to the journal file before it runs, so a crash leaves the last step on disk.
JitReport probe_jit26(const std::string &journal);
} // namespace switchcore::native
