// SPDX-License-Identifier: GPL-3.0-only
// Interpreter and ARM64 recompiler on the same guest code: the compiled C functions of NeXo's CPU
// tests. Results must match, and each CPU path prints its guest instruction rate. A guest loop that
// keeps its state in x18 and TPIDR_EL0 then runs on the recompiler while host threads force context
// switches, which on Darwin change both registers (docs/nativo.md, M0 results).
#include "arm64/interpreter.hpp"
#include "common/logger.hpp"
#include "core/runtime/CodePool.hpp"
#include "memory.hpp"
#include "third_party/nexo/tests/generated/test_programs.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace NeXo2::Core;
namespace P = NeXo2::Tests::Programs;

namespace {
constexpr u64 code_base = 0x80000000, data_base = 0x90000000, stack_top = 0xA0000000;
constexpr u64 return_address = 0x7FFF0000, budget = u64(1) << 40;

enum class Path { Interpreter, Jit, JitPool };
const char *path_name(Path path) {
    return path == Path::Interpreter ? "interpreter" : path == Path::Jit ? "jit" : "jit-pool";
}

struct Workload {
    const char *name;
    u64 function;
    std::vector<u64> args;
    u64 data_bytes; // pseudo-random bytes at data_base, hashed again after the call
};

struct Result {
    bool halted_at_return = false;
    u64 value = 0, checksum = 0, instructions = 0, code = 0;
    double seconds = 0;
};

u64 fnv1a(Memory &mem, u64 address, u64 bytes) {
    std::vector<u8> data(bytes);
    mem.ReadBytes(address, data.data(), data.size());
    u64 hash = 0xCBF29CE484222325ull;
    for (u8 byte : data)
        hash = (hash ^ byte) * 0x100000001B3ull;
    return hash;
}

// One CPU with the c_functions program, set up like cpu_tests.cpp (Machine::Call).
Result call(Path path, const Workload &work) {
    Memory mem;
    Interpreter cpu(mem);
    if (path == Path::JitPool)
        JitBackend::SetCodeMemory(&switchcore::code_pool());
    cpu.SetJitEnabled(path != Path::Interpreter);
    JitBackend::SetCodeMemory(nullptr);
    mem.WriteBytes(code_base, P::c_functions, sizeof(P::c_functions));
    mem.Write<u32>(return_address, 0xD4200000u); // brk #0
    u64 seed = 0x9E3779B97F4A7C15ull;
    std::vector<u8> data(work.data_bytes);
    for (u8 &byte : data) {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        byte = u8(seed);
    }
    mem.WriteBytes(data_base, data.data(), data.size());
    auto &state = cpu.GetState();
    for (size_t i = 0; i < work.args.size(); ++i)
        state.x[i] = work.args[i];
    state.x[30] = return_address;
    state.sp = stack_top;
    state.pc = code_base + work.function;
    const u64 before = cpu.GetInstructionCount();
    const auto start = std::chrono::steady_clock::now();
    cpu.Run(budget);
    Result result;
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    result.instructions = cpu.GetInstructionCount() - before;
    result.halted_at_return = cpu.IsHalted() && state.pc == return_address;
    result.value = state.x[0];
    result.checksum = fnv1a(mem, data_base, work.data_bytes);
    if (const auto *jit = cpu.GetJit())
        result.code = jit->GetStats().code_base;
    return result;
}

// x0 = iterations. x18 and TPIDR_EL0 advance together; x4 collects any difference between them.
constexpr u32 register_loop[] = {
    0x91000E52u, // add x18, x18, #3
    0xD51BD052u, // msr tpidr_el0, x18
    0xD53BD042u, // mrs x2, tpidr_el0
    0xCA120043u, // eor x3, x2, x18
    0xAA030084u, // orr x4, x4, x3
    0xF1000400u, // subs x0, x0, #1
    0x54FFFF41u, // b.ne register_loop
    0xD4200000u, // brk #0
};

bool register_stress(Path path, u64 iterations, bool noise) {
    Memory mem;
    Interpreter cpu(mem);
    if (path == Path::JitPool)
        JitBackend::SetCodeMemory(&switchcore::code_pool());
    cpu.SetJitEnabled(path != Path::Interpreter);
    JitBackend::SetCodeMemory(nullptr);
    mem.WriteBytes(code_base, register_loop, sizeof(register_loop));
    auto &state = cpu.GetState();
    const u64 first = 0x5A5A000000000000ull;
    state.x[0] = iterations;
    state.x[18] = first;
    state.tpidr_el0 = 0;
    state.pc = code_base;
    // More busy host threads than cores: the scheduler preempts the guest thread again and again.
    std::atomic<bool> done{false};
    std::vector<std::thread> threads;
    const unsigned count = noise ? std::max(2u, std::thread::hardware_concurrency()) * 2 : 0;
    for (unsigned i = 0; i < count; ++i)
        threads.emplace_back([&done] {
            volatile u64 spin = 0;
            while (!done.load(std::memory_order_relaxed))
                spin = spin + 1;
        });
    const auto start = std::chrono::steady_clock::now();
    cpu.Run(budget);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    done = true;
    for (auto &thread : threads)
        thread.join();
    const u64 expected = first + 3 * iterations;
    const bool ok = cpu.IsHalted() && state.x[18] == expected && state.tpidr_el0 == expected &&
                    state.x[2] == expected && state.x[4] == 0;
    std::printf("registers path=%s iterations=%llu host_threads=%u seconds=%.3f x18_ok=%d tpidr_ok=%d "
                "mismatch_bits=%llx result=%s\n",
                path_name(path), (unsigned long long)iterations, count, seconds, state.x[18] == expected,
                state.tpidr_el0 == expected, (unsigned long long)state.x[4], ok ? "ok" : "FAIL");
    return ok;
}
} // namespace

int main() {
    NeXo2::Common::Logger::SetMuted(true);
    if (!Interpreter::JitAvailable()) {
        std::printf("recompiler not built\n");
        return 1;
    }
    // The pool exists on Apple arm64 hosts; elsewhere (x86-64 test builds) its rows are skipped.
    std::string error;
    const bool pool = switchcore::code_pool().reserve(error);
#if defined(__APPLE__) && defined(__aarch64__)
    if (!pool) {
        std::printf("code pool: %s\n", error.c_str());
        return 1;
    }
#endif
    std::vector<Path> paths = {Path::Interpreter, Path::Jit};
    if (pool)
        paths.push_back(Path::JitPool);
    const std::vector<Workload> workloads = {
        {"count_primes", P::c_functions_count_primes, {300000}, 0},
        {"fibonacci", P::c_functions_fibonacci, {10000000}, 0},
        {"hash_fnv1a", P::c_functions_hash_fnv1a, {data_base, 4 << 20}, 4 << 20},
        {"bubble_sort", P::c_functions_bubble_sort, {data_base, 2000}, 2000 * 8},
    };
    bool ok = true;
    double totals[3] = {}, counts[3] = {};
    for (const auto &work : workloads) {
        const Result reference = call(Path::Interpreter, work);
        ok = ok && reference.halted_at_return;
        for (Path path : paths) {
            const Result r = path == Path::Interpreter ? reference : call(path, work);
            const bool same =
                r.halted_at_return && r.value == reference.value && r.checksum == reference.checksum;
            // The pool keeps translated code below 2^36, as the iOS 26 JIT protocol requires.
            const bool pooled = path != Path::JitPool ||
                                (r.code && r.code + switchcore::CodePool::slot_bytes <= (u64(1) << 36));
            ok = ok && same && pooled;
            totals[int(path)] += r.seconds;
            counts[int(path)] += double(r.instructions);
            std::printf("bench workload=%s path=%s instructions=%llu seconds=%.4f mips=%.1f value=%llx "
                        "code_base=%llx result=%s\n",
                        work.name, path_name(path), (unsigned long long)r.instructions, r.seconds,
                        r.instructions / r.seconds / 1e6, (unsigned long long)r.value,
                        (unsigned long long)r.code, same && pooled ? "ok" : "FAIL");
        }
    }
    for (Path path : paths)
        std::printf("summary path=%s instructions=%.0f seconds=%.4f mips=%.1f speedup=%.2f\n",
                    path_name(path), counts[int(path)], totals[int(path)],
                    counts[int(path)] / totals[int(path)] / 1e6,
                    (counts[int(path)] / totals[int(path)]) / (counts[0] / totals[0]));
    ok = register_stress(Path::Interpreter, 100000, false) && ok;
    for (Path path : paths)
        if (path != Path::Interpreter)
            ok = register_stress(path, 30000000, true) && ok;
    std::printf("%s\n", ok ? "jit-bench: ok" : "jit-bench: FAIL");
    return ok ? 0 : 1;
}
