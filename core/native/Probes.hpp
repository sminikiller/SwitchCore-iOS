// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Measurements for native ARM64 execution on Apple hosts (docs/nativo.md, milestone M0).
// Used by tools/native-probes.cpp on macOS and by the diagnostics panel of the iOS app.
namespace switchcore::native {
// Code compiled for Horizon uses x18 as an ordinary register and TPIDR_EL0 as thread pointer.
// Counts how often a spinning thread loses either value while other threads and signals force
// context switches. Reports zero checks on hosts other than arm64 (x86_64 simulator).
struct RegisterReport {
    uint64_t checks = 0, x18_changed = 0, tpidr_changed = 0, signals = 0;
};
RegisterReport probe_registers(unsigned threads, uint64_t iterations);

// Guest memory needs host addresses equal to guest addresses, below the 36-bit limit of the
// iOS 26 JIT protocol. One entry per 256 MiB chunk from 4 GiB to 64 GiB.
struct AddressReport {
    std::vector<bool> reservable;
    uint64_t largest_base = 0, largest_size = 0;
};
AddressReport probe_addresses();

// 256 MiB at fixed addresses from 64 GiB up. On iOS only extended-virtual-addressing opens them.
struct HighAddress {
    uint64_t address = 0;
    bool reservable = false;
};
std::vector<HighAddress> probe_high_addresses();

// Code-signing flags of this process (csops CS_OPS_STATUS), 0 when unavailable.
uint32_t code_signing_flags();
constexpr uint32_t cs_get_task_allow = 0x4, cs_debugged = 0x10000000;
// True while a debugger is attached to this process (P_TRACED).
bool debugger_attached();

// Makes a region executable, writes a function through a writable alias and calls it.
// Without a JIT grant iOS kills the process on the call, so the app never runs this.
bool probe_dual_mapping(std::string &error);
// Same with MAP_JIT and per-thread write protection (macOS only).
bool probe_map_jit(std::string &error);
} // namespace switchcore::native
