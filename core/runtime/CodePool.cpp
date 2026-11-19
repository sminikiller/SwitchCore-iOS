// SPDX-License-Identifier: GPL-3.0-only
#include "CodePool.hpp"
#if defined(NEXO2_HAS_JIT) && defined(__APPLE__) && defined(__aarch64__)
#include <mach/mach.h>
#define SWITCHCORE_CODE_POOL 1
#endif

namespace switchcore {
#ifdef SWITCHCORE_CODE_POOL
namespace {
// The iOS 26 JIT script transmits 36-bit addresses, so the executable view ends below 64 GiB.
// The first 4 GiB belong to __PAGEZERO. Candidates advance in 256 MiB steps, as in the M0 probe.
constexpr uint64_t lowest = uint64_t(4) << 30, limit = uint64_t(1) << 36, step = uint64_t(256) << 20;
} // namespace
#endif

bool CodePool::reserve(std::string &error) {
    std::lock_guard guard(lock);
    if (execute)
        return true;
#ifdef SWITCHCORE_CODE_POOL
    const mach_port_t task = mach_task_self();
    const vm_size_t size = slots * slot_bytes;
    vm_address_t base = 0;
    for (uint64_t at = lowest; !base && at + size <= limit; at += step) {
        vm_address_t candidate = at;
        if (vm_allocate(task, &candidate, size, VM_FLAGS_FIXED) == KERN_SUCCESS)
            base = candidate;
    }
    if (!base) {
        error = "Nessun intervallo libero sotto 64 GiB per il codice tradotto.";
        return false;
    }
    vm_address_t alias = 0;
    vm_prot_t current = 0, maximum = 0;
    if (vm_remap(task, &alias, size, 0, VM_FLAGS_ANYWHERE, task, base, FALSE, &current, &maximum,
                 VM_INHERIT_NONE) != KERN_SUCCESS) {
        vm_deallocate(task, base, size);
        error = "Alias scrivibile del codice tradotto non disponibile.";
        return false;
    }
    // macOS changes the protection directly. On iOS 26 with TXM (milestone M2) StikDebug prepares
    // the same range instead: brk #0xf00d with x16 = 1, x0 = base and x1 = size, then x16 = 0.
    if (vm_protect(task, base, size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE) != KERN_SUCCESS) {
        vm_deallocate(task, alias, size);
        vm_deallocate(task, base, size);
        error = "Il codice tradotto non può diventare eseguibile.";
        return false;
    }
    execute = base;
    write = alias;
    return true;
#else
    error = "Ricompilatore non disponibile su questo host.";
    return false;
#endif
}

NeXo2::Core::JitCodeRegion CodePool::Acquire() {
    std::lock_guard guard(lock);
    for (size_t slot = 0; execute && slot < slots; ++slot)
        if (!used[slot]) {
            used[slot] = true;
            return {reinterpret_cast<void *>(execute + slot * slot_bytes),
                    reinterpret_cast<void *>(write + slot * slot_bytes), slot_bytes};
        }
    return {};
}

void CodePool::Release(const NeXo2::Core::JitCodeRegion &region) {
    std::lock_guard guard(lock);
    const auto at = reinterpret_cast<uintptr_t>(region.execute);
    if (execute && at >= execute && at < execute + slots * slot_bytes)
        used[(at - execute) / slot_bytes] = false;
}

CodePool &code_pool() {
    static CodePool pool;
    return pool;
}
} // namespace switchcore
