// SPDX-License-Identifier: GPL-3.0-only
#include "Probes.hpp"
#include <TargetConditionals.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sys/mman.h>
#include <thread>

namespace {
// x0 iterations, x1 value for x18 and TPIDR_EL0, x2 receives the TPIDR_EL0 changes.
// Returns the x18 changes. Both registers get their previous values back before returning.
__attribute__((naked)) uint64_t spin(uint64_t, uint64_t, uint64_t *) {
    asm volatile("mrs x9, tpidr_el0\n"
                 "mov x10, x18\n"
                 "mov x11, #0\n"
                 "mov x12, #0\n"
                 "mov x18, x1\n"
                 "msr tpidr_el0, x1\n"
                 "1: cbz x0, 3f\n"
                 "mov x13, #4000\n"
                 "2: subs x13, x13, #1\n"
                 "b.ne 2b\n"
                 "cmp x18, x1\n"
                 "cinc x11, x11, ne\n"
                 "mov x18, x1\n"
                 "mrs x14, tpidr_el0\n"
                 "cmp x14, x1\n"
                 "cinc x12, x12, ne\n"
                 "msr tpidr_el0, x1\n"
                 "sub x0, x0, #1\n"
                 "b 1b\n"
                 "3: str x12, [x2]\n"
                 "msr tpidr_el0, x9\n"
                 "mov x18, x10\n"
                 "mov x0, x11\n"
                 "ret\n");
}

std::atomic<uint64_t> delivered{0};
void count_signal(int) { delivered.fetch_add(1, std::memory_order_relaxed); }

// mov w0, #42; ret
constexpr uint32_t answer[] = {0x52800540u, 0xD65F03C0u};
constexpr vm_size_t page = 16384;
} // namespace

namespace switchcore::native {
RegisterReport probe_registers(unsigned threads, uint64_t iterations) {
    struct sigaction action{}, previous{};
    action.sa_handler = count_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR2, &action, &previous);
    delivered = 0;
    std::vector<uint64_t> x18(threads), tpidr(threads);
    std::atomic<unsigned> running{threads};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < threads; ++i)
        workers.emplace_back([&, i] {
            x18[i] = spin(iterations, 0x5A5A000000000000ull | i, &tpidr[i]);
            running.fetch_sub(1);
        });
    while (running.load() > 0) {
        for (auto &worker : workers)
            pthread_kill(worker.native_handle(), SIGUSR2);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (auto &worker : workers)
        worker.join();
    sigaction(SIGUSR2, &previous, nullptr);
    RegisterReport report;
    report.checks = uint64_t(threads) * iterations;
    report.signals = delivered.load();
    for (unsigned i = 0; i < threads; ++i) {
        report.x18_changed += x18[i];
        report.tpidr_changed += tpidr[i];
    }
    return report;
}

AddressReport probe_addresses() {
    AddressReport report;
    constexpr uint64_t chunk = 256ull << 20, first = 4ull << 30, last = 64ull << 30;
    uint64_t base = 0, size = 0;
    for (uint64_t address = first; address < last; address += chunk) {
        vm_address_t at = address;
        const bool reserved = vm_allocate(mach_task_self(), &at, chunk, VM_FLAGS_FIXED) == KERN_SUCCESS;
        if (reserved)
            vm_deallocate(mach_task_self(), at, chunk);
        report.reservable.push_back(reserved);
        size = reserved ? size + chunk : 0;
        base = size == chunk ? address : base;
        if (size > report.largest_size) {
            report.largest_size = size;
            report.largest_base = base;
        }
    }
    return report;
}

bool probe_dual_mapping(std::string &error) {
    const mach_port_t task = mach_task_self();
    vm_address_t code = 0, alias = 0;
    if (vm_allocate(task, &code, page, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
        error = "vm_allocate";
        return false;
    }
    vm_prot_t current = 0, maximum = 0;
    bool ok = false;
    if (vm_remap(task, &alias, page, 0, VM_FLAGS_ANYWHERE, task, code, FALSE, &current, &maximum,
                 VM_INHERIT_NONE) != KERN_SUCCESS)
        error = "vm_remap";
    else if (vm_protect(task, code, page, FALSE, VM_PROT_READ | VM_PROT_EXECUTE) != KERN_SUCCESS)
        error = "vm_protect RX";
    else {
        std::memcpy(reinterpret_cast<void *>(alias), answer, sizeof(answer));
        sys_icache_invalidate(reinterpret_cast<void *>(code), sizeof(answer));
        ok = reinterpret_cast<int (*)()>(code)() == 42;
        if (!ok)
            error = "wrong result";
    }
    if (alias)
        vm_deallocate(task, alias, page);
    vm_deallocate(task, code, page);
    return ok;
}

bool probe_map_jit(std::string &error) {
#if TARGET_OS_OSX
    void *code =
        mmap(nullptr, page, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (code == MAP_FAILED) {
        error = "mmap MAP_JIT";
        return false;
    }
    pthread_jit_write_protect_np(0);
    std::memcpy(code, answer, sizeof(answer));
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(code, sizeof(answer));
    const bool ok = reinterpret_cast<int (*)()>(code)() == 42;
    if (!ok)
        error = "wrong result";
    munmap(code, page);
    return ok;
#else
    error = "MAP_JIT requires an entitlement outside macOS";
    return false;
#endif
}
} // namespace switchcore::native
