// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Measurements for native ARM64 execution on Apple hosts (docs/nativo.md, milestone M0).
namespace switchcore::native {
// Code compiled for Horizon uses x18 as an ordinary register and TPIDR_EL0 as thread pointer.
// Counts how often a spinning thread loses either value while other threads and signals force
// context switches.
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

// Makes a region executable, writes a function through a writable alias and calls it.
bool probe_dual_mapping(std::string &error);
// Same with MAP_JIT and per-thread write protection (macOS only).
bool probe_map_jit(std::string &error);
} // namespace switchcore::native
