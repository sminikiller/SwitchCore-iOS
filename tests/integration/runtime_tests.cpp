// SPDX-License-Identifier: GPL-3.0-only
#include "core/loader/nso.hpp"
#include "core/memory/memory.hpp"
#include "core/runtime/SdPath.hpp"
#include "core/runtime/SwitchCore.h"
#include "video_core/gpu.hpp"
#include <filesystem>
#include <fstream>
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
        const auto sd = std::filesystem::path(argv[2]) / "sd-paths";
        std::filesystem::create_directories(sd / "switch");
        const auto game = std::filesystem::weakly_canonical(sd / "switch" / "game.nro");
        expect(switchcore::sd_file(sd, "sdmc:/switch/game.nro") == game, "map guest file into SD");
        expect(switchcore::sd_file(sd, "/switch/./game.nro") == game, "accept path without device");
        expect(!switchcore::sd_file(sd, "sdmc:/../game.nro"), "reject parent escape");
        expect(!switchcore::sd_file(sd, "sdmc:/switch/../../game.nro"), "reject nested escape");
        expect(!switchcore::sd_file(sd, "sdmc:/"), "reject SD root");
        std::unique_ptr<SCSession, decltype(&sc_destroy)> session(sc_create(), sc_destroy);
        expect(sc_load(session.get(), SC_SWITCH_1, argv[1], argv[2]), "load Rally");
        expect(sc_step(session.get(), 1024).instructions == 1024, "execute instruction slice");
        expect(!sc_load(session.get(), SC_SWITCH_2_RESEARCH, argv[1], argv[2]),
               "research backend rejects boot");
        expect(sc_step(session.get(), 20).state == SC_FAULT, "no stale running process");
        expect(!sc_load(session.get(), SC_SWITCH_1, "", argv[2]), "missing file rejected");
        expect(sc_load(session.get(), SC_SWITCH_1, argv[1], argv[2]), "recover following failure");
        const auto folder = std::filesystem::path(argv[2]) / "application";
        std::filesystem::create_directories(folder / "exefs");
        NeXo2::Loader::NsoHeader header{};
        header.magic = 0x304f534e;
        header.text = {sizeof(header), 0, 4};
        header.ro.memory_offset = header.data.memory_offset = 4096;
        const uint32_t exitProcess = 0xd40000e1;
        auto writeApplication = [&] {
            std::ofstream out(folder / "exefs" / "main", std::ios::binary);
            out.write(reinterpret_cast<const char *>(&header), sizeof(header));
            out.write(reinterpret_cast<const char *>(&exitProcess), sizeof(exitProcess));
            expect(out.good(), "write application fixture");
        };
        writeApplication();
        expect(sc_load(session.get(), SC_SWITCH_1, folder.string().c_str(), argv[2]), "boot extracted NSO");
        expect(sc_step(session.get(), 128).state == SC_EXITED, "NSO executes ExitProcess");
        header.bss_size = 1u << 30;
        writeApplication();
        expect(!sc_load(session.get(), SC_SWITCH_1, folder.string().c_str(), argv[2]),
               "bound image size before mapping BSS");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
