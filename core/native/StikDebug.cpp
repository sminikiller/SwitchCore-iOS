// SPDX-License-Identifier: GPL-3.0-only
#include "StikDebug.hpp"
#include "Probes.hpp"
#include <atomic>
#include <csetjmp>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <unistd.h>

namespace {
constexpr uint64_t page_size = 16384, limit = 1ull << 36;
constexpr uint32_t ret = 0xD65F03C0u;
constexpr uint32_t mov_w0(uint32_t value) { return 0x52800000u | value << 5; }

void note(const std::string &journal, const char *step, bool first = false) {
    const int fd = open(journal.c_str(), O_WRONLY | O_CREAT | O_APPEND | (first ? O_TRUNC : 0), 0644);
    if (fd < 0)
        return;
    const std::string line = std::string(step) + '\n';
    (void)!write(fd, line.data(), line.size());
    fsync(fd);
    close(fd);
}

#if defined(__aarch64__)
sigjmp_buf recovery;
volatile sig_atomic_t raised = 0;
void recover(int signal) {
    raised = signal;
    siglongjmp(recovery, 1);
}

// Calls generated code. SIGBUS, SIGSEGV and SIGILL are reported instead of ending the process;
// a code-signing kill cannot be intercepted, which is what the journal is for.
int call(uint64_t code, int &fault) {
    struct sigaction action = {}, bus = {}, segv = {}, ill = {};
    action.sa_handler = recover;
    sigemptyset(&action.sa_mask);
    sigaction(SIGBUS, &action, &bus);
    sigaction(SIGSEGV, &action, &segv);
    sigaction(SIGILL, &action, &ill);
    volatile int result = -1;
    if (sigsetjmp(recovery, 1) == 0)
        result = reinterpret_cast<int (*)()>(code)();
    else
        fault = raised;
    sigaction(SIGBUS, &bus, nullptr);
    sigaction(SIGSEGV, &segv, nullptr);
    sigaction(SIGILL, &ill, nullptr);
    return result;
}

void write_code(uint64_t alias, uint64_t code, uint32_t value) {
    const uint32_t function[] = {mov_w0(value), ret};
    std::memcpy(reinterpret_cast<void *>(alias), function, sizeof(function));
    sys_icache_invalidate(reinterpret_cast<void *>(code), sizeof(function));
}

// One page below 2^36: the script encodes addresses in 36 bits.
vm_address_t reserve() {
    const mach_port_t task = mach_task_self();
    vm_address_t at = 0;
    if (vm_allocate(task, &at, page_size, VM_FLAGS_ANYWHERE) == KERN_SUCCESS) {
        if (at + page_size <= limit)
            return at;
        vm_deallocate(task, at, page_size);
    }
    for (uint64_t address = 4ull << 30; address + page_size <= limit; address += 256ull << 20) {
        at = address;
        if (vm_allocate(task, &at, page_size, VM_FLAGS_FIXED) == KERN_SUCCESS)
            return at;
    }
    return 0;
}
#endif
} // namespace

namespace switchcore::native {
bool can_call_jit26() { return (code_signing_flags() & cs_debugged) && debugger_attached(); }

#if defined(__aarch64__)
__attribute__((naked, noinline)) void *jit26_prepare_region(void *, size_t) {
    asm volatile("mov x16, #1\n"
                 "brk #0xf00d\n"
                 "ret\n");
}

__attribute__((naked, noinline)) void jit26_detach() {
    asm volatile("mov x16, #0\n"
                 "brk #0xf00d\n"
                 "ret\n");
}
#else
void *jit26_prepare_region(void *, size_t) { return nullptr; }
void jit26_detach() {}
#endif

JitReport probe_jit26(const std::string &journal) {
    JitReport report;
    report.size = page_size;
    if (!(code_signing_flags() & cs_debugged)) {
        report.error = "CS_DEBUGGED assente: nessun brk eseguito.";
        return report;
    }
    if (!debugger_attached()) {
        report.error = "Nessun debugger collegato: nessun brk eseguito.";
        return report;
    }
#if defined(__aarch64__)
    // After the detach CS_DEBUGGED stays set but nobody services the brk any more.
    static std::atomic<bool> used{false};
    if (used.exchange(true)) {
        report.error = "Protocollo già concluso in questo processo: riaprire l'app.";
        return report;
    }
    report.attempted = true;
    note(journal, "inizio", true);
    const mach_port_t task = mach_task_self();
    vm_address_t region = reserve();
    void *executable = nullptr;
    if (region)
        report.protect_error = vm_protect(task, region, page_size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    if (region && report.protect_error == KERN_SUCCESS) {
        report.region = region;
        note(journal, "prepare regione propria");
        executable = jit26_prepare_region(reinterpret_cast<void *>(region), page_size);
    } else {
        // The script can allocate the executable page itself through debugserver.
        if (region)
            vm_deallocate(task, region, page_size);
        report.script_allocated = true;
        note(journal, "prepare regione del debugger");
        executable = jit26_prepare_region(nullptr, page_size);
    }
    report.executable = reinterpret_cast<uint64_t>(executable);
    const uint64_t code = report.executable;
    vm_address_t alias = 0;
    if (code && code % page_size == 0 && code + page_size <= limit) {
        note(journal, "alias vm_remap");
        vm_prot_t current = 0, maximum = 0;
        report.remap_error = vm_remap(task, &alias, page_size, 0, VM_FLAGS_ANYWHERE, task, code, FALSE,
                                      &current, &maximum, VM_INHERIT_NONE);
        report.alias_protection = current;
        report.alias_max_protection = maximum;
        if (report.remap_error == KERN_SUCCESS)
            report.remap_error = vm_protect(task, alias, page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
        if (report.remap_error == KERN_SUCCESS) {
            report.alias = alias;
            note(journal, "scrittura 42 e chiamata prima del distacco");
            write_code(alias, code, 42);
            report.before_detach = call(code, report.fault);
        } else
            report.error = "Alias RW non creato.";
    } else
        report.error = "Indirizzo eseguibile non valido o oltre 2^36.";
    note(journal, "distacco");
    jit26_detach();
    report.detached = true;
    if (report.before_detach == 42) {
        note(journal, "chiamata dopo il distacco");
        report.after_detach = call(code, report.fault);
    }
    if (report.after_detach == 42) {
        note(journal, "riscrittura 43 e chiamata");
        write_code(report.alias, code, 43);
        report.rewritten = call(code, report.fault);
    }
    note(journal, "fine");
#else
    report.error = "Protocollo disponibile solo su arm64.";
#endif
    return report;
}
} // namespace switchcore::native
