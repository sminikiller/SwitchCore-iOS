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
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <thread>
#include <unistd.h>

// Exported by libsystem_kernel on macOS and iOS; the SDKs ship no header for it.
extern "C" int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);

namespace {
#if defined(__aarch64__)
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
#endif

// mov w0, #42; ret
constexpr uint32_t answer[] = {0x52800540u, 0xD65F03C0u};
constexpr vm_size_t page = 16384;
constexpr uint64_t chunk = 256ull << 20;

bool reservable(uint64_t address, uint64_t size) {
    vm_address_t at = address;
    if (vm_allocate(mach_task_self(), &at, size, VM_FLAGS_FIXED) != KERN_SUCCESS)
        return false;
    vm_deallocate(mach_task_self(), at, size);
    return true;
}
} // namespace

namespace switchcore::native {
RegisterReport probe_registers(unsigned threads, uint64_t iterations) {
    RegisterReport report;
#if defined(__aarch64__)
    struct sigaction action = {}, previous = {};
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
    report.checks = uint64_t(threads) * iterations;
    report.signals = delivered.load();
    for (unsigned i = 0; i < threads; ++i) {
        report.x18_changed += x18[i];
        report.tpidr_changed += tpidr[i];
    }
#else
    (void)threads;
    (void)iterations;
#endif
    return report;
}

AddressReport probe_addresses() {
    AddressReport report;
    constexpr uint64_t first = 4ull << 30, last = 64ull << 30;
    uint64_t base = 0, size = 0;
    for (uint64_t address = first; address < last; address += chunk) {
        const bool reserved = reservable(address, chunk);
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

std::vector<HighAddress> probe_high_addresses() {
    std::vector<HighAddress> report;
    for (uint64_t gib : {64, 128, 256, 448, 1024, 4096, 16384, 65536})
        report.push_back({gib << 30, reservable(gib << 30, chunk)});
    return report;
}

uint32_t code_signing_flags() {
    uint32_t flags = 0;
    return csops(getpid(), 0, &flags, sizeof(flags)) == 0 ? flags : 0;
}

bool debugger_attached() {
    int name[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    struct kinfo_proc info = {};
    size_t size = sizeof(info);
    return sysctl(name, 4, &info, &size, nullptr, 0) == 0 && (info.kp_proc.p_flag & P_TRACED) != 0;
}

bool probe_dual_mapping(std::string &error) {
#if defined(__aarch64__)
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
#else
    error = "arm64 only";
    return false;
#endif
}

bool probe_map_jit(std::string &error) {
#if TARGET_OS_OSX && defined(__aarch64__)
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
