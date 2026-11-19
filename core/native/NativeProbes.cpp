// SPDX-License-Identifier: GPL-3.0-only
#include "NativeProbes.h"
#include "Probes.hpp"
#include "StikDebug.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace native = switchcore::native;

namespace {
void copy(const std::string &text, char *out, size_t capacity) {
    if (!out || !capacity)
        return;
    const size_t length = std::min(text.size(), capacity - 1);
    std::memcpy(out, text.data(), length);
    out[length] = 0;
}
} // namespace

SCRegisterProbe sc_native_registers(uint32_t threads, uint64_t iterations) {
    const auto report = native::probe_registers(std::max(threads, 1u), iterations);
    return {threads, report.checks, report.signals, report.x18_changed, report.tpidr_changed};
}

void sc_native_addresses(char *map, size_t capacity, uint64_t *largest_base, uint64_t *largest_size) {
    const auto report = native::probe_addresses();
    std::string text;
    for (bool reservable : report.reservable)
        text += reservable ? '+' : '.';
    copy(text, map, capacity);
    if (largest_base)
        *largest_base = report.largest_base;
    if (largest_size)
        *largest_size = report.largest_size;
}

void sc_native_high_addresses(char *out, size_t capacity) {
    std::string text;
    for (const auto &high : native::probe_high_addresses()) {
        char item[40];
        std::snprintf(item, sizeof(item), "%s%lluG%c", text.empty() ? "" : " ",
                      static_cast<unsigned long long>(high.address >> 30), high.reservable ? '+' : '.');
        text += item;
    }
    copy(text, out, capacity);
}

uint32_t sc_native_cs_flags(void) { return native::code_signing_flags(); }

bool sc_native_debugger_attached(void) { return native::debugger_attached(); }

SCJitProbe sc_native_jit(const char *journal_path, char *error, size_t capacity) {
    const auto report = native::probe_jit26(journal_path ? journal_path : "");
    copy(report.error, error, capacity);
    SCJitProbe out = {};
    out.attempted = report.attempted;
    out.detached = report.detached;
    out.script_allocated = report.script_allocated;
    out.region = report.region;
    out.executable = report.executable;
    out.alias = report.alias;
    out.size = report.size;
    out.alias_protection = report.alias_protection;
    out.alias_max_protection = report.alias_max_protection;
    out.protect_error = report.protect_error;
    out.remap_error = report.remap_error;
    out.before_detach = report.before_detach;
    out.after_detach = report.after_detach;
    out.rewritten = report.rewritten;
    out.fault = report.fault;
    return out;
}
