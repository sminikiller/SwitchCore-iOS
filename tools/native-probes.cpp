// SPDX-License-Identifier: GPL-3.0-only
// Milestone M0 of docs/nativo.md: facts that decide how guest ARM64 code can run natively.
#include "core/native/Probes.hpp"
#include <algorithm>
#include <iostream>
#include <thread>

int main() {
    using namespace switchcore::native;
    const unsigned threads = std::max(2u, std::thread::hardware_concurrency() * 2);
    const auto registers = probe_registers(threads, 100000);
    std::cout << "registers threads=" << threads << " checks=" << registers.checks
              << " signals=" << registers.signals << " x18_changed=" << registers.x18_changed
              << " tpidr_el0_changed=" << registers.tpidr_changed << '\n';

    const auto addresses = probe_addresses();
    std::cout << "addresses_4_to_64_gib=";
    for (bool reservable : addresses.reservable)
        std::cout << (reservable ? '+' : '.');
    std::cout << " largest=" << (addresses.largest_size >> 20) << "MiB@0x" << std::hex
              << addresses.largest_base << std::dec << '\n';
    std::cout << "addresses_above_64_gib=";
    for (const auto &high : probe_high_addresses())
        std::cout << (high.address >> 30) << 'G' << (high.reservable ? '+' : '.') << ' ';
    std::cout << "cs_flags=0x" << std::hex << code_signing_flags() << std::dec
              << " debugger=" << debugger_attached() << '\n';

    std::string dual, jit;
    const bool dual_ok = probe_dual_mapping(dual);
    const bool jit_ok = probe_map_jit(jit);
    std::cout << "dual_mapping=" << (dual_ok ? "ok" : dual) << " map_jit=" << (jit_ok ? "ok" : jit) << '\n';
    return dual_ok || jit_ok ? 0 : 1;
}
