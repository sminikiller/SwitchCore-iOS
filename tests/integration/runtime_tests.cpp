// SPDX-License-Identifier: GPL-3.0-only
#include "core/memory/memory.hpp"
#include "core/runtime/SwitchCore.h"
#include "video_core/gpu.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace NeXo2::Core;
void expect(bool condition, const char *reason) {
    if (!condition)
        throw std::runtime_error(reason);
}

int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    try {
        Memory mem;
        mem.MapRegion(0x1000, 0x1000, MemoryState::Code, MemoryPermission::ReadExecute, "text");
        mem.MapRegion(0x2000, 0x1000, MemoryState::Normal, MemoryPermission::ReadWrite, "heap");
        mem.EnableGuestProtection();
        auto allowed = [&](uint64_t address, unsigned length, unsigned access) {
            try {
                mem.RequireGuestAccess(address, length, access);
                return true;
            } catch (const std::runtime_error &) {
                return false;
            }
        };
        expect(allowed(0x1000, 4, 4), "execute mapped text");
        expect(!allowed(0x1000, 4, 2), "protect executable text");
        expect(!allowed(0x2000, 4, 4), "protect heap from fetch");
        expect(allowed(0x1ffe, 4, 1), "read across compatible mappings");
        expect(!allowed(0x2ffe, 4, 2), "reject partial cross-page store");
        expect(!allowed(UINT64_MAX - 1, 4, 1), "reject wrapped range");
        mem.UnmapRegion(0x2000, 0x1000);
        expect(!allowed(0x2000, 1, 1), "invalidate cached permissions");
        std::unique_ptr<SCSession, decltype(&sc_destroy)> session(sc_create(), sc_destroy);
        expect(sc_load(session.get(), SC_SWITCH_1, argv[1], argv[2]), "load Rally");
        expect(sc_step(session.get(), 1024).instructions == 1024, "execute instruction slice");
        expect(!sc_load(session.get(), SC_SWITCH_2_RESEARCH, argv[1], argv[2]),
               "research backend rejects boot");
        expect(sc_step(session.get(), 20).state == SC_FAULT, "no stale running process");
        expect(!sc_load(session.get(), SC_SWITCH_1, "", argv[2]), "missing file rejected");
        expect(sc_load(session.get(), SC_SWITCH_1, argv[1], argv[2]), "recover following failure");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
